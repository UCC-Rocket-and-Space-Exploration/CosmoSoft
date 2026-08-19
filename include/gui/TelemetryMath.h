/**
 * @file TelemetryMath.h
 * @brief Overflow-safe calculations shared by live telemetry widgets.
 */

#ifndef COSMO_SOFT_TELEMETRYMATH_H
#define COSMO_SOFT_TELEMETRYMATH_H

#include "domain/FlightSample.h"

#include <cmath>
#include <optional>

namespace cosmo::gui {

/**
 * @brief Calculate vertical velocity between two ordered telemetry samples.
 * @return Metres per second, or std::nullopt for non-increasing timestamps or
 * non-finite input/result values.
 */
[[nodiscard]] inline std::optional<double> verticalVelocityMetersPerSecond(
    const FlightSample &previous,
    const FlightSample &current) noexcept {
    const double elapsedSeconds = (static_cast<double>(current.timestamp)
                                   - static_cast<double>(previous.timestamp))
        / 1000.0;
    if (!(elapsedSeconds > 0.0)
        || !std::isfinite(previous.altitude)
        || !std::isfinite(current.altitude)) {
        return std::nullopt;
    }

    const double velocity = (current.altitude - previous.altitude) / elapsedSeconds;
    return std::isfinite(velocity) ? std::optional<double>(velocity) : std::nullopt;
}

} // namespace cosmo::gui

#endif // COSMO_SOFT_TELEMETRYMATH_H
