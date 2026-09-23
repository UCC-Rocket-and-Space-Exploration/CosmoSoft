#pragma once

#include "gui/widgets/MetricDefs.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <vector>

namespace ChartSamples {

/**
 * @brief Select original samples for an overview, retaining each enabled metric's
 * minimum and maximum in every bucket. No values are averaged or interpolated.
 * Ranges within the display budget retain every sample. Missing-value boundaries
 * are retained as well, so rendering can break the line at missing measurements.
 */
inline std::vector<int> select(const std::vector<FlightSample> &samples, int begin, int end, int budget,
                               const std::array<bool, MetricDefs::kMetricCount> &enabled) {
    begin = std::clamp(begin, 0, static_cast<int>(samples.size()));
    end = std::clamp(end, begin, static_cast<int>(samples.size()));
    const int count = end - begin;
    std::vector<int> indices;
    if (count <= std::max(2, budget)) {
        indices.resize(count);
        std::iota(indices.begin(), indices.end(), begin);
        return indices;
    }
    const int metrics = static_cast<int>(std::count(enabled.begin(), enabled.end(), true));
    const int buckets = std::max(1, budget / (2 + 2 * metrics));
    for (int bucket = 0; bucket < buckets; ++bucket) {
        const int first = begin + static_cast<int>(static_cast<long long>(bucket) * count / buckets);
        const int last = begin + static_cast<int>(static_cast<long long>(bucket + 1) * count / buckets);
        indices.push_back(first);
        indices.push_back(last - 1);
        for (int metric = 0; metric < MetricDefs::kMetricCount; ++metric) {
            if (!enabled[metric]) continue;
            int low = -1, high = -1;
            for (int i = first; i < last; ++i) {
                const double value = MetricDefs::sampleValueForMetric(samples[i], metric);
                if (i > begin &&
                    std::isfinite(value) != std::isfinite(MetricDefs::sampleValueForMetric(samples[i - 1], metric))) {
                    indices.push_back(i - 1);
                    indices.push_back(i);
                }
                if (!std::isfinite(value)) continue;
                if (low < 0 || value < MetricDefs::sampleValueForMetric(samples[low], metric)) low = i;
                if (high < 0 || value > MetricDefs::sampleValueForMetric(samples[high], metric)) high = i;
            }
            if (low >= 0) indices.push_back(low);
            if (high >= 0) indices.push_back(high);
        }
    }
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    return indices;
}

} // namespace ChartSamples
