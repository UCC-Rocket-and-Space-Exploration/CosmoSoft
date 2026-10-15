#include <catch2/catch_test_macros.hpp>

#include "gui/FlightDataModel.h"
#include "gui/TelemetryMath.h"
#include "services/flight/FakeFlightLink.h"
#include "services/telemetry/LineTelemetryDecodeWorker.h"
#include "services/telemetry/LineTelemetryDecoder.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] std::vector<std::uint8_t> asBytes(const std::string &text) {
    return {text.begin(), text.end()};
}

} // namespace

TEST_CASE("Live CSV keeps supplied degree attitude", "[telemetry][attitude]") {
  const auto sample = cosmo::telemetry::decodeTelemetryCsvRow(
      "1,18,101325,100,0,0,9.81,0,0,0,30,45,-90,54,-5,0");
  REQUIRE(sample.has_value());
  REQUIRE(sample->angularRotation.x == 30);
  REQUIRE(sample->angularRotation.y == 45);
  REQUIRE(sample->angularRotation.z == -90);
}

TEST_CASE("Built-in demo supplies the same attitude as its wire packet",
          "[telemetry][attitude]") {
  const auto packet = cosmo::flightlink::buildLaunchTelemetryPacket(40);
  REQUIRE(packet.sample.angularRotation.z == 16);
  REQUIRE(packet.sample.angularRotation.x > 7);
  REQUIRE(packet.sample.angularRotation.x < 9);
  REQUIRE(packet.sample.angularRotation.y < -1);
}

TEST_CASE("LineTelemetryDecoder bounds oversized partial lines and recovers", "[telemetry][worker]") {
    cosmo::telemetry::LineTelemetryDecoder decoder(256);
    const std::string input = std::string(300, 'x') + '\n'
                            + std::string(cosmo::flightlink::kExampleTelemetryRow) + '\n';
    const auto bytes = asBytes(input);

    const auto samples = decoder.ingest(bytes.data(), bytes.size());

    REQUIRE(decoder.malformedLineCount() == 1);
    REQUIRE(samples.size() == 1);
    REQUIRE(samples.front().timestamp == 0);
}

TEST_CASE("LineTelemetryDecoder rejects unsafe timestamp and coordinate ranges", "[telemetry]") {
    REQUIRE_FALSE(cosmo::telemetry::decodeTelemetryCsvRow(
        "1e300,18.50,101325.00,0.00,0.000,0.040,9.810,0.000,0.015,0.000,"
        "0.000,0.300,0.000,51.89,-8.49,0.00"));
    REQUIRE_FALSE(cosmo::telemetry::decodeTelemetryCsvRow(
        "0.00,18.50,101325.00,0.00,0.000,0.040,9.810,0.000,0.015,0.000,"
        "0.000,0.300,0.000,91.0,-8.49,0.00"));
    REQUIRE_FALSE(cosmo::telemetry::decodeTelemetryCsvRow(
        "0.00,18.50,101325.00,0.00,0.000,0.040,9.810,0.000,0.015,0.000,"
        "0.000,0.300,0.000,51.89,-181.0,0.00"));
    REQUIRE_FALSE(cosmo::telemetry::decodeTelemetryCsvRow(
        "0.00,18.50,101325.00,1e300,0.000,0.040,9.810,0.000,0.015,0.000,"
        "0.000,0.300,0.000,51.89,-8.49,0.00"));
}

TEST_CASE("LineTelemetryDecoder resynchronizes after a byte-stream gap", "[telemetry][worker]") {
    cosmo::telemetry::LineTelemetryDecoder decoder;
    const std::string row(cosmo::flightlink::kExampleTelemetryRow);
    const std::string prefix = row.substr(0, 12);
    const auto prefixBytes = asBytes(prefix);
    REQUIRE(decoder.ingest(prefixBytes.data(), prefixBytes.size()).empty());

    decoder.notifyDataGap();
    const std::string afterGap = row.substr(prefix.size()) + '\n' + row + '\n';
    const auto afterGapBytes = asBytes(afterGap);
    const auto samples = decoder.ingest(afterGapBytes.data(), afterGapBytes.size());

    REQUIRE(samples.size() == 1);
    REQUIRE(samples.front().timestamp == 0);
}

