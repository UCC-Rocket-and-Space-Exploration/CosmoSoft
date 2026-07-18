#ifndef COSMO_SOFT_FLIGHTPREVIEWCACHE_H
#define COSMO_SOFT_FLIGHTPREVIEWCACHE_H

#include "domain/FlightSample.h"
#include "domain/FlightSession.h"
#include "services/Cancellation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cosmo::preview {

/**
 * @brief Precomputed, immutable data used to render large flight logs cheaply.
 */
class FlightPreviewCache {
public:
    static constexpr int kMetricCount = 9;
    static constexpr int kMap2DPointBudget = 10000;
    static constexpr int kMap3DPointBudget = 5000;

    struct Range {
        int begin = 0;
        int end = 0;
    };

    /** @brief Build a preview cache for @p session without modifying raw samples. */
    [[nodiscard]] static std::shared_ptr<const FlightPreviewCache> build(const FlightSession &session)
    {
        return build(session, cosmo::CancellationCheck{});
    }

    /**
     * @brief Build a preview cache while cooperatively observing cancellation.
     * @param session Raw flight session that remains unmodified.
     * @param cancellation_check Callback polled throughout preprocessing loops.
     * @return Immutable preview cache, or nullptr when cancellation was requested.
     */
    [[nodiscard]] static std::shared_ptr<const FlightPreviewCache> build(
        const FlightSession &session,
        const cosmo::CancellationCheck &cancellation_check)
    {
        cosmo::detail::CancellationState cancellation(cancellation_check);
        if (cancellation.poll()) {
            return {};
        }
        auto cache = std::shared_ptr<FlightPreviewCache>(new FlightPreviewCache());
        if (!cache->buildTimeline(session.samples, cancellation)
            || !cache->buildMetricRanges(session.samples, cancellation)
            || !cache->buildGpsIndices(session.samples, cancellation)
            || cancellation.poll()) {
            return {};
        }
        return cache;
    }

    /** @brief Returns corrected monotonic display seconds for every raw sample. */
    [[nodiscard]] const std::vector<double> &displaySeconds() const { return m_displaySeconds; }

    /** @brief Returns corrected display seconds for @p sampleIndex, clamped to valid range. */
    [[nodiscard]] double displaySecondAt(int sampleIndex) const
    {
        if (m_displaySeconds.empty()) {
            return 0.0;
        }
        const int idx = std::clamp(sampleIndex, 0, static_cast<int>(m_displaySeconds.size()) - 1);
        return m_displaySeconds[static_cast<std::size_t>(idx)];
    }

    /** @brief Returns total corrected session duration in seconds. */
    [[nodiscard]] double durationSeconds() const
    {
        return m_displaySeconds.empty() ? 0.0 : m_displaySeconds.back();
    }

    /** @brief Returns the number of non-positive or non-finite raw timestamp deltas. */
    [[nodiscard]] int timestampDiscontinuityCount() const { return m_timestampDiscontinuityCount; }

    /** @brief Returns true when the display timeline differs from raw timestamps. */
    [[nodiscard]] bool correctedTimelineUsed() const { return m_correctedTimelineUsed; }

    /** @brief Median positive raw timestamp delta used to replace invalid jumps. */
    [[nodiscard]] long medianPositiveDeltaMs() const { return m_medianPositiveDeltaMs; }

    /** @brief Returns how many GPS rows were ignored as placeholders or invalid coordinates. */
    [[nodiscard]] int droppedGpsRows() const { return m_droppedGpsRows; }

    /** @brief Returns valid GPS sample indices reduced for 2D map drawing. */
    [[nodiscard]] const std::vector<int> &map2DIndices() const { return m_map2DIndices; }

    /** @brief Returns valid GPS sample indices reduced for 3D/drop-line drawing. */
    [[nodiscard]] const std::vector<int> &map3DIndices() const { return m_map3DIndices; }

    /** @brief Returns true when @p sampleIndex has a coordinate that should be shown on the map. */
    [[nodiscard]] bool gpsSampleValid(int sampleIndex) const
    {
        return std::binary_search(m_validGpsIndices.begin(), m_validGpsIndices.end(), sampleIndex);
    }

    /** @brief Returns per-metric global min and max values. */
    [[nodiscard]] std::pair<double, double> metricRange(int metricIndex) const
    {
        if (metricIndex < 0 || metricIndex >= kMetricCount) {
            return {0.0, 0.0};
        }
        return m_metricRanges[static_cast<std::size_t>(metricIndex)];
    }

