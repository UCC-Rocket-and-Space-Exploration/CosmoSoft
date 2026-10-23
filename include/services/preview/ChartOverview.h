#pragma once

#include "domain/FlightSample.h"
#include "services/Cancellation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

namespace cosmo::preview {

/** @brief Bounded, peak-preserving selections with optional immutable block
 * summaries. */
class ChartOverview {
public:
  static constexpr int kMetricCount = 9;

  /** @brief Build range summaries off the GUI thread, observing cancellation.
   */
  bool build(const std::vector<FlightSample> &samples,
             cosmo::detail::CancellationState &cancellation) {
    const int count = boundedSize(samples);
    m_leafCount = 1;
    const int blocks = count / kBlockSize + (count % kBlockSize != 0);
    while (m_leafCount < blocks)
      m_leafCount *= 2;
    m_nodes.assign(static_cast<std::size_t>(2) * m_leafCount, {});
    for (int i = 0; i < count; ++i) {
      if (cancellation.poll_periodically())
        return false;
      auto &node = m_nodes[m_leafCount + i / kBlockSize];
      for (int metric = 0; metric < kMetricCount; ++metric)
        include(node[metric], samples, metric, i);
    }
    for (int node = m_leafCount - 1; node > 0; --node) {
      if (cancellation.poll_periodically())
        return false;
      for (int metric = 0; metric < kMetricCount; ++metric)
        m_nodes[node][metric] =
            merge(m_nodes[node * 2][metric], m_nodes[node * 2 + 1][metric],
                  samples, metric);
    }
    return !cancellation.poll();
  }

  /**
   * @brief Select original extrema and representative gap boundaries within a
   * strict budget. Dense missing intervals are reduced; renderers must use
   * hasMissing() to avoid bridging them. A default-constructed overview scans
   * raw rows; a built overview queries block summaries.
   */
  [[nodiscard]] std::vector<int>
  select(const std::vector<FlightSample> &samples, int begin, int end,
         int budget, const std::array<bool, kMetricCount> &enabled) const {
    begin = std::clamp(begin, 0, boundedSize(samples));
    end = std::clamp(end, begin, boundedSize(samples));
    if (budget <= 0 || begin == end)
      return {};
    const int count = end - begin;
    if (count <= budget) {
      std::vector<int> indices(count);
      std::iota(indices.begin(), indices.end(), begin);
      return indices;
    }
    if (budget == 1)
      return {begin};
    const int metrics =
        static_cast<int>(std::count(enabled.begin(), enabled.end(), true));
    const int pointsPerBucket = 2 + 6 * metrics;
    const int buckets = std::max(1, budget / pointsPerBucket);
    std::vector<int> indices;
    indices.reserve(std::min(budget, count));
    for (int bucket = 0; bucket < buckets; ++bucket) {
      const int first =
          begin +
          static_cast<int>(static_cast<long long>(bucket) * count / buckets);
      const int last =
          begin + static_cast<int>(static_cast<long long>(bucket + 1) * count /
                                   buckets);
      indices.push_back(first);
      indices.push_back(last - 1);
      for (int metric = 0; metric < kMetricCount; ++metric) {
        if (!enabled[metric])
          continue;
        const auto range = query(samples, metric, first, last);
        if (range.low >= 0) {
          indices.push_back(range.low);
          indices.push_back(range.high);
        }
        if (range.firstMissing >= 0) {
          indices.push_back(std::max(first, range.firstMissing - 1));
          indices.push_back(range.firstMissing);
          indices.push_back(range.lastMissing);
          indices.push_back(std::min(last - 1, range.lastMissing + 1));
        }
      }
    }
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    if (indices.size() > static_cast<std::size_t>(budget)) {
      // Tiny budgets cannot retain every enabled metric's extrema and gaps.
      std::vector<int> reduced;
      reduced.reserve(budget);
      for (int i = 0; i < budget; ++i)
        reduced.push_back(indices[static_cast<std::size_t>(i) *
                                  (indices.size() - 1) / (budget - 1)]);
      return reduced;
    }
    return indices;
  }

