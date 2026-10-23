#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <cmath>
#include <optional>

namespace cosmo::mission {
/** @brief Default editable profiles; confirmations are never persisted. */
inline QJsonArray defaultChecklists() {
  const QJsonArray items{QJsonObject{{"name", "Airframe inspected"},
                                     {"kind", "manual"},
                                     {"required", true},
                                     {"limit", 0}},
                         QJsonObject{{"name", "Fresh telemetry"},
                                     {"kind", "packetAge"},
                                     {"required", true},
                                     {"limit", 2}},
                         QJsonObject{{"name", "Received position"},
                                     {"kind", "gps"},
                                     {"required", true},
                                     {"limit", 0}}};
  return {QJsonObject{{"name", "Launch Day"}, {"items", items}},
          QJsonObject{{"name", "Bench Test"}, {"items", QJsonArray{items[1]}}}};
}
/** @brief Validate bounded profile names, item kinds and finite thresholds. */
inline bool validChecklists(const QJsonArray &profiles) {
  if (profiles.isEmpty() || profiles.size() > 32)
    return false;
  QStringList names;
  for (const auto &value : profiles) {
    const auto profile = value.toObject();
    const auto name = profile.value("name").toString().trimmed();
    if (name.isEmpty() || name.size() > 128 || names.contains(name) ||
        !profile.value("items").isArray())
      return false;
    names << name;
    const auto items = profile.value("items").toArray();
    if (items.size() > 64)
      return false;
    for (const auto &entry : items) {
      const auto item = entry.toObject();
      const auto title = item.value("name").toString().trimmed();
      if (title.isEmpty() || title.size() > 128 ||
          !QStringList{"manual", "packetAge", "gps", "battery"}.contains(
              item.value("kind").toString()) ||
          !item.value("required").isBool() || !item.value("limit").isDouble())
        return false;
      const double limit = item.value("limit").toDouble();
      if (!std::isfinite(limit) || limit < 0 || limit > 1e6)
        return false;
    }
  }
  return true;
}
/** @brief Evaluate one measured check; missing inputs stay unknown. */
inline std::optional<bool> measuredCheck(const QString &kind, double limit,
                                         std::optional<double> age,
                                         std::optional<double> battery,
                                         bool hasPosition) {
  if (kind == "packetAge")
    return age ? std::optional(*age <= limit) : std::nullopt;
  if (kind == "battery")
    return battery ? std::optional(*battery >= limit) : std::nullopt;
  if (kind == "gps")
    return hasPosition ? std::optional(true) : std::nullopt;
  return {};
}
} // namespace cosmo::mission
