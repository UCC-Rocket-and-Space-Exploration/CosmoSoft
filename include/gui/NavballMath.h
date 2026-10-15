#pragma once
#include <QQuaternion>
#include <QVector3D>
#include <algorithm>
#include <cmath>
#include <optional>

namespace cosmo::gui {
/** @brief Rocket camera frame: +Y is the nose and -Z is screen up. */
struct NavballFrame {
  QVector3D right, nose, up;
  /** @brief Build a sphere-view frame from the shared body-to-world pose. */
  explicit NavballFrame(const QQuaternion &q)
      : right(q.rotatedVector({1, 0, 0})), nose(q.rotatedVector({0, 1, 0})),
        up(q.rotatedVector({0, 0, -1})) {}
  /** @brief World ray through a unit-disc coordinate; outside the ball has no
   * ray. */
  [[nodiscard]] std::optional<QVector3D> ray(float x, float y) const {
    if (x * x + y * y > 1)
      return {};
    return right * x + nose * std::sqrt(std::max(0.0F, 1 - x * x - y * y)) +
           up * y;
  }
};
} // namespace cosmo::gui
