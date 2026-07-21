#include "gui/SkinLoader.h"
#include "gui/ThemeManager.h"

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QBuffer>
#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <barrier>
#include <cmath>
#include <cstddef>
#include <optional>
#include <thread>

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

QByteArray makePng(int width, int height, const QColor &fill = QColor(Qt::black)) {
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(fill);
    QByteArray bytes;
    QBuffer output(&bytes);
    REQUIRE(output.open(QIODevice::WriteOnly));
    REQUIRE(image.save(&output, "PNG"));
    return bytes;
}

void overwriteFile(const QString &path, const QByteArray &contents) {
    QFile output(path);
    REQUIRE(output.open(QIODevice::WriteOnly | QIODevice::Truncate));
    REQUIRE(output.write(contents) == contents.size());
    output.close();
}

double linearColorChannel(int channel) {
    const double value = static_cast<double>(channel) / 255.0;
    return value <= 0.04045
        ? value / 12.92
        : std::pow((value + 0.055) / 1.055, 2.4);
}

double colorLuminance(const QString &value) {
    const QColor color(value);
    REQUIRE(color.isValid());
    return 0.2126 * linearColorChannel(color.red())
        + 0.7152 * linearColorChannel(color.green())
        + 0.0722 * linearColorChannel(color.blue());
}

double colorContrast(const QString &first, const QString &second) {
    const double first_luminance = colorLuminance(first);
    const double second_luminance = colorLuminance(second);
    const double lighter = std::max(first_luminance, second_luminance);
    const double darker = std::min(first_luminance, second_luminance);
    return (lighter + 0.05) / (darker + 0.05);
}

std::array<double, 3> colorLab(const QString &value) {
    const QColor color(value);
    REQUIRE(color.isValid());
    const double red = linearColorChannel(color.red());
    const double green = linearColorChannel(color.green());
    const double blue = linearColorChannel(color.blue());
    const double x = (0.4124564 * red + 0.3575761 * green + 0.1804375 * blue)
        / 0.95047;
    const double y = 0.2126729 * red + 0.7151522 * green + 0.0721750 * blue;
    const double z = (0.0193339 * red + 0.1191920 * green + 0.9503041 * blue)
        / 1.08883;
    const auto pivot = [](double component) {
        constexpr double kEpsilon = 216.0 / 24389.0;
        constexpr double kKappa = 24389.0 / 27.0;
        return component > kEpsilon
            ? std::cbrt(component)
            : (kKappa * component + 16.0) / 116.0;
    };
    const double fx = pivot(x);
    const double fy = pivot(y);
    const double fz = pivot(z);
    return {116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz)};
}