TEST_CASE("LineTelemetryDecoder finalizes rows without a trailing newline", "[telemetry][worker]") {
    cosmo::telemetry::LineTelemetryDecoder validDecoder;
    const auto validBytes = asBytes(std::string(cosmo::flightlink::kExampleTelemetryRow));
    REQUIRE(validDecoder.ingest(validBytes.data(), validBytes.size()).empty());
    REQUIRE(validDecoder.finish().size() == 1);
    REQUIRE(validDecoder.malformedLineCount() == 0);

    cosmo::telemetry::LineTelemetryDecoder truncatedDecoder;
    const auto truncatedBytes = asBytes("0.00,18.50,101325.00");
    REQUIRE(truncatedDecoder.ingest(truncatedBytes.data(), truncatedBytes.size()).empty());
    REQUIRE(truncatedDecoder.finish().empty());
    REQUIRE(truncatedDecoder.malformedLineCount() == 1);
}

TEST_CASE("LineTelemetryDecodeWorker drains input in bounded batches with aggregate counters",
          "[telemetry][worker]") {
    std::vector<cosmo::telemetry::LineTelemetryBatch> deliveries;
    cosmo::telemetry::LineTelemetryWorkerConfig config;
    config.maxBatchSamples = 64;
    config.flushInterval = std::chrono::hours(1);

    cosmo::telemetry::LineTelemetryDecodeWorker worker(
        [&deliveries](cosmo::telemetry::LineTelemetryBatch batch) {
            deliveries.push_back(std::move(batch));
        },
        config);

    std::string input = "not,a,telemetry,row\n";
    for (std::size_t index = 0; index < 130; ++index) {
        input.append(cosmo::flightlink::kExampleTelemetryRow);
        input.push_back('\n');
    }

    REQUIRE(worker.start());
    REQUIRE(worker.enqueueChunk(asBytes(input)));
    worker.stop();

    std::size_t sampleCount = 0;
    std::size_t malformedCount = 0;
    std::uint64_t receivedBytes = 0;
    for (const auto &delivery : deliveries) {
        REQUIRE(delivery.samples.size() <= 64);
        sampleCount += delivery.samples.size();
        malformedCount += delivery.malformedLines;
        receivedBytes += delivery.receivedBytes;
    }

    REQUIRE(sampleCount == 130);
    REQUIRE(malformedCount == 1);
    REQUIRE(receivedBytes == input.size());
    REQUIRE(deliveries.size() == 3);
}

TEST_CASE("LineTelemetryDecodeWorker reports an oversized chunk without retaining it",
          "[telemetry][worker]") {
    std::vector<cosmo::telemetry::LineTelemetryBatch> deliveries;
    cosmo::telemetry::LineTelemetryWorkerConfig config;
    config.maxQueuedBytes = 8;

    cosmo::telemetry::LineTelemetryDecodeWorker worker(
        [&deliveries](cosmo::telemetry::LineTelemetryBatch batch) {
            deliveries.push_back(std::move(batch));
        },
        config);

    REQUIRE(worker.start());
    REQUIRE_FALSE(worker.enqueueChunk(std::vector<std::uint8_t>(9, 0x41)));
    worker.stop();

    REQUIRE(deliveries.size() == 1);
    REQUIRE(deliveries.front().samples.empty());
    REQUIRE(deliveries.front().receivedBytes == 9);
    REQUIRE(deliveries.front().droppedChunks == 1);
    REQUIRE(deliveries.front().droppedBytes == 9);
}

TEST_CASE("LineTelemetryDecodeWorker never splices samples across a rejected chunk",
          "[telemetry][worker]") {
    std::vector<cosmo::telemetry::LineTelemetryBatch> deliveries;
    cosmo::telemetry::LineTelemetryWorkerConfig config;
    config.maxQueuedBytes = 512;
    config.flushInterval = std::chrono::hours(1);

    cosmo::telemetry::LineTelemetryDecodeWorker worker(
        [&deliveries](cosmo::telemetry::LineTelemetryBatch batch) {
            deliveries.push_back(std::move(batch));
        },
        config);

    const std::string row(cosmo::flightlink::kExampleTelemetryRow);
    const std::string prefix = row.substr(0, 12);
    const std::string recovery = row.substr(prefix.size()) + '\n' + row + '\n';
    REQUIRE(worker.start());
    REQUIRE(worker.enqueueChunk(asBytes(prefix)));
    REQUIRE_FALSE(worker.enqueueChunk(std::vector<std::uint8_t>(513, std::uint8_t{0x37})));
    REQUIRE(worker.enqueueChunk(asBytes(recovery)));
    worker.stop();

    std::size_t samples = 0;
    std::size_t droppedChunks = 0;
    for (const auto &delivery : deliveries) {
        samples += delivery.samples.size();
        droppedChunks += delivery.droppedChunks;
    }
    REQUIRE(samples == 1);
    REQUIRE(droppedChunks == 1);
}

