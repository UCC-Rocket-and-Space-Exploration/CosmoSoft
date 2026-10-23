#pragma once

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QString>
#include <cmath>
#include <optional>

namespace cosmo::mission {
/** @brief Flight annotations stored beside, and bound to, a telemetry file. */
struct FlightMetadata {
  QJsonArray events;
  QJsonObject reference;
};
/** @brief Digest a telemetry file without keeping a second full copy in memory.
 */
inline QByteArray recordingDigest(const QString &path) {
  QFile input(path);
  QCryptographicHash hash(QCryptographicHash::Sha256);
  if (!input.open(QIODevice::ReadOnly) || !hash.addData(&input))
    return {};
  return hash.result().toHex();
}
/** @brief Validate bounded annotations before displaying external metadata. */
inline bool validMetadata(const FlightMetadata &data) {
  if (data.events.size() > 10000)
    return false;
  for (const auto &value : data.events) {
    const auto event = value.toObject();
    if (!event.value("time").isDouble() ||
        !std::isfinite(event.value("time").toDouble()) ||
        event.value("time").toDouble() < 0 || !event.value("name").isString() ||
        event.value("name").toString().trimmed().isEmpty() ||
        event.value("name").toString().size() > 256)
      return false;
  }
  if (!data.reference.isEmpty()) {
    const auto lat = data.reference.value("lat"),
               lon = data.reference.value("lon");
    if (!lat.isDouble() || !lon.isDouble() || !std::isfinite(lat.toDouble()) ||
        !std::isfinite(lon.toDouble()) || std::abs(lat.toDouble()) > 90 ||
        std::abs(lon.toDouble()) > 180)
      return false;
  }
  return true;
}
/** @brief Atomically save metadata associated with the exact telemetry bytes.
 */
inline bool saveMetadata(const QString &path, const FlightMetadata &metadata) {
  if (!validMetadata(metadata))
    return false;
  const auto digest = recordingDigest(path);
  if (digest.isEmpty())
    return false;
  QSaveFile output(path + ".mission.json");
  output.setDirectWriteFallback(false);
  if (!output.open(QIODevice::WriteOnly))
    return false;
  const auto bytes =
      QJsonDocument(QJsonObject{{"version", 1},
                                {"sha256", QString::fromLatin1(digest)},
                                {"events", metadata.events},
                                {"reference", metadata.reference}})
          .toJson();
  if (output.write(bytes) != bytes.size()) {
    output.cancelWriting();
    return false;
  }
  return output.commit();
}
/** @brief Load matching metadata; an absent sidecar means empty annotations. */
inline std::optional<FlightMetadata> loadMetadata(const QString &path) {
  QFile input(path + ".mission.json");
  if (!input.exists())
    return FlightMetadata{};
  if (!input.open(QIODevice::ReadOnly) || input.size() > 4 * 1024 * 1024)
    return {};
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(input.readAll(), &error);
  if (error.error != QJsonParseError::NoError || !document.isObject())
    return {};
  const auto object = document.object();
  const auto digest = recordingDigest(path);
  if (digest.isEmpty() || object.value("version").toInt() != 1 ||
      object.value("sha256").toString() != QString::fromLatin1(digest) ||
      !object.value("events").isArray() ||
      !object.value("reference").isObject())
    return {};
  FlightMetadata metadata{object.value("events").toArray(),
                          object.value("reference").toObject()};
  return validMetadata(metadata) ? std::optional(metadata) : std::nullopt;
}
} // namespace cosmo::mission
