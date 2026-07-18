#include "gui/SkinLoader.h"

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>

#include <optional>

#include <zlib.h>

namespace {

struct ZipTestEntry {
    QByteArray name;
    QByteArray contents;
    quint32 external_attributes = 0;
    std::optional<quint32> declared_uncompressed_size;
    std::optional<quint32> crc_override;
    bool deflated = false;
};

void appendLe16(QByteArray &output, quint16 value) {
    output.append(static_cast<char>(value & 0xffU));
    output.append(static_cast<char>((value >> 8U) & 0xffU));
}

void appendLe32(QByteArray &output, quint32 value) {
    output.append(static_cast<char>(value & 0xffU));
    output.append(static_cast<char>((value >> 8U) & 0xffU));
    output.append(static_cast<char>((value >> 16U) & 0xffU));
    output.append(static_cast<char>((value >> 24U) & 0xffU));
}

QByteArray makeZip(const QList<ZipTestEntry> &entries) {
    struct CentralEntry {
        ZipTestEntry source;
        QByteArray payload;
        quint32 local_offset = 0;
        quint32 crc = 0;
        quint32 uncompressed_size = 0;
    };

    const auto deflateRaw = [](const QByteArray &contents) -> QByteArray {
        QByteArray compressed(static_cast<qsizetype>(compressBound(contents.size())), Qt::Uninitialized);
        z_stream stream{};
        stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(contents.constData()));
        stream.avail_in = static_cast<uInt>(contents.size());
        stream.next_out = reinterpret_cast<Bytef *>(compressed.data());
        stream.avail_out = static_cast<uInt>(compressed.size());
        REQUIRE(deflateInit2(
            &stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8,
            Z_DEFAULT_STRATEGY) == Z_OK);
        REQUIRE(deflate(&stream, Z_FINISH) == Z_STREAM_END);
        REQUIRE(deflateEnd(&stream) == Z_OK);
        compressed.resize(static_cast<qsizetype>(stream.total_out));
        return compressed;
    };

    QByteArray output;
    QList<CentralEntry> central_entries;
    for (const ZipTestEntry &entry : entries) {
        CentralEntry central;
        central.source = entry;
        central.local_offset = static_cast<quint32>(output.size());
        central.uncompressed_size = entry.declared_uncompressed_size.value_or(
            static_cast<quint32>(entry.contents.size()));
        central.payload = entry.deflated ? deflateRaw(entry.contents) : entry.contents;
        central.crc = entry.crc_override.value_or(static_cast<quint32>(crc32(
            crc32(0L, Z_NULL, 0),
            reinterpret_cast<const Bytef *>(entry.contents.constData()),
            static_cast<uInt>(entry.contents.size()))));

        appendLe32(output, 0x04034b50U);
        appendLe16(output, 20U);
        appendLe16(output, 0U);
        appendLe16(output, entry.deflated ? 8U : 0U);
        appendLe16(output, 0U);
        appendLe16(output, 0U);
        appendLe32(output, central.crc);
        appendLe32(output, static_cast<quint32>(central.payload.size()));
        appendLe32(output, central.uncompressed_size);
        appendLe16(output, static_cast<quint16>(entry.name.size()));
        appendLe16(output, 0U);
        output.append(entry.name);
        output.append(central.payload);
        central_entries.append(central);
    }

    const quint32 central_offset = static_cast<quint32>(output.size());
    for (const CentralEntry &entry : central_entries) {
        appendLe32(output, 0x02014b50U);
        appendLe16(output, 0x0314U);
        appendLe16(output, 20U);
        appendLe16(output, 0U);
        appendLe16(output, entry.source.deflated ? 8U : 0U);
        appendLe16(output, 0U);
        appendLe16(output, 0U);
        appendLe32(output, entry.crc);
        appendLe32(output, static_cast<quint32>(entry.payload.size()));
        appendLe32(output, entry.uncompressed_size);
        appendLe16(output, static_cast<quint16>(entry.source.name.size()));
        appendLe16(output, 0U);
        appendLe16(output, 0U);
        appendLe16(output, 0U);
        appendLe16(output, 0U);
        appendLe32(output, entry.source.external_attributes);
        appendLe32(output, entry.local_offset);
        output.append(entry.source.name);
    }

