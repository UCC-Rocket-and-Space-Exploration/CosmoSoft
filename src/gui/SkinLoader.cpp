#include "gui/SkinLoader.h"

#include "SkinArchive.h"

#include <QBuffer>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>
#include <QUuid>

#include <array>
#include <cmath>
#include <tuple>

using namespace cosmo;

// ─── JSON helpers ─────────────────────────────────────────────────────────────

namespace {

constexpr qsizetype kMaxThemeBytes = 256 * 1024;
constexpr qsizetype kMaxImageBytes = 8 * 1024 * 1024;
constexpr int kMaxImageDimension = 4096;
constexpr quint64 kMaxDecodedImagePixels = 20ULL * 1024ULL * 1024ULL;

void assignError(QString *error_message, const QString &message) {
    if (error_message) {
        *error_message = message;
    }
}

bool readOptionalString(
    const QJsonObject &object,
    const QString &key,
    QString &value,
    int maximum_length,
    QString *error_message) {
    if (!object.contains(key)) {
        return true;
    }
    if (!object.value(key).isString()) {
        assignError(error_message, QStringLiteral("theme.json field '%1' must be a string.").arg(key));
        return false;
    }
    const QString candidate = object.value(key).toString().trimmed();
    if (candidate.isEmpty() || candidate.size() > maximum_length) {
        assignError(
            error_message,
            QStringLiteral("theme.json field '%1' must contain 1 to %2 characters.")
                .arg(key)
                .arg(maximum_length));
        return false;
    }
    value = candidate;
    return true;
}

QString canonicalColor(const QColor &color) {
    return color.alpha() == 255
        ? color.name(QColor::HexRgb)
        : color.name(QColor::HexArgb);
}

double linearChannel(double channel) {
    channel /= 255.0;
    return channel <= 0.04045
        ? channel / 12.92
        : std::pow((channel + 0.055) / 1.055, 2.4);
}

double relativeLuminance(const QColor &color) {
    return 0.2126 * linearChannel(color.red())
        + 0.7152 * linearChannel(color.green())
        + 0.0722 * linearChannel(color.blue());
}

double contrastRatio(const QString &foreground, const QString &background) {
    const double foreground_luminance = relativeLuminance(QColor(foreground));
    const double background_luminance = relativeLuminance(QColor(background));
    const double lighter = std::max(foreground_luminance, background_luminance);
    const double darker = std::min(foreground_luminance, background_luminance);
    return (lighter + 0.05) / (darker + 0.05);
}

bool requireContrast(
    const QString &foreground_key,
    const QString &foreground,
    const QString &background_key,
    const QString &background,
    double minimum,
    QString *error_message) {
    const double ratio = contrastRatio(foreground, background);
    if (ratio + 0.001 >= minimum) {
        return true;
    }
    assignError(
        error_message,
        QStringLiteral("Palette colors '%1' and '%2' have %3:1 contrast; at least %4:1 is required.")
            .arg(foreground_key, background_key)
            .arg(ratio, 0, 'f', 2)
            .arg(minimum, 0, 'f', 1));
    return false;
}

bool validatePaletteContrast(const ColorPalette &palette, QString *error_message) {
    const std::array<std::pair<QString, QString>, 5> primary_backgrounds{{
        {QStringLiteral("bg_base"), palette.bg_base},
        {QStringLiteral("bg_dark"), palette.bg_dark},
        {QStringLiteral("bg_panel"), palette.bg_panel},
        {QStringLiteral("bg_input"), palette.bg_input},
        {QStringLiteral("bg_button"), palette.bg_button},
    }};
    for (const auto &[key, color] : primary_backgrounds) {
        if (!requireContrast(
                QStringLiteral("text_primary"), palette.text_primary,
                key, color, 4.5, error_message)) {
            return false;
        }
    }

    const std::array<std::pair<QString, QString>, 3> secondary_text{{
        {QStringLiteral("text_mid"), palette.text_mid},
        {QStringLiteral("text_muted"), palette.text_muted},
        {QStringLiteral("text_dim"), palette.text_dim},
    }};
    for (const auto &[key, color] : secondary_text) {
        if (!requireContrast(key, color, QStringLiteral("bg_base"), palette.bg_base,
                             4.5, error_message)) {
            return false;
        }
        for (const auto &[background_key, background] : primary_backgrounds) {
            if (background_key == QStringLiteral("bg_base")) {
                continue;
            }
            if (!requireContrast(key, color, background_key, background,
                                 3.0, error_message)) {
                return false;
            }
        }
    }

    const std::array<std::tuple<QString, QString, QString, QString>, 9> graphics{{
        {QStringLiteral("accent_link"), palette.accent_link,
         QStringLiteral("bg_base"), palette.bg_base},
        {QStringLiteral("focus_ring"), palette.focus_ring,
         QStringLiteral("bg_base"), palette.bg_base},
        {QStringLiteral("success"), palette.success,
         QStringLiteral("success_bg"), palette.success_bg},
        {QStringLiteral("warning"), palette.warning,
         QStringLiteral("warning_bg"), palette.warning_bg},
        {QStringLiteral("info"), palette.info,
         QStringLiteral("info_bg"), palette.info_bg},
        {QStringLiteral("success"), palette.success,
         QStringLiteral("bg_dark"), palette.bg_dark},
        {QStringLiteral("warning"), palette.warning,
         QStringLiteral("bg_dark"), palette.bg_dark},
        {QStringLiteral("info"), palette.info,
         QStringLiteral("bg_dark"), palette.bg_dark},
        {QStringLiteral("danger"), palette.danger,
         QStringLiteral("bg_base"), palette.bg_base},
    }};
    for (const auto &[foreground_key, foreground, background_key, background] : graphics) {
        if (!requireContrast(foreground_key, foreground, background_key, background,
                             3.0, error_message)) {
            return false;
        }
    }
    return true;
}

bool validatePng(
    const QByteArray &bytes,
    const QString &name,
    quint64 &decoded_pixels,
    QString &error_message) {
    static const QByteArray png_signature = QByteArray::fromHex("89504e470d0a1a0a");
    if (bytes.size() > kMaxImageBytes || !bytes.startsWith(png_signature)) {
        error_message = QStringLiteral("%1 is not a valid bounded PNG image.").arg(name);
        return false;
    }

    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly)) {
        error_message = QStringLiteral("Could not inspect image %1.").arg(name);
        return false;
    }
    QImageReader reader(&buffer, "PNG");
    reader.setDecideFormatFromContent(true);
    const QSize size = reader.size();
    if (!reader.canRead() || !size.isValid()
        || size.width() > kMaxImageDimension || size.height() > kMaxImageDimension) {
        error_message = QStringLiteral("%1 must be a valid PNG no larger than 4096x4096 pixels.").arg(name);
        return false;
    }
    const QImage decoded = reader.read();
    if (decoded.isNull() || decoded.width() > kMaxImageDimension
        || decoded.height() > kMaxImageDimension) {
        error_message = QStringLiteral("Could not safely decode PNG image %1.").arg(name);
        return false;
    }
    decoded_pixels = static_cast<quint64>(decoded.width())
        * static_cast<quint64>(decoded.height());
    return true;
}

