#include <catch2/catch_test_macros.hpp>

#include "services/telemetry/LineTelemetryBatchMailbox.h"

#include <atomic>
#include <barrier>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] cosmo::telemetry::LineTelemetryBatch makeBatch(
    std::size_t sampleCount,
    std::uint64_t receivedBytes,
    std::size_t malformedLines,
    std::size_t droppedChunks,
    std::uint64_t droppedBytes) {
    cosmo::telemetry::LineTelemetryBatch batch;
    batch.samples.resize(sampleCount);
    batch.receivedBytes = receivedBytes;
    batch.malformedLines = malformedLines;
    batch.droppedChunks = droppedChunks;
    batch.droppedBytes = droppedBytes;
    return batch;
}

} // namespace

TEST_CASE("LineTelemetryBatchMailbox coalesces scheduling until a matching drain",
          "[telemetry][mailbox]") {
    cosmo::telemetry::LineTelemetryBatchMailbox mailbox(4);

    REQUIRE(mailbox.capacity() == 4);
    REQUIRE(mailbox.push(7, makeBatch(1, 10, 0, 0, 0)));
    REQUIRE_FALSE(mailbox.push(7, makeBatch(2, 20, 0, 0, 0)));

    auto result = mailbox.take(7);
    REQUIRE(result.status == cosmo::telemetry::LineTelemetryMailboxTakeStatus::Drained);
    REQUIRE(result.batches.size() == 2);
    REQUIRE(result.discardedDecodedSamples == 0);

    result = mailbox.take(7);
    REQUIRE(result.status == cosmo::telemetry::LineTelemetryMailboxTakeStatus::Empty);
    REQUIRE(mailbox.push(7, makeBatch(1, 30, 0, 0, 0)));
}

TEST_CASE("LineTelemetryBatchMailbox preserves counters when decoded samples overflow",
          "[telemetry][mailbox]") {
    cosmo::telemetry::LineTelemetryBatchMailbox mailbox(2);

    REQUIRE(mailbox.push(3, makeBatch(2, 100, 1, 2, 20)));
    REQUIRE_FALSE(mailbox.push(3, makeBatch(1, 200, 3, 4, 40)));
    REQUIRE_FALSE(mailbox.push(3, makeBatch(3, 300, 5, 6, 60)));

    const auto result = mailbox.take(3);
    REQUIRE(result.batches.size() == 2);
    REQUIRE(result.discardedDecodedSamples == 2);

    std::size_t retainedSamples = 0;
    std::uint64_t receivedBytes = 0;
    std::size_t malformedLines = 0;
    std::size_t droppedChunks = 0;
    std::uint64_t droppedBytes = 0;
    for (const auto &batch : result.batches) {
        retainedSamples += batch.samples.size();
        receivedBytes += batch.receivedBytes;
        malformedLines += batch.malformedLines;
        droppedChunks += batch.droppedChunks;
        droppedBytes += batch.droppedBytes;
    }

    REQUIRE(retainedSamples == 4);
    REQUIRE(receivedBytes == 600);
    REQUIRE(malformedLines == 9);
    REQUIRE(droppedChunks == 12);
    REQUIRE(droppedBytes == 120);
}

TEST_CASE("LineTelemetryBatchMailbox isolates stale generation drains and pushes",
          "[telemetry][mailbox]") {
    cosmo::telemetry::LineTelemetryBatchMailbox mailbox;

    const auto inactive = mailbox.take(10);
    REQUIRE(inactive.status
            == cosmo::telemetry::LineTelemetryMailboxTakeStatus::NoActiveGeneration);

    REQUIRE(mailbox.push(10, makeBatch(1, 10, 0, 0, 0)));
    REQUIRE(mailbox.push(11, makeBatch(2, 20, 0, 0, 0)));

    const auto staleDrain = mailbox.take(10);
    REQUIRE(staleDrain.status
            == cosmo::telemetry::LineTelemetryMailboxTakeStatus::StaleGeneration);
    REQUIRE(staleDrain.batches.empty());

    REQUIRE_FALSE(mailbox.push(11, makeBatch(3, 30, 0, 0, 0)));
    REQUIRE_FALSE(mailbox.push(10, makeBatch(4, 40, 0, 0, 0)));

    const auto currentDrain = mailbox.take(11);
    REQUIRE(currentDrain.status
            == cosmo::telemetry::LineTelemetryMailboxTakeStatus::Drained);
    REQUIRE(currentDrain.batches.size() == 2);
    REQUIRE(currentDrain.batches.front().samples.size() == 2);
    REQUIRE(currentDrain.batches.back().samples.size() == 3);
}

TEST_CASE("LineTelemetryBatchMailbox accepts concurrent producers with one scheduled drain",
          "[telemetry][mailbox]") {
    constexpr std::size_t kProducerCount = 16;
    cosmo::telemetry::LineTelemetryBatchMailbox mailbox(kProducerCount);
    std::barrier startLine(static_cast<std::ptrdiff_t>(kProducerCount));
    std::atomic_size_t scheduleRequests{0};
    std::vector<std::jthread> producers;
    producers.reserve(kProducerCount);

    for (std::size_t index = 0; index < kProducerCount; ++index) {
        producers.emplace_back([&mailbox, &startLine, &scheduleRequests, index] {
            startLine.arrive_and_wait();
            if (mailbox.push(42, makeBatch(1, index + 1, 0, 0, 0))) {
                scheduleRequests.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto &producer : producers) {
        producer.join();
    }

    REQUIRE(scheduleRequests.load(std::memory_order_relaxed) == 1);
    const auto disconnectDrain = mailbox.take(42);
    REQUIRE(disconnectDrain.batches.size() == kProducerCount);
    REQUIRE(disconnectDrain.discardedDecodedSamples == 0);
    REQUIRE(mailbox.push(42, makeBatch(1, 1, 0, 0, 0)));
}

TEST_CASE("LineTelemetryBatchMailbox cannot strand a push racing the scheduled drain",
          "[telemetry][mailbox]") {
    constexpr std::size_t kRaceCount = 64;

    for (std::size_t index = 0; index < kRaceCount; ++index) {
        cosmo::telemetry::LineTelemetryBatchMailbox mailbox(2);
        REQUIRE(mailbox.push(9, makeBatch(1, 1, 0, 0, 0)));

        std::barrier startLine(2);
        std::atomic_bool secondDrainRequested{false};
        std::jthread producer([&mailbox, &startLine, &secondDrainRequested] {
            startLine.arrive_and_wait();
            secondDrainRequested.store(
                mailbox.push(9, makeBatch(1, 1, 0, 0, 0)),
                std::memory_order_release);
        });

        startLine.arrive_and_wait();
        const auto scheduledDrain = mailbox.take(9);
        producer.join();

        std::size_t drainedSamples = 0;
        for (const auto &batch : scheduledDrain.batches) {
            drainedSamples += batch.samples.size();
        }
        if (secondDrainRequested.load(std::memory_order_acquire)) {
            const auto followUpDrain = mailbox.take(9);
            for (const auto &batch : followUpDrain.batches) {
                drainedSamples += batch.samples.size();
            }
        }

        REQUIRE(drainedSamples == 2);
    }
}
