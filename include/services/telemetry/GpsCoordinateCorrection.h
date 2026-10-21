#pragma once

#include "domain/FlightSample.h"
#include <cmath>

namespace cosmo::telemetry {

/**
 * @brief Correct an FC that sends western longitudes with a positive sign.
 * @param sample Live sample to normalize before recording and presentation.
 * @param enabled Explicitly selected compatibility mode; never infer a country.
 * Latitude, already-negative longitude, no-fix zeros and invalid GPS are
 * unchanged.
 */
inline void applyWestLongitudeCorrection(FlightSample &sample, bool enabled) {
  auto &point = sample.coordinates;
  if (enabled && std::isfinite(point.latitude) &&
      std::abs(point.latitude) <= 90.0 && std::isfinite(point.longitude) &&
      point.longitude > 0.0 && point.longitude <= 180.0)
    point.longitude = -point.longitude;
}

} // namespace cosmo::telemetry