TEST_CASE("LineTelemetryDecodeWorker emits a valid final row without newline",
          "[telemetry][worker]") {
    std::vector<cosmo::telemetry::LineTelemetryBatch> deliveries;
    cosmo::telemetry::LineTelemetryDecodeWorker worker(
        [&deliveries](cosmo::telemetry::LineTelemetryBatch batch) {
            deliveries.push_back(std::move(batch));
        });

    REQUIRE(worker.start());
    REQUIRE(worker.enqueueChunk(asBytes(std::string(cosmo::flightlink::kExampleTelemetryRow))));
    worker.stop();

    std::size_t sampleCount = 0;
    for (const auto &delivery : deliveries) {
        sampleCount += delivery.samples.size();
    }
    REQUIRE(sampleCount == 1);
}

TEST_CASE("LineTelemetryDecodeWorker permits a callback to request stop", "[telemetry][worker]") {
    std::mutex callbackMutex;
    std::condition_variable callbackFinished;
    std::size_t callbackCount = 0;
    cosmo::telemetry::LineTelemetryWorkerConfig config;
    config.maxBatchSamples = 1;
    cosmo::telemetry::LineTelemetryDecodeWorker *workerPointer = nullptr;
    cosmo::telemetry::LineTelemetryDecodeWorker worker(
        [&](cosmo::telemetry::LineTelemetryBatch batch) {
            if (!batch.samples.empty()) {
                workerPointer->stop();
                {
                    std::scoped_lock lock(callbackMutex);
                    ++callbackCount;
                }
                callbackFinished.notify_one();
            }
        },
        config);
    workerPointer = &worker;

    const std::string row = std::string(cosmo::flightlink::kExampleTelemetryRow) + '\n';
    REQUIRE(worker.start());
    REQUIRE(worker.enqueueChunk(asBytes(row)));
    {
        std::unique_lock lock(callbackMutex);
        REQUIRE(callbackFinished.wait_for(
            lock,
            std::chrono::seconds(1),
            [&callbackCount] { return callbackCount == 1; }));
    }
    worker.stop();
    REQUIRE_FALSE(worker.isRunning());

    REQUIRE(worker.start());
    REQUIRE(worker.enqueueChunk(asBytes(row)));
    {
        std::unique_lock lock(callbackMutex);
        REQUIRE(callbackFinished.wait_for(
            lock,
            std::chrono::seconds(1),
            [&callbackCount] { return callbackCount == 2; }));
    }
    worker.stop();
    REQUIRE_FALSE(worker.isRunning());
}

TEST_CASE("LineTelemetryDecodeWorker contains exceptions thrown by its batch callback",
          "[telemetry][worker]") {
    std::mutex errorMutex;
    std::condition_variable errorReceived;
    std::string errorMessage;
    cosmo::telemetry::LineTelemetryWorkerConfig config;
    config.maxBatchSamples = 1;

    cosmo::telemetry::LineTelemetryDecodeWorker worker(
        [](cosmo::telemetry::LineTelemetryBatch) {
            throw std::runtime_error("test delivery failure");
        },
        config,
        [&](const std::string &message) {
            {
                std::scoped_lock lock(errorMutex);
                errorMessage = message;
            }
            errorReceived.notify_one();
        });

    const std::string row = std::string(cosmo::flightlink::kExampleTelemetryRow) + '\n';
    REQUIRE(worker.start());
    REQUIRE(worker.enqueueChunk(asBytes(row)));
    {
        std::unique_lock lock(errorMutex);
        REQUIRE(errorReceived.wait_for(
            lock,
            std::chrono::seconds(1),
            [&errorMessage] { return !errorMessage.empty(); }));
    }
    REQUIRE(errorMessage == "test delivery failure");
    worker.stop();
    REQUIRE_FALSE(worker.isRunning());
}