    /**
     * @brief Converts a visible X-axis time range to raw sample indices.
     *
     * @p endLimit lets replay restrict the search to the currently visible trail.
     */
    [[nodiscard]] Range visibleRange(double minSeconds, double maxSeconds, int endLimit) const
    {
        const int n = static_cast<int>(m_displaySeconds.size());
        const int cappedEnd = std::clamp(endLimit, 0, n);
        if (cappedEnd <= 0) {
            return {};
        }
        if (!std::isfinite(minSeconds) || !std::isfinite(maxSeconds) || minSeconds > maxSeconds) {
            return {0, cappedEnd};
        }

        const auto beginIt = m_displaySeconds.begin();
        const auto endIt = beginIt + cappedEnd;
        int begin = static_cast<int>(std::lower_bound(beginIt, endIt, minSeconds) - beginIt);
        int end = static_cast<int>(std::upper_bound(beginIt, endIt, maxSeconds) - beginIt);
        begin = std::clamp(begin, 0, cappedEnd);
        end = std::clamp(end, begin, cappedEnd);
        if (begin == end && cappedEnd > 0) {
            begin = std::clamp(begin - 1, 0, cappedEnd - 1);
            end = begin + 1;
        }
        return {begin, end};
    }

    /** @brief Finds the raw sample nearest to corrected display time @p seconds. */
    [[nodiscard]] int sampleIndexForDisplaySecond(double seconds) const
    {
        if (m_displaySeconds.empty()) {
            return 0;
        }
        const auto it = std::lower_bound(m_displaySeconds.begin(), m_displaySeconds.end(), seconds);
        if (it == m_displaySeconds.begin()) {
            return 0;
        }
        if (it == m_displaySeconds.end()) {
            return static_cast<int>(m_displaySeconds.size()) - 1;
        }
        const int hi = static_cast<int>(it - m_displaySeconds.begin());
        const int lo = hi - 1;
        return std::abs(m_displaySeconds[static_cast<std::size_t>(lo)] - seconds)
             <= std::abs(m_displaySeconds[static_cast<std::size_t>(hi)] - seconds) ? lo : hi;
    }

    /**
     * @brief Returns raw sample indices for chart rendering in [begin, end).
     *
     * The returned list is exact if the range fits the point budget; otherwise
     * LTTB downsampling preserves visual spikes across all enabled metrics.
     */
    [[nodiscard]] std::vector<int> chartIndices(
        const FlightSession &session,
        int begin,
        int end,
        int maxPoints,
        const std::array<bool, kMetricCount> &enabled) const
    {
        const int n = static_cast<int>(session.samples.size());
        begin = std::clamp(begin, 0, n);
        end = std::clamp(end, begin, n);
        maxPoints = std::max(1, maxPoints);

        const ChartKey key{begin, end, maxPoints, metricMask(enabled)};
        {
            std::lock_guard<std::mutex> lock(m_chartCacheMutex);
            if (const auto it = m_chartIndexCache.find(key); it != m_chartIndexCache.end()) {
                return it->second;
            }
        }

        std::vector<int> indices = buildChartIndices(session.samples, begin, end, maxPoints, enabled);
        {
            std::lock_guard<std::mutex> lock(m_chartCacheMutex);
            if (m_chartIndexCache.size() >= kMaxCachedChartSelections) {
                m_chartIndexCache.clear();
            }
            m_chartIndexCache.emplace(key, indices);
        }
        return indices;
    }

    /** @brief Returns the replay preview bucket for @p trailLength and @p maxPoints. */
    [[nodiscard]] int replayBucket(int trailLength, int maxPoints) const
    {
        const int n = static_cast<int>(m_displaySeconds.size());
        const int head = std::clamp(trailLength, 0, n);
        if (head <= maxPoints) {
            return head;
        }
        const int stride = std::max(1, (n + std::max(1, maxPoints) - 1) / std::max(1, maxPoints));
        return (head - 1) / stride;
    }

private:
    struct ChartKey {
        int begin = 0;
        int end = 0;
        int maxPoints = 0;
        std::uint16_t metricMask = 0;

        [[nodiscard]] bool operator==(const ChartKey &other) const
        {
            return begin == other.begin
                && end == other.end
                && maxPoints == other.maxPoints
                && metricMask == other.metricMask;
        }
    };