bool writeStagedFile(
    const QString &root,
    const QString &relative_path,
    const QByteArray &contents,
    QString &error_message) {
    const QString output_path = QDir(root).filePath(relative_path);
    if (!QDir().mkpath(QFileInfo(output_path).path())) {
        error_message = QStringLiteral("Could not create the staging directory for %1.").arg(relative_path);
        return false;
    }
    QSaveFile output(output_path);
    if (!output.open(QIODevice::WriteOnly) || output.write(contents) != contents.size()
        || !output.commit()) {
        error_message = QStringLiteral("Could not stage %1: %2").arg(relative_path, output.errorString());
        return false;
    }
    return true;
}

bool isSafeTexturePath(const QString &relative_path) {
    static const QRegularExpression pattern(
        QStringLiteral(R"(^textures/[A-Za-z0-9][A-Za-z0-9._-]{0,127}\.png$)"));
    return pattern.match(relative_path).hasMatch();
}

bool isPathInside(const QString &base_path, const QString &candidate_path) {
    const QString canonical_base = QFileInfo(base_path).canonicalFilePath();
    const QString canonical_candidate = QFileInfo(candidate_path).canonicalFilePath();
    if (canonical_base.isEmpty() || canonical_candidate.isEmpty()) {
        return false;
    }
    const QString relative = QDir(canonical_base).relativeFilePath(canonical_candidate);
    return !QDir::isAbsolutePath(relative)
        && relative != QStringLiteral("..")
        && !relative.startsWith(QStringLiteral("../"));
}

} // namespace

