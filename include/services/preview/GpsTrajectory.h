#pragma once

#include "domain/FlightSample.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

namespace cosmo::preview {

/** @brief Display-only GPS alignment; recorded samples remain unchanged. */
class GpsTrajectory {
public:
  /**
   * @brief Append a raw sample in acquisition order, before map decimation.
   * @return True when a completed held-coordinate interval needs redrawing.
   */
  bool append(const FlightSample &sample, int index) {
    const bool interrupted =
        m_interrupted || sample.timestamp <= m_lastTimestamp ||
        static_cast<long double>(sample.timestamp) - m_lastTimestamp >
            kMaxGapMs;
    m_lastTimestamp = sample.timestamp;
    if (!valid(sample)) {
      m_interrupted = true;
      return false;
    }
    m_interrupted = false;
    if (!m_fixes.empty() && !interrupted &&
        sample.coordinates.latitude == m_fixes.back().coordinate.latitude &&
        sample.coordinates.longitude == m_fixes.back().coordinate.longitude)
      return false;

    bool redraw = false;
    if (!m_fixes.empty()) {
      if (interrupted)
        ++m_currentSegment;
      const auto &previous = m_fixes.back();
      redraw =
          !interrupted && index > previous.index + 1 &&
          static_cast<long double>(sample.timestamp) - previous.timestamp <=
              kMaxGapMs;
    }
    m_fixes.push_back({static_cast<double>(index), sample.timestamp,
                       sample.coordinates, m_currentSegment});
    return redraw;
  }

  /**
   * @brief Estimate horizontal position between nearby recorded coordinate
   * changes.
   * @note Files lack GNSS fix timestamps. Coordinate changes are a conservative
   * fallback: holds over three seconds, interruptions and the open live tail
   * remain raw. This is interpolation, not a navigation position estimate.
   */
  [[nodiscard]] Coordinates coordinateAt(int index,
                                         const FlightSample &sample) const {
    const auto next = std::lower_bound(
        m_fixes.begin(), m_fixes.end(), index,
        [](const Fix &fix, int value) { return fix.index < value; });
    if (!valid(sample))
      return sample.coordinates;
    if (next != m_fixes.end() && next->index == index)
      return next->coordinate;
    if (next == m_fixes.begin() || next == m_fixes.end())
      return sample.coordinates;
    const auto &previous = *(next - 1);
    const long double elapsed =
        static_cast<long double>(sample.timestamp) - previous.timestamp;
    const long double duration =
        static_cast<long double>(next->timestamp) - previous.timestamp;
    if (duration <= 0 || duration > kMaxGapMs || elapsed <= 0 ||
        elapsed >= duration || previous.segment != next->segment)
      return sample.coordinates;
    const double fraction = static_cast<double>(elapsed / duration);
    const double deltaLon = std::remainder(
        next->coordinate.longitude - previous.coordinate.longitude, 360.0);
    return {std::lerp(previous.coordinate.latitude, next->coordinate.latitude,
                      fraction),
            std::remainder(previous.coordinate.longitude + fraction * deltaLon,
                           360.0)};
  }

  /** @brief Return a stable segment identity so renderers cannot bridge missing
   * data. */
  [[nodiscard]] int segmentAt(int index) const {
    const auto next = std::upper_bound(
        m_fixes.begin(), m_fixes.end(), index,
        [](int value, const Fix &fix) { return value < fix.index; });
    return next == m_fixes.begin() ? 0 : (next - 1)->segment;
  }

  /** @brief Retain bracketing anchors for sorted drawn indices and the live
   * tail. */
  void retainSamples(const std::vector<int> &indices) {
    if (m_fixes.empty())
      return;
    std::vector<Fix> retained;
    const auto keep = [&](const Fix &fix) {
      if (retained.empty() || retained.back().index < fix.index)
        retained.push_back(fix);
    };
    for (const int index : indices) {
      const auto next = std::lower_bound(
          m_fixes.begin(), m_fixes.end(), index,
          [](const Fix &fix, int value) { return fix.index < value; });
      if (next != m_fixes.begin())
        keep(*(next - 1));
      if (next != m_fixes.end())
        keep(*next);
    }
    keep(m_fixes.back());
    m_fixes = std::move(retained);
  }

  /** @brief Rebase sorted retained indices, preserving fix times and open
   * intervals. */
  void rebaseIndices(const std::vector<int> &indices) {
    if (indices.empty()) {
      m_fixes.clear();
      return;
    }
    for (auto &fix : m_fixes) {
      if (indices.size() == 1) {
        fix.index -= indices.front();
        continue;
      }
      const auto next =
          std::upper_bound(indices.begin(), indices.end(), fix.index);
      const auto high = std::clamp<std::ptrdiff_t>(
          next - indices.begin(), 1,
          static_cast<std::ptrdiff_t>(indices.size()) - 1);
      const auto low = high - 1;
      fix.index = static_cast<double>(low) +
                  (fix.index - indices[low]) /
                      static_cast<double>(indices[high] - indices[low]);
    }
  }

private:
  struct Fix {
    // A retained anchor may fall between rows after live index compaction.
    double index = 0;
    long timestamp = 0;
    Coordinates coordinate;
    int segment = 0;
  };
  // ponytail: without fix-age metadata, only reconstruct short held intervals.
  static constexpr long double kMaxGapMs = 3000;
  static bool valid(const FlightSample &sample) {
    const auto &c = sample.coordinates;
    return std::isfinite(c.latitude) && std::isfinite(c.longitude) &&
           std::abs(c.latitude) <= 90 && std::abs(c.longitude) <= 180 &&
           (std::abs(c.latitude) >= 1e-9 || std::abs(c.longitude) >= 1e-9) &&
           std::isfinite(sample.altitude) &&
           std::abs(sample.altitude) <= 10'000'000;
  }
  std::vector<Fix> m_fixes;
  long m_lastTimestamp = 0;
  int m_currentSegment = 0;
  bool m_interrupted = true;
};
} // namespace cosmo::preview