    const quint32 central_size = static_cast<quint32>(output.size()) - central_offset;
    appendLe32(output, 0x06054b50U);
    appendLe16(output, 0U);
    appendLe16(output, 0U);
    appendLe16(output, static_cast<quint16>(entries.size()));
    appendLe16(output, static_cast<quint16>(entries.size()));
    appendLe32(output, central_size);
    appendLe32(output, central_offset);
    appendLe16(output, 0U);
    return output;
}

QString writeArchive(
    const QTemporaryDir &temporary,
    const QString &filename,
    const QList<ZipTestEntry> &entries) {
    const QString path = QDir(temporary.path()).filePath(filename);
    QFile output(path);
    REQUIRE(output.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray archive = makeZip(entries);
    REQUIRE(output.write(archive) == archive.size());
    REQUIRE(output.flush());
    return path;
}

cosmo::SkinImportResult importThemeJson(const QByteArray &json) {
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    const QString archive = writeArchive(
        temporary,
        QStringLiteral("theme.cosmo"),
        {{QByteArrayLiteral("theme.json"), json}});
    return cosmo::SkinLoader::importArchiveDetailed(
        archive,
        QDir(temporary.path()).filePath(QStringLiteral("skins")));
}

QByteArray makePng(int width, int height) {
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(Qt::magenta);
    QByteArray bytes;
    QBuffer output(&bytes);
    REQUIRE(output.open(QIODevice::WriteOnly));
    REQUIRE(image.save(&output, "PNG"));
    return bytes;
}

} // namespace

TEST_CASE("SkinLoader imports and replaces a validated archive", "[gui][skin]") {
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    const QString archive_path = writeArchive(
        temporary,
        QStringLiteral("Night Skin.cosmo"),
        {{QByteArrayLiteral("theme.json"),
          QByteArrayLiteral(R"({"name":"Night","palette":{"border_default":"red"}})")}});
    const QString destination = QDir(temporary.path()).filePath(QStringLiteral("skins"));

    const auto imported = cosmo::SkinLoader::importArchiveDetailed(archive_path, destination);
    REQUIRE(imported.succeeded());
    REQUIRE(imported.theme->id == QStringLiteral("custom:Night_Skin"));
    REQUIRE(imported.theme->palette.border_default == QStringLiteral("#ff0000"));
    REQUIRE(QFileInfo::exists(QDir(destination).filePath(QStringLiteral("Night_Skin/theme.json"))));

    const auto collision = cosmo::SkinLoader::importArchiveDetailed(archive_path, destination);
    REQUIRE(collision.status == cosmo::SkinImportStatus::AlreadyExists);
    REQUIRE_FALSE(collision.theme.has_value());

    const QString updated_archive = writeArchive(
        temporary,
        QStringLiteral("Night Skin.cosmo"),
        {{QByteArrayLiteral("theme.json"), QByteArrayLiteral(R"({"name":"Updated"})")}});
    const auto replaced = cosmo::SkinLoader::importArchiveDetailed(
        updated_archive, destination, true);
    REQUIRE(replaced.succeeded());
    REQUIRE(replaced.theme->name == QStringLiteral("Updated"));
}

TEST_CASE("SkinLoader imports a standard deflated archive", "[gui][skin]") {
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    const QString archive = writeArchive(
        temporary,
        QStringLiteral("deflated.cosmo"),
        {{QByteArrayLiteral("theme.json"), QByteArrayLiteral(R"({"name":"Deflated"})"),
          0U, std::nullopt, std::nullopt, true}});
    const auto result = cosmo::SkinLoader::importArchiveDetailed(
        archive,
        QDir(temporary.path()).filePath(QStringLiteral("skins")));
    REQUIRE(result.succeeded());
    REQUIRE(result.theme->name == QStringLiteral("Deflated"));
}