double colorDistance(const QString &first, const QString &second) {
    const auto first_lab = colorLab(first);
    const auto second_lab = colorLab(second);
    return std::sqrt(
        std::pow(first_lab[0] - second_lab[0], 2.0)
        + std::pow(first_lab[1] - second_lab[1], 2.0)
        + std::pow(first_lab[2] - second_lab[2], 2.0));
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

TEST_CASE("SkinLoader serializes concurrent replacement imports", "[gui][skin][concurrency]") {
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    const QString destination = QDir(temporary.path()).filePath(QStringLiteral("skins"));

    const QString first_archive = writeArchive(
        temporary,
        QStringLiteral("Concurrent Skin.cosmo"),
        {{QByteArrayLiteral("theme.json"), QByteArrayLiteral(R"({"name":"Initial"})")}});
    const auto initial = cosmo::SkinLoader::importArchiveDetailed(first_archive, destination);
    REQUIRE(initial.succeeded());

    // These different filenames normalize to the same installed directory.
    writeArchive(
        temporary,
        QStringLiteral("Concurrent Skin.cosmo"),
        {{QByteArrayLiteral("theme.json"), QByteArrayLiteral(R"({"name":"First"})")}});
    const QString second_archive = writeArchive(
        temporary,
        QStringLiteral("Concurrent@Skin.cosmo"),
        {{QByteArrayLiteral("theme.json"), QByteArrayLiteral(R"({"name":"Second"})")}});

    std::array<cosmo::SkinImportResult, 2> results;
    std::barrier start_line(static_cast<std::ptrdiff_t>(3));
    std::jthread first([&]() {
        start_line.arrive_and_wait();
        results[0] = cosmo::SkinLoader::importArchiveDetailed(
            first_archive, destination, true);
    });
    std::jthread second([&]() {
        start_line.arrive_and_wait();
        results[1] = cosmo::SkinLoader::importArchiveDetailed(
            second_archive, destination, true);
    });
    start_line.arrive_and_wait();
    first.join();
    second.join();

    REQUIRE(results[0].succeeded());
    REQUIRE(results[1].succeeded());
    REQUIRE(results[0].target_path == results[1].target_path);

    const auto installed = cosmo::SkinLoader::loadFromDirectory(results[0].target_path);
    REQUIRE(installed.has_value());
    REQUIRE((installed->name == QStringLiteral("First")
             || installed->name == QStringLiteral("Second")));

    const QStringList transaction_artifacts = QDir(destination).entryList(
        {QStringLiteral(".cosmoskin-import-*"), QStringLiteral(".cosmoskin-backup-*")},
        QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot);
    REQUIRE(transaction_artifacts.isEmpty());
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
                R"({"textures":{"panel":"textures/panel.png"}})")},
             {QByteArrayLiteral("preview.png"), makePng(3300, 3300)},
             {QByteArrayLiteral("textures/panel.png"), makePng(3300, 3300)}});
        const auto result = cosmo::SkinLoader::importArchiveDetailed(archive, destination);
        REQUIRE(result.status == cosmo::SkinImportStatus::Failed);
        REQUIRE(result.error_message.contains(QStringLiteral("20-megapixel")));
    }
}