    struct ChartKeyHash {
        [[nodiscard]] std::size_t operator()(const ChartKey &key) const
        {
            std::size_t h = static_cast<std::size_t>(key.begin);
            h = h * 1315423911u + static_cast<std::size_t>(key.end);
            h = h * 2654435761u + static_cast<std::size_t>(key.maxPoints);
            h = h * 16777619u + static_cast<std::size_t>(key.metricMask);
            return h;
        }
    };

    static constexpr std::size_t kMaxCachedChartSelections = 64;

    FlightPreviewCache() = default;

    static bool finiteCoordinate(double lat, double lon)
    {
        return std::isfinite(lat) && std::isfinite(lon)
            && std::abs(lat) <= 90.0 && std::abs(lon) <= 180.0;
    }

    static bool zeroCoordinate(double lat, double lon)
    {
        return std::abs(lat) < 1e-9 && std::abs(lon) < 1e-9;
    }

    static std::uint16_t metricMask(const std::array<bool, kMetricCount> &enabled)
    {
        std::uint16_t mask = 0;
        for (int i = 0; i < kMetricCount; ++i) {
            if (enabled[static_cast<std::size_t>(i)]) {
                mask |= static_cast<std::uint16_t>(1u << i);
            }
        }
        return mask;
    }

    static double sampleValueForMetric(const FlightSample &s, int idx)
    {
        switch (idx) {
        case 0: return s.altitude;
        case 1: return s.temperature;
        case 2: return s.pressure;
        case 3: {
            const double x = s.acceleration.x, y = s.acceleration.y, z = s.acceleration.z;
            return std::hypot(x, y, z);
        }
        case 4: return s.batteryVoltage;
        case 5: return s.rssi;
        case 6: {
            const double x = s.angularVelocity.x, y = s.angularVelocity.y, z = s.angularVelocity.z;
            return std::hypot(x, y, z);
        }
        case 7: return s.coordinates.latitude;
        case 8: return s.coordinates.longitude;
        default: return 0.0;
        }
    }

    [[nodiscard]] bool buildTimeline(
        const std::vector<FlightSample> &samples,
        cosmo::detail::CancellationState &cancellation)
    {
        m_displaySeconds.clear();
        m_displaySeconds.reserve(samples.size());
        if (samples.empty()) {
            return !cancellation.poll();
        }

        std::vector<long> positiveDeltas;
        positiveDeltas.reserve(samples.size());
        for (std::size_t i = 1; i < samples.size(); ++i) {
            if (cancellation.poll_periodically()) {
                return false;
            }
            const long double rawDelta = static_cast<long double>(samples[i].timestamp)
                - static_cast<long double>(samples[i - 1].timestamp);
            const long double maximum = static_cast<long double>(
                std::numeric_limits<long>::max());
            if (rawDelta > 0.0L && rawDelta <= maximum) {
                positiveDeltas.push_back(static_cast<long>(rawDelta));
            }
        }

        if (!positiveDeltas.empty()) {
            const auto mid = positiveDeltas.begin() + static_cast<std::ptrdiff_t>(positiveDeltas.size() / 2);
            std::nth_element(positiveDeltas.begin(), mid, positiveDeltas.end());
            m_medianPositiveDeltaMs = std::max<long>(1, *mid);
        } else {
            m_medianPositiveDeltaMs = 1;
        }
        if (cancellation.poll()) {
            return false;
        }

        m_displaySeconds.push_back(0.0);
        long double elapsedMs = 0.0L;
        for (std::size_t i = 1; i < samples.size(); ++i) {
            if (cancellation.poll_periodically()) {
                return false;
            }
            const long double rawDelta = static_cast<long double>(samples[i].timestamp)
                - static_cast<long double>(samples[i - 1].timestamp);
            const long double maximum = static_cast<long double>(
                std::numeric_limits<long>::max());
            long double displayDelta = rawDelta;
            if (rawDelta <= 0.0L || rawDelta > maximum) {
                displayDelta = static_cast<long double>(m_medianPositiveDeltaMs);
                ++m_timestampDiscontinuityCount;
                m_correctedTimelineUsed = true;
            }
            elapsedMs += displayDelta;
            m_displaySeconds.push_back(static_cast<double>(elapsedMs / 1000.0L));
        }
        return !cancellation.poll();
    }