TEST_CASE("SkinLoader treats case-only destination names as collisions", "[gui][skin][security]") {
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    const QString destination = QDir(temporary.path()).filePath(QStringLiteral("skins"));
    REQUIRE(QDir().mkpath(QDir(destination).filePath(QStringLiteral("Existing"))));
    QFile existing_theme(QDir(destination).filePath(QStringLiteral("Existing/theme.json")));
    REQUIRE(existing_theme.open(QIODevice::WriteOnly));
    REQUIRE(existing_theme.write(QByteArrayLiteral("{}")) == 2);
    existing_theme.close();

    const QString archive = writeArchive(
        temporary,
        QStringLiteral("existing.cosmo"),
        {{QByteArrayLiteral("theme.json"), QByteArrayLiteral("{}")}});
    const auto result = cosmo::SkinLoader::importArchiveDetailed(archive, destination);
    REQUIRE(result.status == cosmo::SkinImportStatus::AlreadyExists);
    REQUIRE(result.target_path.endsWith(QStringLiteral("/Existing")));
}

TEST_CASE("SkinLoader rejects unsafe ZIP structures", "[gui][skin][security]") {
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    const QString destination = QDir(temporary.path()).filePath(QStringLiteral("skins"));
    const QByteArray theme = QByteArrayLiteral(R"({"name":"Safe"})");

    SECTION("traversal path") {
        const QString archive = writeArchive(
            temporary, QStringLiteral("traversal.cosmo"),
            {{QByteArrayLiteral("theme.json"), theme},
             {QByteArrayLiteral("../preview.png"), QByteArrayLiteral("bad")}});
        const auto result = cosmo::SkinLoader::importArchiveDetailed(archive, destination);
        REQUIRE(result.status == cosmo::SkinImportStatus::Failed);
        REQUIRE(result.error_message.contains(QStringLiteral("traversal"), Qt::CaseInsensitive));
    }

    SECTION("backslash path") {
        const QString archive = writeArchive(
            temporary, QStringLiteral("backslash.cosmo"),
            {{QByteArrayLiteral("theme.json"), theme},
             {QByteArrayLiteral("textures\\evil.png"), QByteArrayLiteral("bad")}});
        const auto result = cosmo::SkinLoader::importArchiveDetailed(archive, destination);
        REQUIRE(result.status == cosmo::SkinImportStatus::Failed);
        REQUIRE(result.error_message.contains(QStringLiteral("backslash"), Qt::CaseInsensitive));
    }

    SECTION("symbolic link") {
        constexpr quint32 kUnixSymlinkMode = 0120777U << 16U;
        const QString archive = writeArchive(
            temporary, QStringLiteral("link.cosmo"),
            {{QByteArrayLiteral("theme.json"), theme},
             {QByteArrayLiteral("textures/link.png"), QByteArrayLiteral("theme.json"),
              kUnixSymlinkMode}});
        const auto result = cosmo::SkinLoader::importArchiveDetailed(archive, destination);
        REQUIRE(result.status == cosmo::SkinImportStatus::Failed);
        REQUIRE(result.error_message.contains(QStringLiteral("link"), Qt::CaseInsensitive));
    }

    SECTION("unsupported entry") {
        const QString archive = writeArchive(
            temporary, QStringLiteral("unsupported.cosmo"),
            {{QByteArrayLiteral("theme.json"), theme},
             {QByteArrayLiteral("script.js"), QByteArrayLiteral("alert(1)")}});
        const auto result = cosmo::SkinLoader::importArchiveDetailed(archive, destination);
        REQUIRE(result.status == cosmo::SkinImportStatus::Failed);
        REQUIRE(result.error_message.contains(QStringLiteral("unsupported"), Qt::CaseInsensitive));
    }

    SECTION("CRC mismatch") {
        const QString archive = writeArchive(
            temporary, QStringLiteral("crc.cosmo"),
            {{QByteArrayLiteral("theme.json"), theme, 0U, std::nullopt, 42U}});
        const auto result = cosmo::SkinLoader::importArchiveDetailed(archive, destination);
        REQUIRE(result.status == cosmo::SkinImportStatus::Failed);
        REQUIRE(result.error_message.contains(QStringLiteral("CRC"), Qt::CaseInsensitive));
    }

    SECTION("case-only archive entry collision") {
        const QString archive = writeArchive(
            temporary, QStringLiteral("case-collision.cosmo"),
            {{QByteArrayLiteral("theme.json"), theme},
             {QByteArrayLiteral("THEME.JSON"), theme}});
        const auto result = cosmo::SkinLoader::importArchiveDetailed(archive, destination);
        REQUIRE(result.status == cosmo::SkinImportStatus::Failed);
        REQUIRE(result.error_message.contains(QStringLiteral("letter case"), Qt::CaseInsensitive));
    }
}

