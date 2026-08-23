#include "SkinArchive.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <zlib.h>

namespace {

constexpr quint32 kLocalFileHeaderSignature = 0x04034b50U;
constexpr quint32 kCentralDirectorySignature = 0x02014b50U;
constexpr quint32 kEndOfCentralDirectorySignature = 0x06054b50U;
constexpr quint16 kCompressionStored = 0;
constexpr quint16 kCompressionDeflated = 8;

constexpr qsizetype kMaxArchiveBytes = 10 * 1024 * 1024;
constexpr quint16 kMaxEntries = 64;
constexpr quint64 kMaxExpandedBytes = 32ULL * 1024ULL * 1024ULL;
constexpr quint32 kMaxThemeBytes = 256U * 1024U;
constexpr quint32 kMaxImageBytes = 8U * 1024U * 1024U;

struct ZipEntry {
    QString name;
    QByteArray raw_name;
    quint16 flags = 0;
    quint16 compression = 0;
    quint32 crc = 0;
    quint32 compressed_size = 0;
    quint32 uncompressed_size = 0;
    quint32 local_header_offset = 0;
    quint32 external_attributes = 0;
    bool directory = false;
};

quint16 readLe16(const QByteArray &bytes, qsizetype offset) {
    const auto *data = reinterpret_cast<const unsigned char *>(bytes.constData() + offset);
    return static_cast<quint16>(data[0])
        | static_cast<quint16>(static_cast<quint16>(data[1]) << 8U);
}

quint32 readLe32(const QByteArray &bytes, qsizetype offset) {
    const auto *data = reinterpret_cast<const unsigned char *>(bytes.constData() + offset);
    return static_cast<quint32>(data[0])
        | (static_cast<quint32>(data[1]) << 8U)
        | (static_cast<quint32>(data[2]) << 16U)
        | (static_cast<quint32>(data[3]) << 24U);
}

bool rangeFits(qsizetype size, quint64 offset, quint64 count) {
    const quint64 unsigned_size = static_cast<quint64>(size);
    return offset <= unsigned_size && count <= unsigned_size - offset;
}

std::optional<qsizetype> findEndOfCentralDirectory(const QByteArray &bytes) {
    constexpr qsizetype kHeaderBytes = 22;
    constexpr qsizetype kMaxCommentBytes = 0xffff;
    if (bytes.size() < kHeaderBytes) {
        return std::nullopt;
    }

    const qsizetype first = std::max<qsizetype>(0, bytes.size() - kHeaderBytes - kMaxCommentBytes);
    for (qsizetype offset = bytes.size() - kHeaderBytes; offset >= first; --offset) {
        if (readLe32(bytes, offset) == kEndOfCentralDirectorySignature) {
            const quint16 comment_size = readLe16(bytes, offset + 20);
            if (offset + kHeaderBytes + comment_size == bytes.size()) {
                return offset;
            }
        }
        if (offset == 0) {
            break;
        }
    }
    return std::nullopt;
}

bool isValidUtf8Name(const QByteArray &raw_name, QString &decoded) {
    if (raw_name.isEmpty() || raw_name.contains('\0')) {
        return false;
    }
    decoded = QString::fromUtf8(raw_name);
    return decoded.toUtf8() == raw_name;
}

bool isSymlinkOrSpecial(quint32 external_attributes) {
    constexpr quint32 kUnixFileTypeMask = 0170000U;
    constexpr quint32 kUnixRegularFile = 0100000U;
    constexpr quint32 kUnixDirectory = 0040000U;
    const quint32 mode = external_attributes >> 16U;
    const quint32 type = mode & kUnixFileTypeMask;
    return type != 0U && type != kUnixRegularFile && type != kUnixDirectory;
}

bool validateEntryName(ZipEntry &entry, QString &error_message) {
    const QString &name = entry.name;
    if (name.startsWith(u'/') || name.startsWith(u'\\') || name.contains(u'\\')) {
        error_message = QStringLiteral("Archive entry uses an absolute or backslash path: %1").arg(name);
        return false;
    }

    const auto path_parts = name.split(u'/', Qt::KeepEmptyParts);
    if (path_parts.contains(QStringLiteral("..")) || path_parts.contains(QStringLiteral("."))) {
        error_message = QStringLiteral("Archive entry contains a traversal path: %1").arg(name);
        return false;
    }

    if (isSymlinkOrSpecial(entry.external_attributes)) {
        error_message = QStringLiteral("Archive entry is a link or special file: %1").arg(name);
        return false;
    }

    const bool directory_attribute = (entry.external_attributes & 0x10U) != 0U;
    entry.directory = name.endsWith(u'/') || directory_attribute;
    if (entry.directory) {
        if (name != QStringLiteral("textures/") || entry.uncompressed_size != 0U
            || entry.compressed_size != 0U || entry.compression != kCompressionStored) {
            error_message = QStringLiteral("Archive contains an unsupported directory: %1").arg(name);
            return false;
        }
        return true;
    }

    static const QRegularExpression texture_pattern(
        QStringLiteral(R"(^textures/[A-Za-z0-9][A-Za-z0-9._-]{0,127}\.png$)"));
    const bool allowed = name == QStringLiteral("theme.json")
        || name == QStringLiteral("preview.png")
        || texture_pattern.match(name).hasMatch();
    if (!allowed) {
        error_message = QStringLiteral("Archive contains an unsupported entry: %1").arg(name);
        return false;
    }

    const quint32 entry_limit = name == QStringLiteral("theme.json")
        ? kMaxThemeBytes
        : kMaxImageBytes;
    if (entry.uncompressed_size > entry_limit) {
        error_message = QStringLiteral("Archive entry exceeds its size limit: %1").arg(name);
        return false;
    }
    return true;
}

std::optional<QByteArray> inflateEntry(
    const QByteArray &archive,
    const ZipEntry &entry,
    quint64 central_directory_offset,
    QString &error_message) {
    const quint64 local_offset = entry.local_header_offset;
    if (!rangeFits(archive.size(), local_offset, 30U)
        || readLe32(archive, static_cast<qsizetype>(local_offset)) != kLocalFileHeaderSignature) {
        error_message = QStringLiteral("Invalid local ZIP header for %1").arg(entry.name);
        return std::nullopt;
    }

    const quint16 local_flags = readLe16(archive, static_cast<qsizetype>(local_offset + 6U));
    const quint16 local_compression = readLe16(archive, static_cast<qsizetype>(local_offset + 8U));
    const quint32 local_crc = readLe32(archive, static_cast<qsizetype>(local_offset + 14U));
    const quint32 local_compressed_size = readLe32(
        archive, static_cast<qsizetype>(local_offset + 18U));
    const quint32 local_uncompressed_size = readLe32(
        archive, static_cast<qsizetype>(local_offset + 22U));
    const quint16 local_name_size = readLe16(archive, static_cast<qsizetype>(local_offset + 26U));
    const quint16 local_extra_size = readLe16(archive, static_cast<qsizetype>(local_offset + 28U));
    const quint64 name_offset = local_offset + 30U;
    if (!rangeFits(archive.size(), name_offset, local_name_size)) {
        error_message = QStringLiteral("Truncated local ZIP name for %1").arg(entry.name);
        return std::nullopt;
    }
    const QByteArray local_name = archive.mid(
        static_cast<qsizetype>(name_offset),
        static_cast<qsizetype>(local_name_size));
    if (local_name != entry.raw_name || local_flags != entry.flags
        || local_compression != entry.compression) {
        error_message = QStringLiteral("Central and local ZIP headers disagree for %1").arg(entry.name);
        return std::nullopt;
    }
    constexpr quint16 kDataDescriptorFlag = 0x0008U;
    if ((entry.flags & kDataDescriptorFlag) == 0U
        && (local_crc != entry.crc
            || local_compressed_size != entry.compressed_size
            || local_uncompressed_size != entry.uncompressed_size)) {
        error_message = QStringLiteral("Central and local ZIP sizes disagree for %1").arg(entry.name);
        return std::nullopt;
    }

    const quint64 data_offset = name_offset + local_name_size + local_extra_size;
    if (!rangeFits(archive.size(), data_offset, entry.compressed_size)
        || data_offset + entry.compressed_size > central_directory_offset) {
        error_message = QStringLiteral("ZIP data points outside the archive for %1").arg(entry.name);
        return std::nullopt;
    }

    if (entry.uncompressed_size > static_cast<quint32>(std::numeric_limits<int>::max())) {
        error_message = QStringLiteral("ZIP entry is too large for this platform: %1").arg(entry.name);
        return std::nullopt;
    }

    const auto *compressed = reinterpret_cast<const Bytef *>(
        archive.constData() + static_cast<qsizetype>(data_offset));
    QByteArray output(static_cast<qsizetype>(entry.uncompressed_size), Qt::Uninitialized);
    if (entry.compression == kCompressionStored) {
        if (entry.compressed_size != entry.uncompressed_size) {
            error_message = QStringLiteral("Stored ZIP entry has inconsistent sizes: %1").arg(entry.name);
            return std::nullopt;
        }
        if (entry.compressed_size > 0U) {
            std::copy_n(
                archive.constData() + static_cast<qsizetype>(data_offset),
                static_cast<qsizetype>(entry.compressed_size),
                output.data());
        }
    } else {
        z_stream stream{};
        stream.next_in = const_cast<Bytef *>(compressed);
        stream.avail_in = entry.compressed_size;
        Bytef empty_output = 0;
        stream.next_out = output.isEmpty()
            ? &empty_output
            : reinterpret_cast<Bytef *>(output.data());
        stream.avail_out = output.isEmpty() ? 1U : entry.uncompressed_size;
        if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
            error_message = QStringLiteral("Could not initialize ZIP decompression for %1").arg(entry.name);
            return std::nullopt;
        }
        const int result = inflate(&stream, Z_FINISH);
        inflateEnd(&stream);
        if (result != Z_STREAM_END || stream.total_out != entry.uncompressed_size
            || stream.total_in != entry.compressed_size) {
            error_message = QStringLiteral("Could not safely decompress ZIP entry: %1").arg(entry.name);
            return std::nullopt;
        }
    }