TEST_CASE("SkinLoader revalidates installed skin images", "[gui][skin][security]") {
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    const QString destination = QDir(temporary.path()).filePath(QStringLiteral("skins"));
    const QByteArray theme = QByteArrayLiteral(
        R"({"textures":{"panel":"textures/panel.png"}})");
    const QString archive = writeArchive(
        temporary,
        QStringLiteral("installed.cosmo"),
        {{QByteArrayLiteral("theme.json"), theme},
         {QByteArrayLiteral("preview.png"), makePng(1, 1)},
         {QByteArrayLiteral("textures/panel.png"), makePng(1, 1)}});
    const auto imported = cosmo::SkinLoader::importArchiveDetailed(archive, destination);
    REQUIRE(imported.succeeded());
    const QString skin_dir = imported.target_path;
    const QString preview_path = QDir(skin_dir).filePath(QStringLiteral("preview.png"));
    const QString panel_path = QDir(skin_dir).filePath(QStringLiteral("textures/panel.png"));

    SECTION("rejects a replaced non-PNG texture") {
        overwriteFile(panel_path, QByteArrayLiteral("not a PNG"));
        REQUIRE_FALSE(cosmo::SkinLoader::loadFromDirectory(skin_dir).has_value());
    }

    SECTION("rejects an oversized encoded image") {
        QFile panel(panel_path);
        REQUIRE(panel.open(QIODevice::ReadWrite));
        REQUIRE(panel.resize(8 * 1024 * 1024 + 1));
        panel.close();
        REQUIRE_FALSE(cosmo::SkinLoader::loadFromDirectory(skin_dir).has_value());
    }

    SECTION("rejects excessive decoded dimensions") {
        overwriteFile(panel_path, makePng(4097, 1));
        REQUIRE_FALSE(cosmo::SkinLoader::loadFromDirectory(skin_dir).has_value());
    }

    SECTION("rejects a replacement that erases panel contrast") {
        overwriteFile(panel_path, makePng(1, 1, QColor(Qt::white)));
        REQUIRE_FALSE(cosmo::SkinLoader::loadFromDirectory(skin_dir).has_value());
    }

    SECTION("rejects an excessive cumulative decoded-image budget") {
        overwriteFile(preview_path, makePng(3300, 3300));
        overwriteFile(panel_path, makePng(3300, 3300));
        REQUIRE_FALSE(cosmo::SkinLoader::loadFromDirectory(skin_dir).has_value());
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

    SECTION("insufficient selected-state contrast") {
        const auto result = importThemeJson(
            QByteArrayLiteral(R"({"palette":{"select_bg":"#202020"}})"));
        REQUIRE(result.status == cosmo::SkinImportStatus::Failed);
        REQUIRE(result.error_message.contains(QStringLiteral("select_bg")));
        REQUIRE(result.error_message.contains(QStringLiteral("contrast"), Qt::CaseInsensitive));
    }

    SECTION("insufficient trace contrast") {
        const auto result = importThemeJson(QByteArrayLiteral(
            R"({"palette":{"trace_colors":["#202020","#202020","#202020","#202020","#202020","#202020","#202020","#202020","#202020"]}})"));
        REQUIRE(result.status == cosmo::SkinImportStatus::Failed);
        REQUIRE(result.error_message.contains(QStringLiteral("trace_colors[0]")));
        REQUIRE(result.error_message.contains(QStringLiteral("contrast"), Qt::CaseInsensitive));
    }

    SECTION("duplicate trace colors") {
        const auto result = importThemeJson(QByteArrayLiteral(
            R"({"palette":{"trace_colors":["#5b9bd5","#5b9bd5","#f0b429","#c084fc","#ff6b6b","#ff9f6b","#f472b6","#a3e635","#f8de22"]}})"));
        REQUIRE(result.status == cosmo::SkinImportStatus::Failed);
        REQUIRE(result.error_message.contains(QStringLiteral("trace_colors[0]")));
        REQUIRE(result.error_message.contains(QStringLiteral("trace_colors[1]")));
        REQUIRE(result.error_message.contains(
            QStringLiteral("indistinguishable"), Qt::CaseInsensitive));
    }

    SECTION("perceptually indistinguishable trace colors") {
        const auto result = importThemeJson(QByteArrayLiteral(
            R"({"palette":{"trace_colors":["#5b9bd5","#5c9cd6","#f0b429","#c084fc","#ff6b6b","#ff9f6b","#f472b6","#a3e635","#f8de22"]}})"));
        REQUIRE(result.status == cosmo::SkinImportStatus::Failed);
        REQUIRE(result.error_message.contains(QStringLiteral("CIE76")));
        REQUIRE(result.error_message.contains(QStringLiteral("10.0")));
    }

    SECTION("trace palette has the required size") {
        const auto result = importThemeJson(
            QByteArrayLiteral(R"({"palette":{"trace_colors":["#ffffff"]}})"));
        REQUIRE(result.status == cosmo::SkinImportStatus::Failed);
        REQUIRE(result.error_message.contains(QStringLiteral("exactly 9")));
    }

    SECTION("placeholder text must contrast with input backgrounds") {
        const auto result = importThemeJson(
            QByteArrayLiteral(R"({"palette":{"bg_input":"#6b6b6b"}})"));
        REQUIRE(result.status == cosmo::SkinImportStatus::Failed);
        REQUIRE(result.error_message.contains(QStringLiteral("text_dim")));
        REQUIRE(result.error_message.contains(QStringLiteral("bg_input")));
    }

    SECTION("disabled text must contrast with button backgrounds") {
        const auto result = importThemeJson(
            QByteArrayLiteral(R"({"palette":{"bg_button":"#6b6b6b"}})"));
        REQUIRE(result.status == cosmo::SkinImportStatus::Failed);
        REQUIRE(result.error_message.contains(QStringLiteral("text_muted")));
        REQUIRE(result.error_message.contains(QStringLiteral("bg_button")));
    }

    SECTION("links must contrast with panel backgrounds") {
        const auto result = importThemeJson(
            QByteArrayLiteral(R"({"palette":{"accent_link":"#8b8b8b"}})"));
        REQUIRE(result.status == cosmo::SkinImportStatus::Failed);
        REQUIRE(result.error_message.contains(QStringLiteral("accent_link")));
        REQUIRE(result.error_message.contains(QStringLiteral("bg_panel")));
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

TEST_CASE("SkinLoader accepts recognized legacy texture regions", "[gui][skin][compatibility]") {
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    const QString destination = QDir(temporary.path()).filePath(QStringLiteral("skins"));
    const std::array<QString, 4> legacy_regions = {
        QStringLiteral("sidebar"),
        QStringLiteral("toolbar"),
        QStringLiteral("chart_bg"),
        QStringLiteral("settings_bg"),
    };

    for (const QString &region : legacy_regions) {
        CAPTURE(region);
        const QString texture_path = QStringLiteral("textures/%1.png").arg(region);
        const QByteArray theme = QStringLiteral(
            R"({"textures":{"%1":"%2"}})")
                                     .arg(region, texture_path)
                                     .toUtf8();
        const QString archive = writeArchive(
            temporary,
            region + QStringLiteral(".cosmo"),
            {{QByteArrayLiteral("theme.json"), theme},
             {texture_path.toUtf8(), makePng(1, 1)}});
        const auto result = cosmo::SkinLoader::importArchiveDetailed(archive, destination);
        REQUIRE(result.succeeded());
        REQUIRE(result.theme->textures.paths.contains(region));
    }
}

TEST_CASE("SkinLoader caps custom background texture opacity", "[gui][skin][accessibility]") {
    const auto result = importThemeJson(QByteArrayLiteral(R"({"texture_opacity":0.95})"));
    REQUIRE(result.succeeded());
    REQUIRE(result.theme->textures.opacity == 0.20);
}

TEST_CASE("SkinLoader validates rendered panel texture contrast", "[gui][skin][accessibility]") {
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    const QString destination = QDir(temporary.path()).filePath(QStringLiteral("skins"));

    SECTION("accepts a compatible texture") {
        const QString archive = writeArchive(
            temporary,
            QStringLiteral("safe-panel.cosmo"),
            {{QByteArrayLiteral("theme.json"), QByteArrayLiteral(
                R"({"textures":{"panel":"textures/panel.png"},"texture_opacity":0.2})")},
             {QByteArrayLiteral("textures/panel.png"), makePng(1, 1, QColor(Qt::black))}});
        const auto result = cosmo::SkinLoader::importArchiveDetailed(archive, destination);
        REQUIRE(result.succeeded());
    }

    SECTION("rejects a hostile solid texture") {
        const QString archive = writeArchive(
            temporary,
            QStringLiteral("hostile-panel.cosmo"),
            {{QByteArrayLiteral("theme.json"), QByteArrayLiteral(
                R"({"textures":{"panel":"textures/panel.png"},"texture_opacity":0.2})")},
             {QByteArrayLiteral("textures/panel.png"), makePng(1, 1, QColor(Qt::white))}});
        const auto result = cosmo::SkinLoader::importArchiveDetailed(archive, destination);
        REQUIRE(result.status == cosmo::SkinImportStatus::Failed);
        REQUIRE(result.error_message.contains(QStringLiteral("Panel texture")));
        REQUIRE(result.error_message.contains(QStringLiteral("contrast"), Qt::CaseInsensitive));
        REQUIRE(result.error_message.contains(QStringLiteral("pixel"), Qt::CaseInsensitive));
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

    for (const auto *theme : {&*dark, &*light}) {
        const auto &palette = theme->palette;
        REQUIRE(colorContrast(palette.text_dim, palette.bg_input) >= 4.5);
        REQUIRE(colorContrast(palette.text_muted, palette.bg_input) >= 4.5);
        REQUIRE(colorContrast(palette.text_muted, palette.bg_button) >= 4.5);
        for (const QString &background : {
                 palette.bg_base, palette.bg_dark, palette.bg_panel, palette.bg_input}) {
            REQUIRE(colorContrast(palette.accent_link, background) >= 4.5);
        }

        QSet<QString> unique_colors;
        for (const QString &trace_color : theme->palette.trace_colors) {
            const QColor color(trace_color);
            REQUIRE(color.isValid());
            REQUIRE(color.alpha() == 255);
            REQUIRE(colorContrast(trace_color, theme->palette.bg_base) >= 3.0);
            REQUIRE(colorContrast(trace_color, theme->palette.bg_panel) >= 3.0);
            unique_colors.insert(trace_color);
        }
        REQUIRE(unique_colors.size() == 9);
        for (std::size_t first = 0; first < palette.trace_colors.size(); ++first) {
            for (std::size_t second = first + 1;
                 second < palette.trace_colors.size(); ++second) {
                REQUIRE(colorDistance(
                    palette.trace_colors[first], palette.trace_colors[second]) >= 10.0);
            }
        }
    }
}

TEST_CASE("SkinLoader supplies distinct trace colors to legacy skins", "[gui][skin][accessibility]") {
    for (const QString &resource : {
             QStringLiteral(":/skins/dark/theme.json"),
             QStringLiteral(":/skins/light/theme.json")}) {
        CAPTURE(resource);
        QFile theme_file(resource);
        REQUIRE(theme_file.open(QIODevice::ReadOnly));
        QJsonDocument document = QJsonDocument::fromJson(theme_file.readAll());
        REQUIRE(document.isObject());
        QJsonObject root = document.object();
        QJsonObject source_palette = root.value(QStringLiteral("palette")).toObject();
        source_palette.remove(QStringLiteral("trace_colors"));
        root.insert(QStringLiteral("palette"), source_palette);

        const auto result = importThemeJson(
            QJsonDocument(root).toJson(QJsonDocument::Compact));
        REQUIRE(result.succeeded());
        QSet<QString> unique_colors;
        const auto &trace_colors = result.theme->palette.trace_colors;
        for (const QString &trace_color : trace_colors) {
            REQUIRE(colorContrast(trace_color, result.theme->palette.bg_base) >= 3.0);
            REQUIRE(colorContrast(trace_color, result.theme->palette.bg_panel) >= 3.0);
            unique_colors.insert(trace_color);
        }
        REQUIRE(unique_colors.size() == 9);
        for (std::size_t first = 0; first < trace_colors.size(); ++first) {
            for (std::size_t second = first + 1;
                 second < trace_colors.size(); ++second) {
                REQUIRE(colorDistance(trace_colors[first], trace_colors[second]) >= 10.0);
            }
        }
    }
}

TEST_CASE("ThemeManager generates complete supported global styles", "[gui][theme]") {
    const QString qss = cosmo::ThemeManager::generateQss(cosmo::ColorPalette{});
    REQUIRE(qss.contains(QStringLiteral("QMenu")));
    REQUIRE(qss.contains(QStringLiteral("QStatusBar")));
    REQUIRE(qss.contains(QStringLiteral("QMessageBox")));
    REQUIRE(qss.contains(QStringLiteral("QProgressBar")));
    REQUIRE_FALSE(qss.contains(QStringLiteral("@")));
    REQUIRE_FALSE(qss.contains(QStringLiteral("outline")));
    REQUIRE_FALSE(qss.contains(QStringLiteral("focus-visible")));
    REQUIRE_FALSE(qss.contains(QStringLiteral("line-height")));
    REQUIRE_FALSE(qss.contains(QStringLiteral("Roboto Mono")));
}