// ─── parseThemeJson ───────────────────────────────────────────────────────────

std::optional<CosmoTheme> SkinLoader::parseThemeJson(const QByteArray &json,
                                                     const QString &base_path,
                                                     QString *error_message,
                                                     bool enforce_contrast)
{
    if (json.isEmpty() || json.size() > kMaxThemeBytes) {
        assignError(error_message, QStringLiteral("theme.json must contain 1 byte to 256 KiB."));
        return std::nullopt;
    }

    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(json, &err);
    if (doc.isNull() || !doc.isObject()) {
        assignError(
            error_message,
            QStringLiteral("Invalid theme.json at byte %1: %2")
                .arg(err.offset)
                .arg(err.errorString()));
        return std::nullopt;
    }

    const auto root = doc.object();

    CosmoTheme theme;
    theme.name = QStringLiteral("Custom");
    theme.author = QStringLiteral("Unknown");
    theme.version = QStringLiteral("1.0");
    if (!readOptionalString(root, QStringLiteral("name"), theme.name, 80, error_message)
        || !readOptionalString(root, QStringLiteral("author"), theme.author, 120, error_message)
        || !readOptionalString(root, QStringLiteral("version"), theme.version, 32, error_message)) {
        return std::nullopt;
    }

    const auto preview_file = base_path + QStringLiteral("/preview.png");
    if (QFile::exists(preview_file)) {
        const QFileInfo preview_info(preview_file);
        if (!base_path.startsWith(QStringLiteral(":/"))
            && (!preview_info.isFile() || preview_info.isSymLink()
                || !isPathInside(base_path, preview_file))) {
            assignError(error_message, QStringLiteral("preview.png must be a regular file inside the skin."));
            return std::nullopt;
        }
        theme.preview_path = preview_file;
    }

    // Palette
    if (root.contains(QStringLiteral("palette"))
        && !root.value(QStringLiteral("palette")).isObject()) {
        assignError(error_message, QStringLiteral("theme.json field 'palette' must be an object."));
        return std::nullopt;
    }
    if (root.contains(QStringLiteral("palette"))) {
        const auto pal = root[QStringLiteral("palette")].toObject();
        ColorPalette &p = theme.palette;

        struct PaletteField {
            const char *key;
            QString ColorPalette::*value;
        };
        static constexpr std::array fields{
            PaletteField{"bg_base", &ColorPalette::bg_base},
            PaletteField{"bg_dark", &ColorPalette::bg_dark},
            PaletteField{"bg_panel", &ColorPalette::bg_panel},
            PaletteField{"bg_input", &ColorPalette::bg_input},
            PaletteField{"bg_button", &ColorPalette::bg_button},
            PaletteField{"text_primary", &ColorPalette::text_primary},
            PaletteField{"text_mid", &ColorPalette::text_mid},
            PaletteField{"text_muted", &ColorPalette::text_muted},
            PaletteField{"text_dim", &ColorPalette::text_dim},
            PaletteField{"border_default", &ColorPalette::border_default},
            PaletteField{"border_light", &ColorPalette::border_light},
            PaletteField{"border_subtle", &ColorPalette::border_subtle},
            PaletteField{"border_panel", &ColorPalette::border_panel},
            PaletteField{"btn_hover", &ColorPalette::btn_hover},
            PaletteField{"btn_pressed", &ColorPalette::btn_pressed},
            PaletteField{"accent_link", &ColorPalette::accent_link},
            PaletteField{"accent_checkbox", &ColorPalette::accent_checkbox},
            PaletteField{"accent_checkbox_border", &ColorPalette::accent_checkbox_border},
            PaletteField{"danger", &ColorPalette::danger},
            PaletteField{"error", &ColorPalette::error},
            PaletteField{"select_bg", &ColorPalette::select_bg},
            PaletteField{"success", &ColorPalette::success},
            PaletteField{"success_bg", &ColorPalette::success_bg},
            PaletteField{"warning", &ColorPalette::warning},
            PaletteField{"warning_bg", &ColorPalette::warning_bg},
            PaletteField{"info", &ColorPalette::info},
            PaletteField{"info_bg", &ColorPalette::info_bg},
            PaletteField{"focus_ring", &ColorPalette::focus_ring},
            PaletteField{"focus_ring_offset", &ColorPalette::focus_ring_offset},
        };
        for (const auto &field : fields) {
            const QString key = QString::fromLatin1(field.key);
            if (!pal.contains(key)) {
                continue;
            }
            if (!pal.value(key).isString()) {
                assignError(
                    error_message,
                    QStringLiteral("Palette field '%1' must be a color string.").arg(key));
                return std::nullopt;
            }
            const QColor color(pal.value(key).toString().trimmed());
            if (!color.isValid()) {
                assignError(
                    error_message,
                    QStringLiteral("Palette field '%1' is not a valid QColor value.").arg(key));
                return std::nullopt;
            }
            const bool opaque_foreground = key.startsWith(QStringLiteral("text_"))
                || key == QStringLiteral("accent_link")
                || key == QStringLiteral("accent_checkbox")
                || key == QStringLiteral("accent_checkbox_border")
                || key == QStringLiteral("danger")
                || key == QStringLiteral("error")
                || key == QStringLiteral("success")
                || key == QStringLiteral("warning")
                || key == QStringLiteral("info")
                || key == QStringLiteral("focus_ring");
            if (opaque_foreground && color.alpha() != 255) {
                assignError(
                    error_message,
                    QStringLiteral("Palette foreground '%1' must be opaque for reliable contrast.").arg(key));
                return std::nullopt;
            }
            p.*(field.value) = canonicalColor(color);
        }
    }

    const std::array<std::pair<QString, QString>, 8> backgrounds{{
        {QStringLiteral("bg_base"), theme.palette.bg_base},
        {QStringLiteral("bg_dark"), theme.palette.bg_dark},
        {QStringLiteral("bg_panel"), theme.palette.bg_panel},
        {QStringLiteral("bg_input"), theme.palette.bg_input},
        {QStringLiteral("bg_button"), theme.palette.bg_button},
        {QStringLiteral("success_bg"), theme.palette.success_bg},
        {QStringLiteral("warning_bg"), theme.palette.warning_bg},
        {QStringLiteral("info_bg"), theme.palette.info_bg},
    }};
    for (const auto &[key, value] : backgrounds) {
        if (QColor(value).alpha() != 255) {
            assignError(error_message, QStringLiteral("Palette background '%1' must be opaque.").arg(key));
            return std::nullopt;
        }
    }
    if (enforce_contrast && !validatePaletteContrast(theme.palette, error_message)) {
        return std::nullopt;
    }

    // Textures
    if (root.contains(QStringLiteral("textures"))
        && !root.value(QStringLiteral("textures")).isObject()) {
        assignError(error_message, QStringLiteral("theme.json field 'textures' must be an object."));
        return std::nullopt;
    }
    if (root.contains(QStringLiteral("textures"))) {
        const auto tex = root[QStringLiteral("textures")].toObject();
        static const QSet<QString> allowed_regions = {
            QStringLiteral("sidebar"),
            QStringLiteral("toolbar"),
            QStringLiteral("panel"),
            QStringLiteral("chart_bg"),
            QStringLiteral("settings_bg"),
        };
        QSet<QString> referenced_paths;
        for (auto it = tex.begin(); it != tex.end(); ++it) {
            if (!allowed_regions.contains(it.key())) {
                assignError(
                    error_message,
                    QStringLiteral("Texture region '%1' is not supported.").arg(it.key()));
                return std::nullopt;
            }
            if (!it.value().isString()) {
                assignError(
                    error_message,
                    QStringLiteral("Texture field '%1' must be a relative PNG path.").arg(it.key()));
                return std::nullopt;
            }
            const auto rel = it.value().toString();
            if (!isSafeTexturePath(rel)) {
                assignError(
                    error_message,
                    QStringLiteral("Texture field '%1' has an unsafe or unsupported path: %2")
                        .arg(it.key(), rel));
                return std::nullopt;
            }
            if (referenced_paths.contains(rel)) {
                assignError(
                    error_message,
                    QStringLiteral("Texture path '%1' must not be assigned to multiple regions.")
                        .arg(rel));
                return std::nullopt;
            }
            const auto full = QDir(base_path).filePath(rel);
            const QFileInfo texture_info(full);
            if (!texture_info.exists() || !texture_info.isFile() || texture_info.isSymLink()
                || !isPathInside(base_path, full)) {
                assignError(
                    error_message,
                    QStringLiteral("Referenced texture does not exist inside the skin: %1").arg(rel));
                return std::nullopt;
            }
            referenced_paths.insert(rel);
            theme.textures.paths.insert(it.key(), full);
        }
    }

    if (root.contains(QStringLiteral("texture_opacity"))) {
        if (!root.value(QStringLiteral("texture_opacity")).isDouble()) {
            assignError(error_message, QStringLiteral("texture_opacity must be a number from 0 to 1."));
            return std::nullopt;
        }
        const double opacity = root.value(QStringLiteral("texture_opacity")).toDouble();
        if (!std::isfinite(opacity) || opacity < 0.0 || opacity > 1.0) {
            assignError(error_message, QStringLiteral("texture_opacity must be between 0 and 1."));
            return std::nullopt;
        }
        theme.textures.opacity = opacity;
    }

    if (root.contains(QStringLiteral("texture_mode"))) {
        if (!root.value(QStringLiteral("texture_mode")).isString()) {
            assignError(error_message, QStringLiteral("texture_mode must be tile, stretch, or cover."));
            return std::nullopt;
        }
        const QString mode = root.value(QStringLiteral("texture_mode")).toString();
        if (mode.compare(u"tile", Qt::CaseInsensitive) == 0) {
            theme.textures.mode = TextureMode::Tile;
        } else if (mode.compare(u"stretch", Qt::CaseInsensitive) == 0) {
            theme.textures.mode = TextureMode::Stretch;
        } else if (mode.compare(u"cover", Qt::CaseInsensitive) == 0) {
            theme.textures.mode = TextureMode::Cover;
        } else {
            assignError(error_message, QStringLiteral("texture_mode must be tile, stretch, or cover."));
            return std::nullopt;
        }
    }

    return theme;
}