    const auto *output_data = reinterpret_cast<const Bytef *>(output.constData());
    const uLong actual_crc = crc32(
        crc32(0L, Z_NULL, 0),
        output_data,
        static_cast<uInt>(output.size()));
    if (actual_crc != entry.crc) {
        error_message = QStringLiteral("CRC verification failed for %1").arg(entry.name);
        return std::nullopt;
    }
    return output;
}

} // namespace

namespace cosmo::detail {

std::optional<SkinArchiveContents> readSkinArchive(
    const QString &archive_path,
    QString &error_message) {
    QFileInfo archive_info(archive_path);
    if (!archive_info.exists() || !archive_info.isFile()) {
        error_message = QStringLiteral("The selected skin archive does not exist or is not a file.");
        return std::nullopt;
    }
    if (archive_info.size() < 0 || archive_info.size() > kMaxArchiveBytes) {
        error_message = QStringLiteral("The skin archive exceeds the 10 MiB compressed-size limit.");
        return std::nullopt;
    }

    QFile file(archive_path);
    if (!file.open(QIODevice::ReadOnly)) {
        error_message = QStringLiteral("Could not open the skin archive: %1").arg(file.errorString());
        return std::nullopt;
    }
    const QByteArray archive = file.readAll();
    if (archive.size() != archive_info.size()) {
        error_message = QStringLiteral("Could not read the complete skin archive.");
        return std::nullopt;
    }

    const auto eocd_offset = findEndOfCentralDirectory(archive);
    if (!eocd_offset) {
        error_message = QStringLiteral("The file is not a supported ZIP archive.");
        return std::nullopt;
    }
    if (readLe16(archive, *eocd_offset + 4) != 0U
        || readLe16(archive, *eocd_offset + 6) != 0U) {
        error_message = QStringLiteral("Multi-disk ZIP archives are not supported.");
        return std::nullopt;
    }

    const quint16 entries_on_disk = readLe16(archive, *eocd_offset + 8);
    const quint16 entry_count = readLe16(archive, *eocd_offset + 10);
    const quint32 central_size = readLe32(archive, *eocd_offset + 12);
    const quint32 central_offset = readLe32(archive, *eocd_offset + 16);
    if (entries_on_disk != entry_count || entry_count == 0U || entry_count > kMaxEntries
        || entry_count == 0xffffU || central_size == 0xffffffffU
        || central_offset == 0xffffffffU) {
        error_message = QStringLiteral("The ZIP entry count is invalid or exceeds the 64-entry limit.");
        return std::nullopt;
    }
    if (!rangeFits(archive.size(), central_offset, central_size)
        || static_cast<quint64>(central_offset) + central_size > static_cast<quint64>(*eocd_offset)) {
        error_message = QStringLiteral("The ZIP central directory points outside the archive.");
        return std::nullopt;
    }

    QList<ZipEntry> entries;
    QMap<QString, bool> names;
    QMap<QString, bool> case_folded_names;
    quint64 expanded_total = 0;
    quint64 offset = central_offset;
    const quint64 central_end = static_cast<quint64>(central_offset) + central_size;
    for (quint16 index = 0; index < entry_count; ++index) {
        if (!rangeFits(archive.size(), offset, 46U)
            || offset + 46U > central_end
            || readLe32(archive, static_cast<qsizetype>(offset)) != kCentralDirectorySignature) {
            error_message = QStringLiteral("The ZIP central directory is malformed.");
            return std::nullopt;
        }

        ZipEntry entry;
        entry.flags = readLe16(archive, static_cast<qsizetype>(offset + 8U));
        entry.compression = readLe16(archive, static_cast<qsizetype>(offset + 10U));
        entry.crc = readLe32(archive, static_cast<qsizetype>(offset + 16U));
        entry.compressed_size = readLe32(archive, static_cast<qsizetype>(offset + 20U));
        entry.uncompressed_size = readLe32(archive, static_cast<qsizetype>(offset + 24U));
        const quint16 name_size = readLe16(archive, static_cast<qsizetype>(offset + 28U));
        const quint16 extra_size = readLe16(archive, static_cast<qsizetype>(offset + 30U));
        const quint16 comment_size = readLe16(archive, static_cast<qsizetype>(offset + 32U));
        const quint16 start_disk = readLe16(archive, static_cast<qsizetype>(offset + 34U));
        entry.external_attributes = readLe32(archive, static_cast<qsizetype>(offset + 38U));
        entry.local_header_offset = readLe32(archive, static_cast<qsizetype>(offset + 42U));

        const quint64 name_offset = offset + 46U;
        const quint64 next_offset = name_offset + name_size + extra_size + comment_size;
        if (!rangeFits(archive.size(), name_offset, name_size) || next_offset > central_end) {
            error_message = QStringLiteral("A ZIP directory entry is truncated.");
            return std::nullopt;
        }
        entry.raw_name = archive.mid(
            static_cast<qsizetype>(name_offset),
            static_cast<qsizetype>(name_size));
        if (!isValidUtf8Name(entry.raw_name, entry.name)) {
            error_message = QStringLiteral("A ZIP entry has an invalid UTF-8 name.");
            return std::nullopt;
        }
        if (names.contains(entry.name)) {
            error_message = QStringLiteral("The archive contains a duplicate entry: %1").arg(entry.name);
            return std::nullopt;
        }
        const QString case_folded_name = entry.name.toCaseFolded();
        if (case_folded_names.contains(case_folded_name)) {
            error_message = QStringLiteral(
                "Archive entries must not differ only by letter case: %1").arg(entry.name);
            return std::nullopt;
        }
        names.insert(entry.name, true);
        case_folded_names.insert(case_folded_name, true);

        if (start_disk != 0U) {
            error_message = QStringLiteral("Multi-disk ZIP entries are not supported: %1").arg(entry.name);
            return std::nullopt;
        }

        constexpr quint16 kEncryptedFlags = 0x2041U;
        if ((entry.flags & kEncryptedFlags) != 0U) {
            error_message = QStringLiteral("Encrypted ZIP entries are not supported: %1").arg(entry.name);
            return std::nullopt;
        }
        constexpr quint16 kAllowedFlags = 0x080eU;
        if ((entry.flags & static_cast<quint16>(~kAllowedFlags)) != 0U
            || (entry.compression == kCompressionStored && (entry.flags & 0x0006U) != 0U)) {
            error_message = QStringLiteral("Unsupported ZIP options for %1").arg(entry.name);
            return std::nullopt;
        }
        if (entry.compression != kCompressionStored
            && entry.compression != kCompressionDeflated) {
            error_message = QStringLiteral("Unsupported ZIP compression for %1").arg(entry.name);
            return std::nullopt;
        }
        if (!validateEntryName(entry, error_message)) {
            return std::nullopt;
        }

        expanded_total += entry.uncompressed_size;
        if (expanded_total > kMaxExpandedBytes) {
            error_message = QStringLiteral("The archive exceeds the 32 MiB expanded-size limit.");
            return std::nullopt;
        }
        entries.append(entry);
        offset = next_offset;
    }
    if (offset != central_end) {
        error_message = QStringLiteral("The ZIP central-directory size is inconsistent.");
        return std::nullopt;
    }
    if (!names.contains(QStringLiteral("theme.json"))) {
        error_message = QStringLiteral("The archive does not contain a root theme.json file.");
        return std::nullopt;
    }

    SkinArchiveContents result;
    for (const ZipEntry &entry : entries) {
        auto contents = inflateEntry(archive, entry, central_offset, error_message);
        if (!contents) {
            return std::nullopt;
        }
        if (!entry.directory) {
            result.files.insert(entry.name, std::move(*contents));
        }
    }
    return result;
}

} // namespace cosmo::detail
