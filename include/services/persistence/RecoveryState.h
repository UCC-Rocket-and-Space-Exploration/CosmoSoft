#pragma once
#include "domain/FlightSample.h"
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>

namespace cosmo::mission {
/** @brief Validate coordinates without treating an initial zero placeholder as
 * a fix. */
inline bool usablePosition(double lat, double lon) {
  return std::isfinite(lat) && std::isfinite(lon) && std::abs(lat) <= 90 &&
         std::abs(lon) <= 180 && (std::abs(lat) > 1e-8 || std::abs(lon) > 1e-8);
}
/** @brief Surface range in metres and true bearing in degrees from a named
 * reference. */
inline std::pair<double, double> rangeBearing(double fromLat, double fromLon,
                                              double lat, double lon) {
  constexpr double radians = std::numbers::pi / 180;
  const double a = fromLat * radians, b = lat * radians,
               d = (lon - fromLon) * radians;
  const double h = std::pow(std::sin((b - a) / 2), 2) +
                   std::cos(a) * std::cos(b) * std::pow(std::sin(d / 2), 2);
  const double distance =
      6371000 * 2 * std::asin(std::sqrt(std::clamp(h, 0.0, 1.0)));
  const double bearing =
      std::atan2(std::sin(d) * std::cos(b),
                 std::cos(a) * std::sin(b) -
                     std::sin(a) * std::cos(b) * std::cos(d)) /
      radians;
  return {distance, std::fmod(bearing + 360, 360)};
}
/** @brief Persistent last real-live fix, independent of replay and
 * demonstration data. */
class RecoveryState {
public:
  /** @brief Start a real acquisition without reusing another flight's launch
   * reference. */
  void beginSession(const QString &identity, const QString &source) {
    m_identity = identity;
    m_source = source;
    m_reference = {};
    markEarlier();
  }
  /** @brief Mark the retained point as belonging to an ended acquisition. */
  void markEarlier() {
    if (!m_record.isEmpty())
      m_record["earlier"] = true;
  }
  /** @brief Store an explicitly chosen recovery reference with the retained
   * fix. */
  void setReference(const QJsonObject &reference) {
    const auto lat = reference.value("lat"), lon = reference.value("lon");
    if (!lat.isDouble() || !lon.isDouble() || !std::isfinite(lat.toDouble()) ||
        !std::isfinite(lon.toDouble()) || std::abs(lat.toDouble()) > 90 ||
        std::abs(lon.toDouble()) > 180)
      return;
    m_reference = reference;
    if (!m_record.isEmpty())
      m_record["reference"] = reference;
  }
  /** @brief Retain a real received position with its UTC arrival time. */
  bool observe(const FlightSample &sample, qint64 receivedUtcMs,
               bool realLive) {
    const auto &point = sample.coordinates;
    if (!realLive || receivedUtcMs <= 0 ||
        !usablePosition(point.latitude, point.longitude))
      return false;
    if (m_reference.isEmpty())
      m_reference = {{"lat", point.latitude},
                     {"lon", point.longitude},
                     {"name", "launch point"}};
    m_record = {{"lat", point.latitude},
                {"lon", point.longitude},
                {"receivedUtcMs", static_cast<double>(receivedUtcMs)},
                {"earlier", false},
                {"session", m_identity},
                {"source", m_source},
                {"reference", m_reference}};
    return true;
  }
  /** @brief Return serializable last-known position and original receipt time.
   */
  [[nodiscard]] QJsonObject json() const { return m_record; }
  /** @brief Restore validated state, marking its origin as an earlier session.
   */
  bool restore(const QJsonObject &record) {
    if (!record.value("lat").isDouble() || !record.value("lon").isDouble() ||
        !usablePosition(record.value("lat").toDouble(),
                        record.value("lon").toDouble()) ||
        !record.value("receivedUtcMs").isDouble() ||
        !std::isfinite(record.value("receivedUtcMs").toDouble()) ||
        record.value("receivedUtcMs").toDouble() <= 0 ||
        record.value("receivedUtcMs").toDouble() >=
            static_cast<double>(std::numeric_limits<qint64>::max()))
      return false;
    m_record = record;
    m_record["earlier"] = true;
    if (record.value("reference").isObject())
      setReference(record.value("reference").toObject());
    return true;
  }

private:
  QJsonObject m_record, m_reference;
  QString m_identity, m_source;
};
} // namespace cosmo::mission
