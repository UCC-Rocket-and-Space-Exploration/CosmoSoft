/**
 * @file ThemeManager.h
 * @brief Singleton that owns the active CosmoTheme, generates QSS, and
 *        notifies the application when the skin changes.
 *
 * All theme-dependent code should either:
 *   - Connect to themeChanged() and re-apply styles, or
 *   - Use Theme:: accessors (which delegate here at runtime).
 */

#ifndef COSMO_SOFT_THEMEMANAGER_H
#define COSMO_SOFT_THEMEMANAGER_H

#include "gui/CosmoTheme.h"

#include <QObject>
#include <QPixmap>
#include <QString>
#include <QMap>

namespace cosmo {

class ThemeManager : public QObject {
    Q_OBJECT

public:
    /** @brief Access the singleton instance (created on first call). */
    static ThemeManager &instance();

    /** @brief Currently active theme. */
    [[nodiscard]] const CosmoTheme &current() const { return m_active; }

    /** @brief Shortcut to the active color palette. */
    [[nodiscard]] const ColorPalette &palette() const { return m_active.palette; }

    /**
     * @brief Apply a new theme and persist the choice.
     *
     * Generates QSS from the palette, applies it globally, caches textures,
     * and emits themeChanged().
     */
    void setActiveSkin(const CosmoTheme &theme);

    /**
     * @brief Load the persisted skin from QSettings on startup.
     * @param custom_skins_dir Path to the imported-skins directory.
     */
    void loadPersistedSkin(const QString &custom_skins_dir);

    /**
     * @brief Retrieve a cached texture pixmap for a named region.
     * @param region Supported region name (currently only "panel").
     * @return Cached QPixmap (null pixmap if the region has no texture).
     */
    [[nodiscard]] QPixmap texture(const QString &region) const;

    /** @brief Global texture opacity from the active skin. */
    [[nodiscard]] double textureOpacity() const { return m_active.textures.opacity; }

    /** @brief Global texture rendering mode from the active skin. */
    [[nodiscard]] TextureMode textureMode() const { return m_active.textures.mode; }

    /** @brief Generate the full QSS string from a palette (for preview). */
    [[nodiscard]] static QString generateQss(const ColorPalette &pal);

    /** @brief Path where imported skins are stored. */
    [[nodiscard]] static QString skinsDirectory();

signals:
    /** @brief Emitted after the active theme changes (palette + textures ready). */
    void themeChanged();

private:
    ThemeManager();
    ~ThemeManager() override = default;
    ThemeManager(const ThemeManager &) = delete;
    ThemeManager &operator=(const ThemeManager &) = delete;

    void rebuildTextureCache();

    CosmoTheme m_active;
    QMap<QString, QPixmap> m_texture_cache;
};

} // namespace cosmo

#endif // COSMO_SOFT_THEMEMANAGER_H