// ─── loadBuiltin ──────────────────────────────────────────────────────────────

std::optional<CosmoTheme> SkinLoader::loadBuiltin(const QString &resource_prefix)
{
    const auto json_path = resource_prefix + QStringLiteral("/theme.json");
    QFile f(json_path);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "SkinLoader: cannot open built-in skin:" << json_path;
        return std::nullopt;
    }

    QString error_message;
    auto theme = parseThemeJson(f.readAll(), resource_prefix, &error_message, false);
    if (!theme) {
        qWarning() << "SkinLoader: invalid built-in skin" << resource_prefix << error_message;
    }
    if (theme) {
        theme->builtin = true;
    }
    return theme;
}

// ─── loadFromDirectory ────────────────────────────────────────────────────────

std::optional<CosmoTheme> SkinLoader::loadFromDirectory(const QString &skin_dir)
{
    const auto json_path = skin_dir + QStringLiteral("/theme.json");
    QFile f(json_path);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "SkinLoader: cannot open" << json_path;
        return std::nullopt;
    }

    if (f.size() <= 0 || f.size() > kMaxThemeBytes) {
        qWarning() << "SkinLoader: theme.json is empty or exceeds 256 KiB:" << json_path;
        return std::nullopt;
    }

    QString error_message;
    auto theme = parseThemeJson(f.readAll(), skin_dir, &error_message);
    if (!theme) {
        qWarning() << "SkinLoader: invalid custom skin" << skin_dir << error_message;
    }
    if (theme) {
        theme->builtin = false;
    }
    return theme;
}

