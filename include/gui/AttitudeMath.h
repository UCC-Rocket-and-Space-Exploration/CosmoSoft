/** @file AttitudeMath.h
 * @brief Presentation conversion for explicitly supplied rocket attitude.
 */
#ifndef COSMO_SOFT_ATTITUDEMATH_H
#define COSMO_SOFT_ATTITUDEMATH_H

#include "domain/FlightSample.h"
#include <QQuaternion>
#include <cmath>
#include <optional>

namespace cosmo::gui {
/**
 * @brief Convert degree-valued intrinsic XYZ angles to a body-to-world
 * rotation. The right-handed world is X east, Y up, Z south. The upright rocket
 * points along body +Y; rotation is qX * qY * qZ, so Y is longitudinal roll at
 * zero tilt.
 * @return A unit quaternion, or no value when any angle is missing or
 * excessive.
 */
[[nodiscard]] inline std::optional<QQuaternion>
attitudeQuaternion(const AngularRotationCoordinates &angles) {
  for (const double angle : {angles.x, angles.y, angles.z}) {
    if (!std::isfinite(angle) || std::abs(angle) > 1.0e6)
      return std::nullopt;
  }
  const auto degrees = [](double angle) {
    return static_cast<float>(std::remainder(angle, 360.0));
  };
  return (QQuaternion::fromAxisAndAngle(1, 0, 0, degrees(angles.x)) *
          QQuaternion::fromAxisAndAngle(0, 1, 0, degrees(angles.y)) *
          QQuaternion::fromAxisAndAngle(0, 0, 1, degrees(angles.z)))
      .normalized();
}
} // namespace cosmo::gui
#endif
