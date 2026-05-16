#include "gui/SkinLoader.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

using namespace cosmo;

// ─── JSON helpers ─────────────────────────────────────────────────────────────

static QString jsonStr(const QJsonObject &obj, const QString &key, const QString &fallback)
{
    if (obj.contains(key) && obj[key].isString()) {
        return obj[key].toString();
    }
    return fallback;
}

static TextureMode parseTileMode(const QString &str)
{
    if (str.compare(u"stretch", Qt::CaseInsensitive) == 0) return TextureMode::Stretch;
    if (str.compare(u"cover", Qt::CaseInsensitive) == 0) return TextureMode::Cover;
    return TextureMode::Tile;
}

// ─── parseThemeJson ───────────────────────────────────────────────────────────

std::optional<CosmoTheme> SkinLoader::parseThemeJson(const QByteArray &json,
                                                     const QString &base_path)
{
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(json, &err);
    if (doc.isNull() || !doc.isObject()) {
        qWarning() << "SkinLoader: invalid theme.json:" << err.errorString();
        return std::nullopt;
    }

    const auto root = doc.object();

    CosmoTheme theme;
    theme.name    = jsonStr(root, QStringLiteral("name"), QStringLiteral("Custom"));
    theme.author  = jsonStr(root, QStringLiteral("author"), QStringLiteral("Unknown"));
    theme.version = jsonStr(root, QStringLiteral("version"), QStringLiteral("1.0"));

    const auto preview_file = base_path + QStringLiteral("/preview.png");
    if (QFile::exists(preview_file)) {
        theme.preview_path = preview_file;
    }

    // Palette
    if (root.contains(QStringLiteral("palette")) && root[QStringLiteral("palette")].isObject()) {
        const auto pal = root[QStringLiteral("palette")].toObject();
        ColorPalette &p = theme.palette;

        p.bg_base        = jsonStr(pal, QStringLiteral("bg_base"), p.bg_base);
        p.bg_dark        = jsonStr(pal, QStringLiteral("bg_dark"), p.bg_dark);
        p.bg_panel       = jsonStr(pal, QStringLiteral("bg_panel"), p.bg_panel);
        p.bg_input       = jsonStr(pal, QStringLiteral("bg_input"), p.bg_input);
        p.bg_button      = jsonStr(pal, QStringLiteral("bg_button"), p.bg_button);

        p.text_primary   = jsonStr(pal, QStringLiteral("text_primary"), p.text_primary);
        p.text_mid       = jsonStr(pal, QStringLiteral("text_mid"), p.text_mid);
        p.text_muted     = jsonStr(pal, QStringLiteral("text_muted"), p.text_muted);
        p.text_dim       = jsonStr(pal, QStringLiteral("text_dim"), p.text_dim);

        p.border_default = jsonStr(pal, QStringLiteral("border_default"), p.border_default);
        p.border_light   = jsonStr(pal, QStringLiteral("border_light"), p.border_light);
        p.border_subtle  = jsonStr(pal, QStringLiteral("border_subtle"), p.border_subtle);
        p.border_panel   = jsonStr(pal, QStringLiteral("border_panel"), p.border_panel);

        p.btn_hover      = jsonStr(pal, QStringLiteral("btn_hover"), p.btn_hover);
        p.btn_pressed    = jsonStr(pal, QStringLiteral("btn_pressed"), p.btn_pressed);

        p.accent_link    = jsonStr(pal, QStringLiteral("accent_link"), p.accent_link);
        p.accent_checkbox = jsonStr(pal, QStringLiteral("accent_checkbox"), p.accent_checkbox);
        p.accent_checkbox_border = jsonStr(pal, QStringLiteral("accent_checkbox_border"), p.accent_checkbox_border);
        p.danger         = jsonStr(pal, QStringLiteral("danger"), p.danger);
        p.error          = jsonStr(pal, QStringLiteral("error"), p.error);
        p.select_bg      = jsonStr(pal, QStringLiteral("select_bg"), p.select_bg);

        p.success        = jsonStr(pal, QStringLiteral("success"), p.success);
        p.success_bg     = jsonStr(pal, QStringLiteral("success_bg"), p.success_bg);
        p.warning        = jsonStr(pal, QStringLiteral("warning"), p.warning);
        p.warning_bg     = jsonStr(pal, QStringLiteral("warning_bg"), p.warning_bg);
        p.info           = jsonStr(pal, QStringLiteral("info"), p.info);
        p.info_bg        = jsonStr(pal, QStringLiteral("info_bg"), p.info_bg);
        p.focus_ring     = jsonStr(pal, QStringLiteral("focus_ring"), p.focus_ring);
        p.focus_ring_offset = jsonStr(pal, QStringLiteral("focus_ring_offset"), p.focus_ring_offset);
    }

    // Textures
    if (root.contains(QStringLiteral("textures")) && root[QStringLiteral("textures")].isObject()) {
        const auto tex = root[QStringLiteral("textures")].toObject();
        for (auto it = tex.begin(); it != tex.end(); ++it) {
            if (!it.value().isString()) continue;
            const auto rel = it.value().toString();
            const auto full = base_path + QStringLiteral("/") + rel;
            if (QFile::exists(full)) {
                theme.textures.paths.insert(it.key(), full);
            }
        }
    }

    theme.textures.opacity = root.value(QStringLiteral("texture_opacity")).toDouble(0.15);
    theme.textures.mode = parseTileMode(
        root.value(QStringLiteral("texture_mode")).toString(QStringLiteral("tile")));

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

    auto theme = parseThemeJson(f.readAll(), resource_prefix);
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

    auto theme = parseThemeJson(f.readAll(), skin_dir);
    if (theme) {
        theme->builtin = false;
    }
    return theme;
}

// ─── importArchive ────────────────────────────────────────────────────────────

std::optional<CosmoTheme> SkinLoader::importArchive(const QString &archive_path,
                                                    const QString &dest_dir)
{
    QFileInfo info(archive_path);
    if (!info.exists() || info.size() > 10 * 1024 * 1024) {
        qWarning() << "SkinLoader: archive missing or >10MB:" << archive_path;
        return std::nullopt;
    }

    const auto skin_name = info.completeBaseName();
    const auto target_dir = dest_dir + QStringLiteral("/") + skin_name;

    QDir().mkpath(target_dir);

    // Use platform unzip (available on macOS/Linux; Windows has tar)
    QProcess proc;
#ifdef _WIN32
    proc.start(QStringLiteral("tar"), {QStringLiteral("-xf"), archive_path, QStringLiteral("-C"), target_dir});
#else
    proc.start(QStringLiteral("unzip"), {QStringLiteral("-o"), archive_path, QStringLiteral("-d"), target_dir});
#endif
    proc.waitForFinished(5000);

    if (proc.exitCode() != 0) {
        qWarning() << "SkinLoader: extraction failed:" << proc.readAllStandardError();
        return std::nullopt;
    }

    auto theme = loadFromDirectory(target_dir);
    if (theme) {
        theme->id = QStringLiteral("custom:") + skin_name;
    }
    return theme;
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
