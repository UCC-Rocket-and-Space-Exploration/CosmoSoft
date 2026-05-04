/**
 * @file CosmoTheme.h
 * @brief Data model for CosmoSoft skins: color palette, texture definitions,
 *        and metadata.
 *
 * A CosmoTheme can originate from a built-in resource (dark/light) or from
 * a user-imported .cosmo archive (zip containing theme.json + PNGs).
 */

#ifndef COSMO_SOFT_COSMOTHEME_H
#define COSMO_SOFT_COSMOTHEME_H

#include <QString>
#include <QMap>

namespace cosmo {

/**
 * @brief Full color palette consumed by QSS generation and widget paint.
 *
 * Every field has a sensible dark-mode default so that partially-specified
 * custom skins fall back gracefully.
 */
struct ColorPalette {
    QString bg_base            = QStringLiteral("#1f1f1f");
    QString bg_dark            = QStringLiteral("#161618");
    QString bg_panel           = QStringLiteral("#2b2d33");
    QString bg_input           = QStringLiteral("#1a1a1a");
    QString bg_button          = QStringLiteral("#3d3f47");

    QString text_primary       = QStringLiteral("#f8f8f8");
    QString text_mid           = QStringLiteral("#c8c8c8");
    QString text_muted         = QStringLiteral("#b0bcc8");
    QString text_dim           = QStringLiteral("#8fa0b0");

    QString border_default     = QStringLiteral("#4d4d4d");
    QString border_light       = QStringLiteral("#6a6a6a");
    QString border_subtle      = QStringLiteral("#3a3a3a");
    QString border_panel       = QStringLiteral("#3b3b45");

    QString btn_hover          = QStringLiteral("#4d4f57");
    QString btn_pressed        = QStringLiteral("#2d2f37");

    QString accent_link        = QStringLiteral("#6ab0de");
    QString accent_checkbox    = QStringLiteral("#4a7fb5");
    QString accent_checkbox_border = QStringLiteral("#5a9fd5");
    QString danger             = QStringLiteral("#ff6b6b");
    QString error              = QStringLiteral("#e05555");
    QString select_bg          = QStringLiteral("#4b4b4b");
};

/**
 * @brief Texture rendering mode for panel backgrounds.
 */
enum class TextureMode { Tile, Stretch, Cover };

/**
 * @brief Optional background textures for UI regions.
 *
 * Keys: "sidebar", "toolbar", "panel", "chart_bg", "settings_bg".
 * Values: absolute path to extracted PNG on disk (empty if unused).
 */
struct TextureSet {
    QMap<QString, QString> paths;
    double opacity = 0.15;
    TextureMode mode = TextureMode::Tile;
};

/**
 * @brief Complete skin definition: metadata + palette + textures.
 */
struct CosmoTheme {
    QString name    = QStringLiteral("Dark");
    QString author  = QStringLiteral("CosmoSoft");
    QString version = QStringLiteral("1.0");

    /** @brief Identifier used in QSettings: "builtin:dark", "custom:filename" */
    QString id;

    /** @brief Path to preview.png (256x160) for the settings selector. */
    QString preview_path;

    ColorPalette palette;
    TextureSet textures;

    /** @brief True if this is a built-in (non-removable) skin. */
    bool builtin = true;
};

} // namespace cosmo

#endif // COSMO_SOFT_COSMOTHEME_H
