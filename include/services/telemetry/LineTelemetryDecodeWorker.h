/**
 * @file LineTelemetryDecodeWorker.h
 * @brief Bounded background worker for newline-delimited live telemetry.
 */

#ifndef COSMO_SOFT_LINETELEMETRYDECODEWORKER_H
#define COSMO_SOFT_LINETELEMETRYDECODEWORKER_H

#include "domain/FlightSample.h"
#include "services/telemetry/LineTelemetryDecoder.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace cosmo::telemetry {

/** @brief Limits and batching policy for LineTelemetryDecodeWorker. */
struct LineTelemetryWorkerConfig {
    std::size_t maxQueuedChunks = 512;
    std::size_t maxQueuedBytes = 2U * 1024U * 1024U;
    std::size_t maxBatchSamples = 64;
    std::chrono::milliseconds flushInterval{16};
};

/**
 * @brief One coalesced delivery from LineTelemetryDecodeWorker.
 *
 * Counter fields are deltas since the previous delivery. receivedBytes counts
 * all non-empty input presented while the worker is running, including bytes
 * rejected or later dropped by the bounded queue. droppedBytes is therefore a
 * subset of receivedBytes.
 */
struct LineTelemetryBatch {
    std::vector<FlightSample> samples;
    std::uint64_t receivedBytes = 0;
    std::size_t malformedLines = 0;
    std::size_t droppedChunks = 0;
    std::uint64_t droppedBytes = 0;
};

/**
 * @class LineTelemetryDecodeWorker
 * @brief Decodes serial chunks away from the caller thread and emits batches.
 *
 * enqueueChunk() is thread-safe. start() and stop() may be called repeatedly,
 * but must not be called concurrently with one another. The callback executes
 * on the worker thread; GUI owners must forward one complete batch through a
 * queued Qt invocation. A callback may request stop(), but must not destroy the
 * worker. stop() drains accepted chunks before returning when called externally.
 */
class LineTelemetryDecodeWorker {
public:
    using BatchCallback = std::function<void(LineTelemetryBatch)>;
    using ErrorCallback = std::function<void(const std::string &)>;

    /**
     * @brief Construct a stopped worker.
     * @param callback Called for each decoded batch on the worker thread.
     * @param config Queue bounds and flush policy; zero values are normalised to one.
     * @param errorCallback Called when the batch callback throws std::exception.
     */
    explicit LineTelemetryDecodeWorker(
        BatchCallback callback,
        LineTelemetryWorkerConfig config = {},
        ErrorCallback errorCallback = {});

    /** @brief Stop and join the worker, if needed. */
    ~LineTelemetryDecodeWorker();

    LineTelemetryDecodeWorker(const LineTelemetryDecodeWorker &) = delete;
    LineTelemetryDecodeWorker &operator=(const LineTelemetryDecodeWorker &) = delete;
    LineTelemetryDecodeWorker(LineTelemetryDecodeWorker &&) = delete;
    LineTelemetryDecodeWorker &operator=(LineTelemetryDecodeWorker &&) = delete;

    /** @brief Start background decoding; returns true if running afterward. */
    bool start();

    /** @brief Stop accepting input, drain accepted chunks, and join the worker. */
    void stop();

    /** @brief Return true while the worker accepts chunks. */
    [[nodiscard]] bool isRunning() const noexcept;

    /**
     * @brief Enqueue an owned byte chunk without blocking the producer.
     * @return true if this chunk remains queued; false if stopped or too large.
     *
     * When a bound is reached, the oldest queued chunks are discarded. Their
     * counts and byte sizes are reported in a later LineTelemetryBatch.
     */
    bool enqueueChunk(std::vector<std::uint8_t> chunk);

private:
    struct QueuedChunk {
        std::vector<std::uint8_t> bytes;
        bool gapBefore = false;
    };

    struct CounterSnapshot {
        std::uint64_t receivedBytes = 0;
        std::size_t droppedChunks = 0;
        std::uint64_t droppedBytes = 0;
    };

    void run(const std::stop_token &stopToken);
    [[nodiscard]] bool deliver(std::vector<FlightSample> &samples);
    [[nodiscard]] CounterSnapshot counterSnapshot() const;

    BatchCallback m_callback;
    ErrorCallback m_errorCallback;
    LineTelemetryWorkerConfig m_config;

    mutable std::mutex m_queueMutex;
    std::condition_variable_any m_queueChanged;
    std::deque<QueuedChunk> m_chunks;
    std::size_t m_queuedBytes = 0;
    bool m_gapBeforeNextChunk = false;
    CounterSnapshot m_counters;
    bool m_statsDirty = false;

    std::mutex m_lifecycleMutex;
    std::jthread m_thread;
    std::atomic_bool m_accepting{false};

    LineTelemetryDecoder m_decoder;
    CounterSnapshot m_lastDeliveredCounters;
    std::size_t m_lastDeliveredMalformedLines = 0;
};

} // namespace cosmo::telemetry

#endif // COSMO_SOFT_LINETELEMETRYDECODEWORKER_H