TEST_CASE("SkinLoader enforces ZIP resource limits", "[gui][skin][security]") {
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    const QString destination = QDir(temporary.path()).filePath(QStringLiteral("skins"));

    SECTION("entry count") {
        QList<ZipTestEntry> entries;
        entries.append({QByteArrayLiteral("theme.json"), QByteArrayLiteral("{}")});
        for (int index = 0; index < 64; ++index) {
            entries.append({QStringLiteral("textures/%1.png").arg(index).toUtf8(), {}});
        }
        const QString archive = writeArchive(
            temporary, QStringLiteral("entries.cosmo"), entries);
        const auto result = cosmo::SkinLoader::importArchiveDetailed(archive, destination);
        REQUIRE(result.status == cosmo::SkinImportStatus::Failed);
        REQUIRE(result.error_message.contains(QStringLiteral("64-entry")));
    }

    SECTION("expanded size") {
        QList<ZipTestEntry> entries;
        entries.append({QByteArrayLiteral("theme.json"), QByteArrayLiteral("{}")});
        for (int index = 0; index < 5; ++index) {
            entries.append({QStringLiteral("textures/%1.png").arg(index).toUtf8(), {}, 0U,
                            8U * 1024U * 1024U});
        }
        const QString archive = writeArchive(
            temporary, QStringLiteral("expanded.cosmo"), entries);
        const auto result = cosmo::SkinLoader::importArchiveDetailed(archive, destination);
        REQUIRE(result.status == cosmo::SkinImportStatus::Failed);
        REQUIRE(result.error_message.contains(QStringLiteral("32 MiB")));
    }

    SECTION("theme.json size") {
        const QString archive = writeArchive(
            temporary, QStringLiteral("large-theme.cosmo"),
            {{QByteArrayLiteral("theme.json"), {}, 0U, 256U * 1024U + 1U}});
        const auto result = cosmo::SkinLoader::importArchiveDetailed(archive, destination);
        REQUIRE(result.status == cosmo::SkinImportStatus::Failed);
        REQUIRE(result.error_message.contains(QStringLiteral("size limit")));
    }

    SECTION("image entry size") {
        const QString archive = writeArchive(
            temporary, QStringLiteral("large-image.cosmo"),
            {{QByteArrayLiteral("theme.json"), QByteArrayLiteral("{}")},
             {QByteArrayLiteral("preview.png"), {}, 0U, 8U * 1024U * 1024U + 1U}});
        const auto result = cosmo::SkinLoader::importArchiveDetailed(archive, destination);
        REQUIRE(result.status == cosmo::SkinImportStatus::Failed);
        REQUIRE(result.error_message.contains(QStringLiteral("size limit")));
    }

    SECTION("decoded image dimensions") {
        const QString archive = writeArchive(
            temporary, QStringLiteral("wide-image.cosmo"),
            {{QByteArrayLiteral("theme.json"), QByteArrayLiteral("{}")},
             {QByteArrayLiteral("preview.png"), makePng(4097, 1)}});
        const auto result = cosmo::SkinLoader::importArchiveDetailed(archive, destination);
        REQUIRE(result.status == cosmo::SkinImportStatus::Failed);
        REQUIRE(result.error_message.contains(QStringLiteral("4096x4096")));
    }

    SECTION("compressed archive size") {
        const QString path = QDir(temporary.path()).filePath(QStringLiteral("too-large.cosmo"));
        QFile archive(path);
        REQUIRE(archive.open(QIODevice::WriteOnly));
        REQUIRE(archive.resize(10 * 1024 * 1024 + 1));
        archive.close();
        const auto result = cosmo::SkinLoader::importArchiveDetailed(path, destination);
        REQUIRE(result.status == cosmo::SkinImportStatus::Failed);
        REQUIRE(result.error_message.contains(QStringLiteral("10 MiB")));
    }

    SECTION("cumulative decoded image budget") {
        const QString archive = writeArchive(
            temporary, QStringLiteral("pixel-budget.cosmo"),
            {{QByteArrayLiteral("theme.json"), QByteArrayLiteral(
                R"({"textures":{"sidebar":"textures/one.png","toolbar":"textures/two.png"}})")},
             {QByteArrayLiteral("textures/one.png"), makePng(3300, 3300)},
             {QByteArrayLiteral("textures/two.png"), makePng(3300, 3300)}});
        const auto result = cosmo::SkinLoader::importArchiveDetailed(archive, destination);
        REQUIRE(result.status == cosmo::SkinImportStatus::Failed);
        REQUIRE(result.error_message.contains(QStringLiteral("20-megapixel")));
    }
}