    [[nodiscard]] bool buildMetricRanges(
        const std::vector<FlightSample> &samples,
        cosmo::detail::CancellationState &cancellation)
    {
        for (auto &range : m_metricRanges) {
            range = {std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
        }
        for (const FlightSample &sample : samples) {
            if (cancellation.poll_periodically()) {
                return false;
            }
            for (int mi = 0; mi < kMetricCount; ++mi) {
                const double value = sampleValueForMetric(sample, mi);
                if (!std::isfinite(value)) {
                    continue;
                }
                auto &range = m_metricRanges[static_cast<std::size_t>(mi)];
                range.first = std::min(range.first, value);
                range.second = std::max(range.second, value);
            }
        }
        for (auto &range : m_metricRanges) {
            if (!std::isfinite(range.first) || !std::isfinite(range.second)) {
                range = {0.0, 0.0};
            }
        }
        return !cancellation.poll();
    }

    [[nodiscard]] bool buildGpsIndices(
        const std::vector<FlightSample> &samples,
        cosmo::detail::CancellationState &cancellation)
    {
        bool hasRealCoordinate = false;
        for (const FlightSample &sample : samples) {
            if (cancellation.poll_periodically()) {
                return false;
            }
            const double lat = sample.coordinates.latitude;
            const double lon = sample.coordinates.longitude;
            if (finiteCoordinate(lat, lon) && !zeroCoordinate(lat, lon)) {
                hasRealCoordinate = true;
                break;
            }
        }

        for (int i = 0; i < static_cast<int>(samples.size()); ++i) {
            if (cancellation.poll_periodically()) {
                return false;
            }
            const FlightSample &sample = samples[static_cast<std::size_t>(i)];
            const double lat = sample.coordinates.latitude;
            const double lon = sample.coordinates.longitude;
            const bool valid = finiteCoordinate(lat, lon)
                && (!hasRealCoordinate || !zeroCoordinate(lat, lon));
            if (valid) {
                m_validGpsIndices.push_back(i);
            } else {
                ++m_droppedGpsRows;
            }
        }

        m_map2DIndices = cappedIndices(m_validGpsIndices, kMap2DPointBudget);
        m_map3DIndices = cappedIndices(m_validGpsIndices, kMap3DPointBudget);
        return !cancellation.poll();
    }

    static std::vector<int> cappedIndices(const std::vector<int> &source, int maxPoints)
    {
        if (source.size() <= static_cast<std::size_t>(maxPoints)) {
            return source;
        }
        std::vector<int> out;
        out.reserve(static_cast<std::size_t>(maxPoints));
        const long long denom = maxPoints - 1;
        for (int i = 0; i < maxPoints; ++i) {
            const auto sourceIndex = static_cast<std::size_t>(
                (static_cast<long long>(i) * static_cast<long long>(source.size() - 1)) / denom);
            out.push_back(source[sourceIndex]);
        }
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }

    std::vector<int> buildChartIndices(
        const std::vector<FlightSample> &samples,
        int begin,
        int end,
        int maxPoints,
        const std::array<bool, kMetricCount> &enabled) const
    {
        const int count = end - begin;
        std::vector<int> idx;
        if (count <= 0 || maxPoints <= 0) {
            return idx;
        }
        if (count <= maxPoints) {
            idx.resize(static_cast<std::size_t>(count));
            std::iota(idx.begin(), idx.end(), begin);
            return idx;
        }
        if (maxPoints <= 2) {
            idx.push_back(begin);
            if (count > 1) {
                idx.push_back(end - 1);
            }
            return idx;
        }

        std::vector<int> enabledMetrics;
        for (int mi = 0; mi < kMetricCount; ++mi) {
            if (enabled[static_cast<std::size_t>(mi)]) {
                enabledMetrics.push_back(mi);
            }
        }
        if (enabledMetrics.empty()) {
            return cappedContiguousIndices(begin, end, maxPoints);
        }

        const auto metricCount = enabledMetrics.size();
        std::vector<double> metricMin(metricCount, std::numeric_limits<double>::infinity());
        std::vector<double> metricMax(metricCount, -std::numeric_limits<double>::infinity());
        for (int i = begin; i < end; ++i) {
            const FlightSample &sample = samples[static_cast<std::size_t>(i)];
            for (std::size_t m = 0; m < metricCount; ++m) {
                const double value = sampleValueForMetric(sample, enabledMetrics[m]);
                metricMin[m] = std::min(metricMin[m], value);
                metricMax[m] = std::max(metricMax[m], value);
            }
        }

        std::vector<double> metricScale(metricCount, 1.0);
        for (std::size_t m = 0; m < metricCount; ++m) {
            const double span = metricMax[m] - metricMin[m];
            metricScale[m] = span > 1e-15 ? 1.0 / span : 1.0;
        }

        idx.reserve(static_cast<std::size_t>(maxPoints));
        idx.push_back(begin);
        const int nBuckets = maxPoints - 2;
        const double bucketSize = static_cast<double>(count - 2) / static_cast<double>(nBuckets);
        int previousSelected = begin;

        for (int bucket = 0; bucket < nBuckets; ++bucket) {
            const int bucketStart = begin + static_cast<int>(std::floor(1.0 + bucket * bucketSize));
            const int bucketEnd = std::min(
                begin + static_cast<int>(std::floor(1.0 + (bucket + 1) * bucketSize)),
                end - 1);
            if (bucketStart >= bucketEnd) {
                const int selected = std::clamp(bucketStart, begin, end - 1);
                idx.push_back(selected);
                previousSelected = selected;
                continue;
            }

            const int nextBucketStart = bucketEnd;
            const int nextBucketEnd = (bucket + 1 < nBuckets)
                ? std::min(begin + static_cast<int>(std::floor(1.0 + (bucket + 2) * bucketSize)), end - 1)
                : end;
            const int nextBucketCount = std::max(nextBucketEnd - nextBucketStart, 1);

            double avgX = 0.0;
            std::vector<double> avgY(metricCount, 0.0);
            for (int i = nextBucketStart; i < nextBucketEnd; ++i) {
                const FlightSample &sample = samples[static_cast<std::size_t>(i)];
                avgX += m_displaySeconds[static_cast<std::size_t>(i)];
                for (std::size_t m = 0; m < metricCount; ++m) {
                    avgY[m] += (sampleValueForMetric(sample, enabledMetrics[m]) - metricMin[m]) * metricScale[m];
                }
            }
            avgX /= static_cast<double>(nextBucketCount);
            for (double &value : avgY) {
                value /= static_cast<double>(nextBucketCount);
            }

            const FlightSample &previousSample = samples[static_cast<std::size_t>(previousSelected)];
            const double previousX = m_displaySeconds[static_cast<std::size_t>(previousSelected)];
            int bestIndex = bucketStart;
            double bestArea = -1.0;

            for (int i = bucketStart; i < bucketEnd; ++i) {
                const FlightSample &sample = samples[static_cast<std::size_t>(i)];
                const double currentX = m_displaySeconds[static_cast<std::size_t>(i)];
                double maxArea = 0.0;
                for (std::size_t m = 0; m < metricCount; ++m) {
                    const int metric = enabledMetrics[m];
                    const double previousY = (sampleValueForMetric(previousSample, metric) - metricMin[m]) * metricScale[m];
                    const double currentY = (sampleValueForMetric(sample, metric) - metricMin[m]) * metricScale[m];
                    const double area = std::abs(
                        (previousX - avgX) * (currentY - previousY)
                        - (previousX - currentX) * (avgY[m] - previousY));
                    maxArea = std::max(maxArea, area);
                }
                if (maxArea > bestArea) {
                    bestArea = maxArea;
                    bestIndex = i;
                }
            }
            idx.push_back(bestIndex);
            previousSelected = bestIndex;
        }

        idx.push_back(end - 1);
        idx.erase(std::unique(idx.begin(), idx.end()), idx.end());
        return idx;
    }

    static std::vector<int> cappedContiguousIndices(int begin, int end, int maxPoints)
    {
        std::vector<int> out;
        out.reserve(static_cast<std::size_t>(maxPoints));
        const long long denom = maxPoints - 1;
        for (int i = 0; i < maxPoints; ++i) {
            out.push_back(begin + static_cast<int>(
                (static_cast<long long>(i) * static_cast<long long>(end - begin - 1)) / denom));
        }
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }

    std::vector<double> m_displaySeconds;
    std::array<std::pair<double, double>, kMetricCount> m_metricRanges{};
    std::vector<int> m_validGpsIndices;
    std::vector<int> m_map2DIndices;
    std::vector<int> m_map3DIndices;
    long m_medianPositiveDeltaMs = 1;
    int m_timestampDiscontinuityCount = 0;
    int m_droppedGpsRows = 0;
    bool m_correctedTimelineUsed = false;

    mutable std::mutex m_chartCacheMutex;
    mutable std::unordered_map<ChartKey, std::vector<int>, ChartKeyHash> m_chartIndexCache;
};

} // namespace cosmo::preview

#endif // COSMO_SOFT_FLIGHTPREVIEWCACHE_H
