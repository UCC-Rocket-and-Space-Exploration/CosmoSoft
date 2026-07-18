/**
 * @file LineTelemetryBatchMailbox.h
 * @brief Thread-safe bounded handoff for decoded live-telemetry batches.
 */

#ifndef COSMO_SOFT_LINETELEMETRYBATCHMAILBOX_H
#define COSMO_SOFT_LINETELEMETRYBATCHMAILBOX_H

#include "services/telemetry/LineTelemetryDecodeWorker.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

namespace cosmo::telemetry {

/** @brief Outcome of a generation-specific mailbox drain. */
enum class LineTelemetryMailboxTakeStatus : std::uint8_t {
    /** The requested generation was active and one or more batches were returned. */
    Drained,
    /** The requested generation was active but had no queued batches. */
    Empty,
    /** No producer generation has reached the mailbox yet. */
    NoActiveGeneration,
    /** Another generation is active; its scheduling state was left untouched. */
    StaleGeneration,
};

/** @brief Batches and overflow information returned by a mailbox drain. */
struct LineTelemetryMailboxTakeResult {
    /** Batches retained for the requested generation, in producer order. */
    std::vector<LineTelemetryBatch> batches;

    /** Number of decoded samples discarded when the bounded mailbox overflowed. */
    std::size_t discardedDecodedSamples = 0;

    /** Result classification, including explicit stale-generation detection. */
    LineTelemetryMailboxTakeStatus status =
        LineTelemetryMailboxTakeStatus::NoActiveGeneration;
};

/**
 * @class LineTelemetryBatchMailbox
 * @brief Coalesces worker-to-GUI delivery through one bounded, scheduled drain.
 *
 * Generations must increase monotonically. push() may be called by producer
 * threads, while take() is intended for the GUI thread. A true push() result
 * asks the caller to schedule one GUI drain. Further pushes for that generation
 * return false until take() atomically drains the queue and resets that flag.
 *
 * On capacity overflow, decoded samples from the oldest batch are discarded.
 * Its received-byte, malformed-line, dropped-chunk, and dropped-byte counters
 * are merged into the incoming batch so those diagnostics remain observable.
 * Producers are expected to honor LineTelemetryWorkerConfig::maxBatchSamples;
 * the default worker and mailbox bounds retain at most 8,192 decoded samples.
 */
class LineTelemetryBatchMailbox {
public:
    /** @brief Default maximum number of retained worker batches. */
    static constexpr std::size_t kDefaultMaxQueuedBatches = 128;

    /**
     * @brief Construct an empty mailbox.
     * @param maxQueuedBatches Maximum retained batches; zero is normalised to one.
     */
    explicit LineTelemetryBatchMailbox(
        std::size_t maxQueuedBatches = kDefaultMaxQueuedBatches);

    /** @brief Mailboxes cannot be copied or moved because they own synchronization state. */
    LineTelemetryBatchMailbox(const LineTelemetryBatchMailbox &) = delete;
    LineTelemetryBatchMailbox &operator=(const LineTelemetryBatchMailbox &) = delete;
    LineTelemetryBatchMailbox(LineTelemetryBatchMailbox &&) = delete;
    LineTelemetryBatchMailbox &operator=(LineTelemetryBatchMailbox &&) = delete;

    /**
     * @brief Add a decoded batch for a monotonically increasing live generation.
     * @param generation Generation captured when the decoder worker was created.
     * @param batch Worker output to enqueue.
     * @return true only when the caller must schedule a GUI drain.
     *
     * A batch older than the active generation is rejected. A newer generation
     * replaces any undrained older-generation data and owns a fresh scheduling
     * flag, so a late drain for the old generation cannot suppress its delivery.
     */
    [[nodiscard]] bool push(std::uint64_t generation, LineTelemetryBatch batch);

    /**
     * @brief Atomically drain all batches belonging to one generation.
     * @param generation Generation accepted by the GUI consumer.
     * @return Retained batches, capacity-discard count, and generation status.
     *
     * A matching drain resets the scheduled flag while holding the same mutex
     * used by push(). A concurrent push is therefore either included in this
     * result or observes the reset flag and requests another GUI drain. A stale
     * drain never changes the active generation's flag.
     */
    [[nodiscard]] LineTelemetryMailboxTakeResult take(std::uint64_t generation);

    /** @brief Return the configured maximum number of retained batches. */
    [[nodiscard]] std::size_t capacity() const noexcept;

private:
    mutable std::mutex m_mutex;
    std::deque<LineTelemetryBatch> m_batches;
    const std::size_t m_maxQueuedBatches;
    std::size_t m_discardedDecodedSamples = 0;
    std::uint64_t m_activeGeneration = 0;
    bool m_hasActiveGeneration = false;
    bool m_drainScheduled = false;
};

} // namespace cosmo::telemetry

#endif // COSMO_SOFT_LINETELEMETRYBATCHMAILBOX_H
