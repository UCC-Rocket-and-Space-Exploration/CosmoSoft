#include "services/telemetry/LineTelemetryDecodeWorker.h"

#include <algorithm>
#include <exception>
#include <system_error>
#include <utility>

namespace cosmo::telemetry {

LineTelemetryDecodeWorker::LineTelemetryDecodeWorker(
    BatchCallback callback,
    LineTelemetryWorkerConfig config,
    ErrorCallback errorCallback)
    : m_callback(std::move(callback)),
      m_errorCallback(std::move(errorCallback)),
      m_config(config) {
    m_config.maxQueuedChunks = std::max<std::size_t>(1, m_config.maxQueuedChunks);
    m_config.maxQueuedBytes = std::max<std::size_t>(1, m_config.maxQueuedBytes);
    m_config.maxBatchSamples = std::max<std::size_t>(1, m_config.maxBatchSamples);
    m_config.flushInterval = std::max(std::chrono::milliseconds{1}, m_config.flushInterval);
}

LineTelemetryDecodeWorker::~LineTelemetryDecodeWorker() {
    stop();
}

bool LineTelemetryDecodeWorker::start() {
    std::scoped_lock lifecycleLock(m_lifecycleMutex);
    if (m_thread.joinable()) {
        if (m_accepting.load(std::memory_order_acquire)) {
            return true;
        }
        m_thread.join();
    }

    m_decoder.reset();
    m_lastDeliveredMalformedLines = 0;
    {
        std::scoped_lock queueLock(m_queueMutex);
        m_chunks.clear();
        m_queuedBytes = 0;
        m_gapBeforeNextChunk = false;
        m_counters = {};
        m_lastDeliveredCounters = {};
        m_statsDirty = false;
    }

    m_accepting.store(true, std::memory_order_release);
    try {
        m_thread = std::jthread([this](const std::stop_token &stopToken) {
            run(stopToken);
        });
    } catch (const std::system_error &) {
        m_accepting.store(false, std::memory_order_release);
        return false;
    }
    return true;
}

void LineTelemetryDecodeWorker::stop() {
    std::jthread worker;
    {
        std::scoped_lock lifecycleLock(m_lifecycleMutex);
        m_accepting.store(false, std::memory_order_release);
        if (!m_thread.joinable()) {
            return;
        }

        m_thread.request_stop();
        m_queueChanged.notify_all();
        if (m_thread.get_id() == std::this_thread::get_id()) {
            return;
        }
        worker = std::move(m_thread);
    }
    worker.join();
}

bool LineTelemetryDecodeWorker::isRunning() const noexcept {
    return m_accepting.load(std::memory_order_acquire);
}

bool LineTelemetryDecodeWorker::enqueueChunk(std::vector<std::uint8_t> chunk) {
    if (!m_accepting.load(std::memory_order_acquire) || chunk.empty()) {
        return false;
    }

    const std::size_t chunkBytes = chunk.size();
    bool queued = true;
    {
        std::scoped_lock lock(m_queueMutex);
        if (!m_accepting.load(std::memory_order_relaxed)) {
            return false;
        }

        m_counters.receivedBytes += static_cast<std::uint64_t>(chunkBytes);
        m_statsDirty = true;

        if (chunkBytes > m_config.maxQueuedBytes) {
            ++m_counters.droppedChunks;
            m_counters.droppedBytes += static_cast<std::uint64_t>(chunkBytes);
            m_gapBeforeNextChunk = true;
            queued = false;
        } else {
            bool queueGap = false;
            while (!m_chunks.empty()
                   && (m_chunks.size() >= m_config.maxQueuedChunks
                       || m_queuedBytes > m_config.maxQueuedBytes - chunkBytes)) {
                const std::size_t droppedBytes = m_chunks.front().bytes.size();
                m_chunks.pop_front();
                m_queuedBytes -= droppedBytes;
                ++m_counters.droppedChunks;
                m_counters.droppedBytes += static_cast<std::uint64_t>(droppedBytes);
                queueGap = true;
            }

            if (queueGap) {
                if (m_chunks.empty()) {
                    m_gapBeforeNextChunk = true;
                } else {
                    m_chunks.front().gapBefore = true;
                }
            }

            m_queuedBytes += chunkBytes;
            m_chunks.push_back({std::move(chunk), m_gapBeforeNextChunk});
            m_gapBeforeNextChunk = false;
        }
    }

    m_queueChanged.notify_one();
    return queued;
}

LineTelemetryDecodeWorker::CounterSnapshot
LineTelemetryDecodeWorker::counterSnapshot() const {
    std::scoped_lock lock(m_queueMutex);
    return m_counters;
}

bool LineTelemetryDecodeWorker::deliver(std::vector<FlightSample> &samples) {
    const CounterSnapshot counters = counterSnapshot();
    const std::size_t malformedLines = m_decoder.malformedLineCount();

    LineTelemetryBatch batch;
    batch.receivedBytes = counters.receivedBytes - m_lastDeliveredCounters.receivedBytes;
    batch.droppedChunks = counters.droppedChunks - m_lastDeliveredCounters.droppedChunks;
    batch.droppedBytes = counters.droppedBytes - m_lastDeliveredCounters.droppedBytes;
    batch.malformedLines = malformedLines - m_lastDeliveredMalformedLines;
    batch.samples.swap(samples);

    m_lastDeliveredCounters = counters;
    m_lastDeliveredMalformedLines = malformedLines;

    if (batch.samples.empty()
        && batch.receivedBytes == 0
        && batch.malformedLines == 0
        && batch.droppedChunks == 0
        && batch.droppedBytes == 0) {
        return true;
    }

    if (m_callback) {
        try {
            m_callback(std::move(batch));
        } catch (const std::exception &error) {
            m_accepting.store(false, std::memory_order_release);
            if (m_errorCallback) {
                try {
                    m_errorCallback(error.what());
                } catch (const std::exception &) {
                }
            }
            return false;
        }
    }
    return true;
}

void LineTelemetryDecodeWorker::run(const std::stop_token &stopToken) {
    using Clock = std::chrono::steady_clock;

    std::vector<FlightSample> pendingSamples;
    pendingSamples.reserve(m_config.maxBatchSamples);
    auto flushDeadline = Clock::time_point::max();

    while (true) {
        QueuedChunk chunk;
        bool activity = false;
        bool shouldStop = false;
        bool deadlineReached = false;
        bool gapAtEnd = false;

        {
            std::unique_lock lock(m_queueMutex);
            const auto ready = [this, &stopToken] {
                return stopToken.stop_requested() || !m_chunks.empty() || m_statsDirty;
            };

            if (flushDeadline == Clock::time_point::max()) {
                // Use the stop-token-aware overload: request_stop() atomically
                // registers a callback that fires notify_all() under the internal
                // lock, eliminating the lost-wakeup race present in wait(lock, pred).
                m_queueChanged.wait(lock, stopToken, ready);
            } else if (!m_queueChanged.wait_until(lock, stopToken, flushDeadline, ready)) {
                deadlineReached = true;
            }

            if (m_statsDirty) {
                m_statsDirty = false;
                activity = true;
            }

            if (!m_chunks.empty()) {
                chunk = std::move(m_chunks.front());
                m_chunks.pop_front();
                m_queuedBytes -= chunk.bytes.size();
                activity = true;
            } else if (stopToken.stop_requested()) {
                shouldStop = true;
                gapAtEnd = m_gapBeforeNextChunk;
                m_gapBeforeNextChunk = false;
            }
        }

        if (activity && flushDeadline == Clock::time_point::max()) {
            flushDeadline = Clock::now() + m_config.flushInterval;
        }

        if (chunk.gapBefore) {
            m_decoder.notifyDataGap();
        }

        if (!chunk.bytes.empty()) {
            auto decoded = m_decoder.ingest(chunk.bytes.data(), chunk.bytes.size());
            for (auto &sample : decoded) {
                if (pendingSamples.empty()
                    && flushDeadline == Clock::time_point::max()) {
                    flushDeadline = Clock::now() + m_config.flushInterval;
                }
                pendingSamples.push_back(std::move(sample));
                if (pendingSamples.size() >= m_config.maxBatchSamples) {
                    if (!deliver(pendingSamples)) {
                        return;
                    }
                    flushDeadline = Clock::time_point::max();
                }
            }
        }

        if (flushDeadline != Clock::time_point::max()
            && Clock::now() >= flushDeadline) {
            deadlineReached = true;
        }

        if (deadlineReached) {
            if (!deliver(pendingSamples)) {
                return;
            }
            flushDeadline = Clock::time_point::max();
        }

        if (shouldStop) {
            if (gapAtEnd) {
                m_decoder.notifyDataGap();
            }
            auto finalSamples = m_decoder.finish();
            for (auto &sample : finalSamples) {
                pendingSamples.push_back(std::move(sample));
            }
            (void) deliver(pendingSamples);
            return;
        }
    }
}

} // namespace cosmo::telemetry