TEST_CASE("SkinLoader rejects invalid custom theme values", "[gui][skin][security]") {
    SECTION("invalid QColor") {
        const auto result = importThemeJson(
            QByteArrayLiteral(R"({"palette":{"text_primary":"not-a-color"}})"));
        REQUIRE(result.status == cosmo::SkinImportStatus::Failed);
        REQUIRE(result.error_message.contains(QStringLiteral("QColor")));
    }

    SECTION("insufficient contrast") {
        const auto result = importThemeJson(
            QByteArrayLiteral(R"({"palette":{"text_primary":"#1f1f1f"}})"));
        REQUIRE(result.status == cosmo::SkinImportStatus::Failed);
        REQUIRE(result.error_message.contains(QStringLiteral("contrast"), Qt::CaseInsensitive));
    }

    SECTION("transparent foreground") {
        const auto result = importThemeJson(
            QByteArrayLiteral(R"({"palette":{"text_primary":"#00ffffff"}})"));
        REQUIRE(result.status == cosmo::SkinImportStatus::Failed);
        REQUIRE(result.error_message.contains(QStringLiteral("opaque"), Qt::CaseInsensitive));
    }

    SECTION("opacity outside range") {
        const auto result = importThemeJson(QByteArrayLiteral(R"({"texture_opacity":1.5})"));
        REQUIRE(result.status == cosmo::SkinImportStatus::Failed);
        REQUIRE(result.error_message.contains(QStringLiteral("between 0 and 1")));
    }

    SECTION("unsafe texture reference") {
        const auto result = importThemeJson(
            QByteArrayLiteral(R"({"textures":{"panel":"../outside.png"}})"));
        REQUIRE(result.status == cosmo::SkinImportStatus::Failed);
        REQUIRE(result.error_message.contains(QStringLiteral("unsafe"), Qt::CaseInsensitive));
    }

    SECTION("unsupported texture region") {
        const auto result = importThemeJson(
            QByteArrayLiteral(R"({"textures":{"unbounded-alias":"textures/panel.png"}})"));
        REQUIRE(result.status == cosmo::SkinImportStatus::Failed);
        REQUIRE(result.error_message.contains(QStringLiteral("region"), Qt::CaseInsensitive));
    }
}

TEST_CASE("SkinLoader rejects unreferenced texture payloads", "[gui][skin][security]") {
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    const QString archive = writeArchive(
        temporary,
        QStringLiteral("unused-texture.cosmo"),
        {{QByteArrayLiteral("theme.json"), QByteArrayLiteral("{}")},
         {QByteArrayLiteral("textures/unused.png"), makePng(1, 1)}});
    const auto result = cosmo::SkinLoader::importArchiveDetailed(
        archive,
        QDir(temporary.path()).filePath(QStringLiteral("skins")));
    REQUIRE(result.status == cosmo::SkinImportStatus::Failed);
    REQUIRE(result.error_message.contains(QStringLiteral("not referenced")));
}

TEST_CASE("SkinLoader trusted built-in themes remain loadable", "[gui][skin]") {
    const auto dark = cosmo::SkinLoader::loadBuiltin(QStringLiteral(":/skins/dark"));
    const auto light = cosmo::SkinLoader::loadBuiltin(QStringLiteral(":/skins/light"));
    REQUIRE(dark.has_value());
    REQUIRE(light.has_value());
}
