#ifndef COSMO_SOFT_SKINARCHIVE_H
#define COSMO_SOFT_SKINARCHIVE_H

#include <QMap>
#include <QByteArray>
#include <QString>

#include <optional>

namespace cosmo::detail {

/**
 * @brief Validated files read from a bounded CosmoSoft skin archive.
 */
struct SkinArchiveContents {
    QMap<QString, QByteArray> files;
};

/**
 * @brief Read and validate a .cosmo ZIP archive without extracting it.
 *
 * The reader accepts stored and deflated ZIP entries, verifies each CRC, and
 * enforces the CosmoSoft skin allow-list and resource limits.
 *
 * @param archive_path Path to the archive on disk.
 * @param error_message Receives a user-facing failure description.
 * @return Validated in-memory files, or std::nullopt on failure.
 */
std::optional<SkinArchiveContents> readSkinArchive(
    const QString &archive_path,
    QString &error_message);

} // namespace cosmo::detail

#endif // COSMO_SOFT_SKINARCHIVE_H
