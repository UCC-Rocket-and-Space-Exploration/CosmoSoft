/**
 * @file SkinLoader.h
 * @brief Loads CosmoTheme definitions from built-in resources or .cosmo archives.
 *
 * .cosmo files are zip archives containing:
 *   theme.json   (required) — palette, metadata, texture references
 *   preview.png  (optional) — 256x160 thumbnail for the skin selector
 *   textures/    (optional) — PNG background images referenced by theme.json
 */

#ifndef COSMO_SOFT_SKINLOADER_H
#define COSMO_SOFT_SKINLOADER_H

#include "gui/CosmoTheme.h"

#include <QString>
#include <QList>
#include <optional>

namespace cosmo {

class SkinLoader {
public:
    /**
     * @brief Load a built-in skin from Qt resources.
     * @param resource_prefix e.g. ":/skins/dark" (contains theme.json)
     * @return Parsed CosmoTheme or nullopt on failure.
     */
    static std::optional<CosmoTheme> loadBuiltin(const QString &resource_prefix);

    /**
     * @brief Import a .cosmo archive from disk.
     *
     * Extracts to the application skin directory and returns the parsed theme.
     * @param archive_path Path to the .cosmo file.
     * @param dest_dir Directory to extract into (AppDataLocation/skins/).
     * @return Parsed CosmoTheme or nullopt on failure.
     */
    static std::optional<CosmoTheme> importArchive(const QString &archive_path,
                                                   const QString &dest_dir);

    /**
     * @brief Load a previously-imported custom skin from its extracted directory.
     * @param skin_dir Directory containing theme.json and textures/.
     * @return Parsed CosmoTheme or nullopt on failure.
     */
    static std::optional<CosmoTheme> loadFromDirectory(const QString &skin_dir);

    /**
     * @brief Discover all available skins (built-in + imported).
     * @param custom_skins_dir Directory where imported skins live.
     * @return List of loadable themes with metadata populated.
     */
    static QList<CosmoTheme> discoverAll(const QString &custom_skins_dir);

private:
    static std::optional<CosmoTheme> parseThemeJson(const QByteArray &json,
                                                    const QString &base_path);
};

} // namespace cosmo

#endif // COSMO_SOFT_SKINLOADER_H
