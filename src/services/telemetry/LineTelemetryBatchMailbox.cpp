#include "services/telemetry/LineTelemetryBatchMailbox.h"

#include <algorithm>
#include <limits>
#include <type_traits>
#include <utility>

namespace cosmo::telemetry {
namespace {

template <typename T>
void saturatingAdd(T &target, T increment) noexcept {
    static_assert(std::is_unsigned_v<T>);
    const T maximum = std::numeric_limits<T>::max();
    target = increment > maximum - target ? maximum : target + increment;
}

void preserveCounters(LineTelemetryBatch &target,
                      const LineTelemetryBatch &source) noexcept {
    saturatingAdd(target.receivedBytes, source.receivedBytes);
    saturatingAdd(target.malformedLines, source.malformedLines);
    saturatingAdd(target.droppedChunks, source.droppedChunks);
    saturatingAdd(target.droppedBytes, source.droppedBytes);
}

} // namespace

LineTelemetryBatchMailbox::LineTelemetryBatchMailbox(
    std::size_t maxQueuedBatches)
    : m_maxQueuedBatches(std::max<std::size_t>(1, maxQueuedBatches)) {}

bool LineTelemetryBatchMailbox::push(std::uint64_t generation,
                                     LineTelemetryBatch batch) {
    std::scoped_lock lock(m_mutex);

    if (m_hasActiveGeneration && generation < m_activeGeneration) {
        return false;
    }

    if (!m_hasActiveGeneration || generation > m_activeGeneration) {
        m_batches.clear();
        m_discardedDecodedSamples = 0;
        m_activeGeneration = generation;
        m_hasActiveGeneration = true;
        m_drainScheduled = false;
    }

    while (m_batches.size() >= m_maxQueuedBatches) {
        const LineTelemetryBatch &evicted = m_batches.front();
        saturatingAdd(m_discardedDecodedSamples, evicted.samples.size());
        preserveCounters(batch, evicted);
        m_batches.pop_front();
    }

    m_batches.push_back(std::move(batch));
    if (m_drainScheduled) {
        return false;
    }

    m_drainScheduled = true;
    return true;
}

LineTelemetryMailboxTakeResult
LineTelemetryBatchMailbox::take(std::uint64_t generation) {
    std::scoped_lock lock(m_mutex);

    LineTelemetryMailboxTakeResult result;
    if (!m_hasActiveGeneration) {
        return result;
    }
    if (generation != m_activeGeneration) {
        result.status = LineTelemetryMailboxTakeStatus::StaleGeneration;
        return result;
    }

    result.discardedDecodedSamples = m_discardedDecodedSamples;
    result.batches.reserve(m_batches.size());
    while (!m_batches.empty()) {
        result.batches.push_back(std::move(m_batches.front()));
        m_batches.pop_front();
    }

    m_discardedDecodedSamples = 0;
    m_drainScheduled = false;
    result.status = result.batches.empty()
        ? LineTelemetryMailboxTakeStatus::Empty
        : LineTelemetryMailboxTakeStatus::Drained;
    return result;
}

std::size_t LineTelemetryBatchMailbox::capacity() const noexcept {
    return m_maxQueuedBatches;
}

} // namespace cosmo::telemetry