TEST_CASE("LineTelemetryDecodeWorker flushes a partial batch on its deadline",
          "[telemetry][worker]") {
    std::mutex deliveryMutex;
    std::condition_variable delivered;
    std::size_t deliveredSamples = 0;
    cosmo::telemetry::LineTelemetryWorkerConfig config;
    config.flushInterval = std::chrono::milliseconds(5);

    cosmo::telemetry::LineTelemetryDecodeWorker worker(
        [&deliveryMutex, &delivered, &deliveredSamples](cosmo::telemetry::LineTelemetryBatch batch) {
            {
                std::scoped_lock lock(deliveryMutex);
                deliveredSamples += batch.samples.size();
            }
            delivered.notify_one();
        },
        config);

    const std::string input = std::string(cosmo::flightlink::kExampleTelemetryRow) + '\n';
    REQUIRE(worker.start());
    REQUIRE(worker.enqueueChunk(asBytes(input)));

    bool arrivedBeforeStop = false;
    {
        std::unique_lock lock(deliveryMutex);
        arrivedBeforeStop = delivered.wait_for(
            lock,
            std::chrono::seconds(1),
            [&deliveredSamples] { return deliveredSamples == 1; });
    }
    worker.stop();

    REQUIRE(arrivedBeforeStop);
    REQUIRE(deliveredSamples == 1);
}

TEST_CASE("FlightDataModel separates live batches from coalesced display updates",
          "[gui][telemetry]") {
    FlightDataModel model;
    int legacyUpdates = 0;
    int displayUpdates = 0;
    int liveUpdates = 0;
    std::size_t deliveredSamples = 0;
    std::vector<long> legacyTimestamps;

    QObject::connect(&model, &FlightDataModel::sampleUpdated,
                     [&legacyUpdates, &legacyTimestamps](const FlightSample &sample) {
                         ++legacyUpdates;
                         legacyTimestamps.push_back(sample.timestamp);
                     });
    QObject::connect(&model, &FlightDataModel::displayedSampleChanged,
                     [&displayUpdates](const FlightSample &) { ++displayUpdates; });
    QObject::connect(&model, &FlightDataModel::liveSamplesReceived,
                     [&liveUpdates, &deliveredSamples](const FlightSampleBatch &samples) {
                         ++liveUpdates;
                         deliveredSamples += static_cast<std::size_t>(samples.size());
                     });

    FlightSample first;
    first.timestamp = 10;
    FlightSample second;
    second.timestamp = 20;
    model.appendLiveBatch(FlightSampleBatch{first, second});

    REQUIRE(model.latestSample().timestamp == 20);
    REQUIRE(legacyUpdates == 2);
    REQUIRE((legacyTimestamps == std::vector<long>{10, 20}));
    REQUIRE(displayUpdates == 1);
    REQUIRE(liveUpdates == 1);
    REQUIRE(deliveredSamples == 2);

    FlightSample replaySample;
    replaySample.timestamp = 30;
    model.setDisplayedSample(replaySample);

    REQUIRE(model.latestSample().timestamp == 30);
    REQUIRE(legacyUpdates == 3);
    REQUIRE(displayUpdates == 2);
    REQUIRE(liveUpdates == 1);

    model.appendLiveBatch({});
    REQUIRE(legacyUpdates == 3);
    REQUIRE(displayUpdates == 2);
    REQUIRE(liveUpdates == 1);

    model.addBytesReceived(std::numeric_limits<qint64>::max() - 5);
    model.addBytesReceived(10);
    REQUIRE(model.totalBytesReceived() == std::numeric_limits<qint64>::max());
    model.addBytesReceived(-1);
    REQUIRE(model.totalBytesReceived() == std::numeric_limits<qint64>::max());
}