  /** @brief Return whether a metric has a missing value in the half-open raw
   * range. */
  [[nodiscard]] bool hasMissing(const std::vector<FlightSample> &samples,
                                int metric, int begin, int end) const {
    if (metric < 0 || metric >= kMetricCount)
      return true;
    begin = std::clamp(begin, 0, boundedSize(samples));
    end = std::clamp(end, begin, boundedSize(samples));
    if (m_nodes.empty()) {
      for (int i = begin; i < end; ++i)
        if (!std::isfinite(sampleValue(samples[i], metric)))
          return true;
      return false;
    }
    while (begin < end && begin % kBlockSize != 0)
      if (!std::isfinite(sampleValue(samples[begin++], metric)))
        return true;
    while (begin < end && end % kBlockSize != 0)
      if (!std::isfinite(sampleValue(samples[--end], metric)))
        return true;
    int left = begin / kBlockSize + m_leafCount;
    int right = end / kBlockSize + m_leafCount;
    while (left < right) {
      if (left % 2 != 0 && m_nodes[left++][metric].firstMissing >= 0)
        return true;
      if (right % 2 != 0 && m_nodes[--right][metric].firstMissing >= 0)
        return true;
      left /= 2;
      right /= 2;
    }
    return false;
  }

  /** @brief Extract the SI scalar represented by a chart metric. */
  [[nodiscard]] static double sampleValue(const FlightSample &sample,
                                          int metric) {
    switch (metric) {
    case 0:
      return sample.altitude;
    case 1:
      return sample.temperature;
    case 2:
      return sample.pressure;
    case 3:
      return std::hypot(sample.acceleration.x, sample.acceleration.y,
                        sample.acceleration.z);
    case 4:
      return sample.batteryVoltage;
    case 5:
      return sample.rssi;
    case 6:
      return std::hypot(sample.angularVelocity.x, sample.angularVelocity.y,
                        sample.angularVelocity.z);
    case 7:
      return sample.coordinates.latitude;
    case 8:
      return sample.coordinates.longitude;
    default:
      return std::numeric_limits<double>::quiet_NaN();
    }
  }

private:
  struct Summary {
    int low = -1;
    int high = -1;
    int firstMissing = -1;
    int lastMissing = -1;
  };
  static constexpr int kBlockSize = 64;
  int m_leafCount = 0;
  std::vector<std::array<Summary, kMetricCount>> m_nodes;

  static int boundedSize(const std::vector<FlightSample> &samples) {
    return static_cast<int>(
        std::min(samples.size(),
                 static_cast<std::size_t>(std::numeric_limits<int>::max())));
  }

  static void include(Summary &summary,
                      const std::vector<FlightSample> &samples, int metric,
                      int index) {
    const double value = sampleValue(samples[index], metric);
    if (!std::isfinite(value)) {
      if (summary.firstMissing < 0)
        summary.firstMissing = index;
      summary.lastMissing = index;
    } else {
      if (summary.low < 0 || value < sampleValue(samples[summary.low], metric))
        summary.low = index;
      if (summary.high < 0 ||
          value > sampleValue(samples[summary.high], metric))
        summary.high = index;
    }
  }

  static Summary merge(const Summary &left, const Summary &right,
                       const std::vector<FlightSample> &samples, int metric) {
    Summary result = left;
    if (right.low >= 0 &&
        (result.low < 0 || sampleValue(samples[right.low], metric) <
                               sampleValue(samples[result.low], metric)))
      result.low = right.low;
    if (right.high >= 0 &&
        (result.high < 0 || sampleValue(samples[right.high], metric) >
                                sampleValue(samples[result.high], metric)))
      result.high = right.high;
    if (right.firstMissing >= 0) {
      if (result.firstMissing < 0 || right.firstMissing < result.firstMissing)
        result.firstMissing = right.firstMissing;
      result.lastMissing = std::max(result.lastMissing, right.lastMissing);
    }
    return result;
  }

  Summary query(const std::vector<FlightSample> &samples, int metric, int begin,
                int end) const {
    Summary result;
    if (m_nodes.empty()) {
      for (int i = begin; i < end; ++i)
        include(result, samples, metric, i);
      return result;
    }
    while (begin < end && begin % kBlockSize != 0)
      include(result, samples, metric, begin++);
    while (begin < end && end % kBlockSize != 0) {
      Summary tail;
      include(tail, samples, metric, --end);
      result = merge(result, tail, samples, metric);
    }
    int left = begin / kBlockSize + m_leafCount;
    int right = end / kBlockSize + m_leafCount;
    while (left < right) {
      if (left % 2 != 0)
        result = merge(result, m_nodes[left++][metric], samples, metric);
      if (right % 2 != 0)
        result = merge(result, m_nodes[--right][metric], samples, metric);
      left /= 2;
      right /= 2;
    }
    return result;
  }
};

} // namespace cosmo::preview