// ─── importArchive ────────────────────────────────────────────────────────────

std::optional<CosmoTheme> SkinLoader::importArchive(const QString &archive_path,
                                                    const QString &dest_dir)
{
    auto result = importArchiveDetailed(archive_path, dest_dir, false);
    if (!result.succeeded()) {
        qWarning() << "SkinLoader: import failed:" << result.error_message;
        return std::nullopt;
    }
    return result.theme;
}

SkinImportResult SkinLoader::importArchiveDetailed(
    const QString &archive_path,
    const QString &dest_dir,
    bool replace_existing) {
    SkinImportResult result;

    const QFileInfo archive_info(archive_path);
    const QString suffix = archive_info.suffix();
    if (suffix.compare(u"cosmo", Qt::CaseInsensitive) != 0
        && suffix.compare(u"zip", Qt::CaseInsensitive) != 0) {
        result.error_message = QStringLiteral("Select a .cosmo or .zip skin archive.");
        return result;
    }

    QString skin_name = archive_info.completeBaseName().normalized(QString::NormalizationForm_KC);
    skin_name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")), QStringLiteral("_"));
    skin_name.remove(QRegularExpression(QStringLiteral("^[._-]+")));
    skin_name.remove(QRegularExpression(QStringLiteral(R"([.]+$)")));
    skin_name.truncate(64);
    if (skin_name.isEmpty()) {
        skin_name = QStringLiteral("skin");
    }
    static const QRegularExpression windows_device_name(
        QStringLiteral(R"(^(?:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\..*)?$)"),
        QRegularExpression::CaseInsensitiveOption);
    if (windows_device_name.match(skin_name).hasMatch()) {
        skin_name.prepend(QStringLiteral("skin_"));
        skin_name.truncate(64);
    }

    QString archive_error;
    auto archive = detail::readSkinArchive(archive_path, archive_error);
    if (!archive) {
        result.error_message = archive_error;
        return result;
    }

    quint64 decoded_pixel_total = 0;
    for (auto it = archive->files.cbegin(); it != archive->files.cend(); ++it) {
        if (it.key().endsWith(u".png", Qt::CaseInsensitive)) {
            quint64 image_pixels = 0;
            if (!validatePng(it.value(), it.key(), image_pixels, result.error_message)) {
                return result;
            }
            decoded_pixel_total += image_pixels;
            if (decoded_pixel_total > kMaxDecodedImagePixels) {
                result.error_message = QStringLiteral(
                    "Skin images exceed the 20-megapixel decoded-image budget.");
                return result;
            }
        }
    }

    if (!QDir().mkpath(dest_dir)) {
        result.error_message = QStringLiteral("Could not create the custom-skins directory.");
        return result;
    }
    const QFileInfo destination_info(dest_dir);
    if (!destination_info.isDir() || destination_info.isSymLink()) {
        result.error_message = QStringLiteral("The custom-skins destination is not a safe directory.");
        return result;
    }

    const QDir destination(dest_dir);
    QStringList case_insensitive_matches;
    const auto destination_entries = destination.entryList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    for (const QString &entry : destination_entries) {
        if (entry.compare(skin_name, Qt::CaseInsensitive) == 0) {
            case_insensitive_matches.append(entry);
        }
    }
    if (case_insensitive_matches.size() > 1) {
        result.error_message = QStringLiteral(
            "Multiple installed skins differ only by letter case; resolve them before importing '%1'.")
            .arg(skin_name);
        return result;
    }
    const QString storage_name = case_insensitive_matches.isEmpty()
        ? skin_name
        : case_insensitive_matches.constFirst();
    const QString target_dir = destination.filePath(storage_name);
    result.target_path = target_dir;

    QTemporaryDir staging(
        QDir(dest_dir).filePath(QStringLiteral(".cosmoskin-import-XXXXXX")));
    if (!staging.isValid()) {
        result.error_message = QStringLiteral("Could not create a temporary skin staging directory.");
        return result;
    }
    const QString staged_skin = QDir(staging.path()).filePath(QStringLiteral("skin"));
    if (!QDir().mkpath(staged_skin)) {
        result.error_message = QStringLiteral("Could not initialize the skin staging directory.");
        return result;
    }

    for (auto it = archive->files.cbegin(); it != archive->files.cend(); ++it) {
        if (!writeStagedFile(staged_skin, it.key(), it.value(), result.error_message)) {
            return result;
        }
    }

    QString parse_error;
    auto staged_theme = parseThemeJson(
        archive->files.value(QStringLiteral("theme.json")), staged_skin, &parse_error);
    if (!staged_theme) {
        result.error_message = parse_error;
        return result;
    }

    QSet<QString> referenced_textures;
    for (const QString &texture_path : staged_theme->textures.paths) {
        referenced_textures.insert(QDir(staged_skin).relativeFilePath(texture_path));
    }
    for (auto it = archive->files.cbegin(); it != archive->files.cend(); ++it) {
        if (it.key().startsWith(QStringLiteral("textures/"))
            && !referenced_textures.contains(it.key())) {
            result.error_message = QStringLiteral(
                "Texture %1 is present but is not referenced by theme.json.").arg(it.key());
            return result;
        }
    }

    const QFileInfo existing_target(target_dir);
    if (existing_target.exists() && !replace_existing) {
        result.status = SkinImportStatus::AlreadyExists;
        result.error_message = QStringLiteral("A skin named '%1' is already installed.").arg(storage_name);
        return result;
    }
    if (existing_target.exists()
        && (!existing_target.isDir() || existing_target.isSymLink())) {
        result.error_message = QStringLiteral(
            "The existing skin target is not a replaceable directory: %1").arg(target_dir);
        return result;
    }

    QString backup_dir;
    if (existing_target.exists()) {
        backup_dir = QDir(dest_dir).filePath(
            QStringLiteral(".cosmoskin-backup-%1")
                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        if (!QDir().rename(target_dir, backup_dir)) {
            result.error_message = QStringLiteral("Could not stage the existing skin for replacement.");
            return result;
        }
    }

    if (!QDir().rename(staged_skin, target_dir)) {
        if (!backup_dir.isEmpty() && !QDir().rename(backup_dir, target_dir)) {
            qCritical() << "SkinLoader: could not roll back skin replacement" << target_dir;
        }
        result.error_message = QStringLiteral("Could not install the validated skin.");
        return result;
    }

    auto installed_theme = loadFromDirectory(target_dir);
    if (!installed_theme) {
        QDir(target_dir).removeRecursively();
        if (!backup_dir.isEmpty() && !QDir().rename(backup_dir, target_dir)) {
            qCritical() << "SkinLoader: could not restore skin after verification failure" << target_dir;
        }
        result.error_message = QStringLiteral("The installed skin failed final verification.");
        return result;
    }

    if (!backup_dir.isEmpty() && !QDir(backup_dir).removeRecursively()) {
        qWarning() << "SkinLoader: imported skin but could not remove backup" << backup_dir;
    }

    installed_theme->id = QStringLiteral("custom:") + storage_name;
    result.status = SkinImportStatus::Imported;
    result.theme = std::move(installed_theme);
    result.error_message.clear();
    return result;
}

// ─── discoverAll ──────────────────────────────────────────────────────────────

QList<CosmoTheme> SkinLoader::discoverAll(const QString &custom_skins_dir)
{
    QList<CosmoTheme> result;

    // Built-in skins
    if (auto dark = loadBuiltin(QStringLiteral(":/skins/dark"))) {
        dark->id = QStringLiteral("builtin:dark");
        result.append(*dark);
    }
    if (auto light = loadBuiltin(QStringLiteral(":/skins/light"))) {
        light->id = QStringLiteral("builtin:light");
        result.append(*light);
    }

    // Custom skins from disk
    QDir dir(custom_skins_dir);
    if (dir.exists()) {
        const auto entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const auto &entry : entries) {
            const auto skin_path = custom_skins_dir + QStringLiteral("/") + entry;
            if (auto t = loadFromDirectory(skin_path)) {
                t->id = QStringLiteral("custom:") + entry;
                result.append(*t);
            }
        }
    }

    return result;
}