TEST_CASE("Live telemetry velocity rejects non-increasing and extreme timestamps", "[gui][telemetry]") {
    FlightSample previous;
    previous.timestamp = 1'000;
    previous.altitude = 10.0;
    FlightSample current;
    current.timestamp = 2'000;
    current.altitude = 25.0;

    REQUIRE(cosmo::gui::verticalVelocityMetersPerSecond(previous, current).value() == 15.0);

    current.timestamp = previous.timestamp;
    REQUIRE_FALSE(cosmo::gui::verticalVelocityMetersPerSecond(previous, current));

    current.timestamp = 500;
    REQUIRE_FALSE(cosmo::gui::verticalVelocityMetersPerSecond(previous, current));

    previous.timestamp = std::numeric_limits<long>::max();
    current.timestamp = std::numeric_limits<long>::lowest();
    REQUIRE_FALSE(cosmo::gui::verticalVelocityMetersPerSecond(previous, current));
}

TEST_CASE("Live diagnostics identify missing and invalid fields without "
          "accepting partial rows",
          "[mission][telemetry][diagnostics]") {
  cosmo::telemetry::LineTelemetryDecoder decoder;
  const auto first = asBytes("1,18,101325,,0,0,9.81,0,0,0,bad,0,0,54,-5,0");
  REQUIRE(decoder.ingest(first.data(), first.size()).empty());
  REQUIRE(decoder.lastRowDiagnostic().sequence == 0);
  const auto newline = asBytes("\n");
  REQUIRE(decoder.ingest(newline.data(), newline.size()).empty());
  const auto issue = decoder.lastRowDiagnostic();
  REQUIRE(issue.sequence == 1);
  REQUIRE(issue.missing == (1U << 3));
  REQUIRE(issue.invalid == (1U << 10));
  REQUIRE(issue.malformed);
  const auto valid = asBytes("2,0,101325,0,0,0,0,0,0,0,0,0,0,54,-5,0\n");
  REQUIRE(decoder.ingest(valid.data(), valid.size()).size() == 1);
  REQUIRE(decoder.lastRowDiagnostic().sequence == 2);
  REQUIRE_FALSE(decoder.lastRowDiagnostic().malformed);
  REQUIRE(decoder.lastRowDiagnostic().missing == 0);
  decoder.reset();
  REQUIRE(decoder.lastRowDiagnostic().sequence == 0);
}

TEST_CASE("An oversized completed row replaces an older field diagnostic",
          "[mission][telemetry][diagnostics]") {
  cosmo::telemetry::LineTelemetryDecoder decoder(80);
  const auto input = asBytes("1,18,101325,,0,0,9.81,0,0,0,0,0,0,54,-5,0\n");
  REQUIRE(decoder.ingest(input.data(), input.size()).empty());
  REQUIRE(decoder.lastRowDiagnostic().missing == (1U << 3));
  const auto oversized = asBytes(std::string(100, 'x'));
  REQUIRE(decoder.ingest(oversized.data(), oversized.size()).empty());
  REQUIRE(decoder.lastRowDiagnostic().sequence == 1);
  const auto newline = asBytes("\n");
  decoder.ingest(newline.data(), newline.size());
  REQUIRE(decoder.lastRowDiagnostic().sequence == 2);
  REQUIRE(decoder.lastRowDiagnostic().missing == 0);
  REQUIRE(decoder.lastRowDiagnostic().malformed);
}

TEST_CASE(
    "Worker delivers a final oversized-row diagnostic after counter flush",
    "[telemetry][worker][diagnostics]") {
  std::mutex mutex;
  std::condition_variable delivered;
  std::size_t malformed = 0;
  std::uint64_t latestSequence = 0;
  cosmo::telemetry::LineTelemetryWorkerConfig config;
  config.flushInterval = std::chrono::milliseconds(5);
  cosmo::telemetry::LineTelemetryDecodeWorker worker(
      [&](cosmo::telemetry::LineTelemetryBatch batch) {
        std::scoped_lock lock(mutex);
        malformed += batch.malformedLines;
        if (batch.rowDiagnostic)
          latestSequence = batch.rowDiagnostic->sequence;
        delivered.notify_one();
      },
      config);
  REQUIRE(worker.start());
  REQUIRE(worker.enqueueChunk(asBytes(
      std::string(cosmo::telemetry::kMaxLiveTelemetryLineBytes + 1, 'x'))));
  bool countersFlushed = false;
  {
    std::unique_lock lock(mutex);
    countersFlushed = delivered.wait_for(lock, std::chrono::seconds(1),
                                         [&] { return malformed == 1; });
  }
  worker.stop();
  REQUIRE(countersFlushed);
  REQUIRE(latestSequence == 1);
}
