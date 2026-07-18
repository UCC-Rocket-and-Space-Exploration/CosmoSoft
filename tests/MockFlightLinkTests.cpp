#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "services/flight/FakeFlightLink.h"
#include "services/preview/FlightPreviewCache.h"
#include "services/telemetry/LineTelemetryDecoder.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace {

[[nodiscard]] std::string readAll(cosmo::flightlink::FakeComms &comms, std::size_t readSize) {
    std::string out;
    std::vector<uint8_t> buffer(readSize);
    while (true) {
        const ssize_t count = comms.read(buffer.data(), buffer.size());
        if (count <= 0) {
            break;
        }
        out.append(
            reinterpret_cast<const char *>(buffer.data()),
            static_cast<std::size_t>(count));
    }
    return out;
}

[[nodiscard]] double cellAsDouble(const std::vector<std::string> &cells, std::size_t index) {
    return std::strtod(cells.at(index).c_str(), nullptr);
}

} // namespace

TEST_CASE("FakeFlightComputer emits deterministic rocket launch telemetry", "[flight-link]") {
    cosmo::flightlink::FakeFlightComputer computer;

    const auto firstPacket = computer.nextPacket();
    const auto secondPacket = computer.nextPacket();

    REQUIRE(firstPacket.has_value());
    REQUIRE(secondPacket.has_value());
    REQUIRE(firstPacket->line == std::string(cosmo::flightlink::kExampleTelemetryRow));
    REQUIRE(cosmo::flightlink::isValidTelemetryCsvRow(firstPacket->line));
    REQUIRE(cosmo::flightlink::isValidTelemetryCsvRow(secondPacket->line));

    const auto firstCells = cosmo::flightlink::splitCsvCells(firstPacket->line);
    const auto secondCells = cosmo::flightlink::splitCsvCells(secondPacket->line);
    REQUIRE(firstCells.at(13) == std::string(cosmo::flightlink::kLaunchLatitudeText));
    REQUIRE(firstCells.at(14) == std::string(cosmo::flightlink::kLaunchLongitudeText));
    REQUIRE(cellAsDouble(firstCells, 3) == 0.0);
    REQUIRE(cellAsDouble(firstCells, 15) == 0.0);
    REQUIRE(cellAsDouble(secondCells, 3) > cellAsDouble(firstCells, 3));
    REQUIRE(cellAsDouble(secondCells, 15) > cellAsDouble(firstCells, 15));
    REQUIRE(firstPacket->sample.coordinates.latitude == cosmo::flightlink::kLaunchLatitude);
    REQUIRE(firstPacket->sample.coordinates.longitude == cosmo::flightlink::kLaunchLongitude);
}

TEST_CASE("FakeGroundStation splits packet lines into deterministic serial chunks", "[flight-link]") {
    const cosmo::flightlink::FakeGroundStation station;
    const std::string packetLine = std::string(cosmo::flightlink::kExampleTelemetryRow) + '\n';

    const auto chunks = station.chunkPacketLine(packetLine, 17);

    REQUIRE(chunks.size() == (packetLine.size() + 16) / 17);

    std::string reassembled;
    for (const auto &chunk : chunks) {
        REQUIRE(chunk.size() <= 17);
        reassembled.append(reinterpret_cast<const char *>(chunk.data()), chunk.size());
    }
    REQUIRE(reassembled == packetLine);
}

TEST_CASE("FakeComms models serial open read write and close without hardware", "[flight-link]") {
    cosmo::flightlink::FakeFlightComputer computer;
    const cosmo::flightlink::FakeGroundStation station;
    cosmo::flightlink::FakeComms comms;
    std::array<uint8_t, 4> scratch{};

    REQUIRE_FALSE(comms.isOpen());
    REQUIRE(comms.read(scratch.data(), scratch.size()) == -1);

    REQUIRE(comms.open());
    REQUIRE(comms.isOpen());
    REQUIRE(station.forwardNextPacket(computer, comms, 8));

    const std::string expectedPacket = std::string(cosmo::flightlink::kExampleTelemetryRow) + '\n';
    REQUIRE(readAll(comms, 5) == expectedPacket);

    const std::string command = "PING\n";
    REQUIRE(comms.write(reinterpret_cast<const uint8_t *>(command.data()), command.size())
            == static_cast<ssize_t>(command.size()));
    REQUIRE(comms.writtenText() == command);

    comms.close();
    REQUIRE_FALSE(comms.isOpen());
    REQUIRE(comms.read(scratch.data(), scratch.size()) == -1);
    REQUIRE(comms.write(reinterpret_cast<const uint8_t *>(command.data()), command.size())
            == -1);
}

TEST_CASE("Telemetry CSV row validation rejects malformed rows without production parser changes", "[flight-link]") {
    const std::string nonNumericAcceleration =
        "13603,19.24,100896.15,165.59,0.704,-0.927,not-a-number,-0.047,0.091,0.091,"
        "-4.822,-2.985,16.506,0.000000,0.000000,0.00";

    REQUIRE(cosmo::flightlink::isValidTelemetryCsvRow(cosmo::flightlink::kExampleTelemetryRow));
    REQUIRE(cosmo::flightlink::isValidTelemetryCsvRow(
        std::string(cosmo::flightlink::kExampleTelemetryRow) + "\r\n"));

    REQUIRE_FALSE(cosmo::flightlink::isValidTelemetryCsvRow("13603,19.24,100896.15"));
    REQUIRE_FALSE(cosmo::flightlink::isValidTelemetryCsvRow(nonNumericAcceleration));
    REQUIRE_FALSE(cosmo::flightlink::isValidTelemetryCsvRow(
        std::string(cosmo::flightlink::kExampleTelemetryRow) + ",1.0"));
}

TEST_CASE("LineTelemetryDecoder decodes fake telemetry CSV rows", "[telemetry]") {
    const auto sample = cosmo::telemetry::decodeTelemetryCsvRow(
        cosmo::flightlink::kExampleTelemetryRow);

    REQUIRE(sample.has_value());
    REQUIRE(sample->timestamp == 0);
    REQUIRE(sample->temperature == Catch::Approx(18.50));
    REQUIRE(sample->pressure == Catch::Approx(101325.00));
    REQUIRE(sample->altitude == Catch::Approx(0.0));
    REQUIRE(sample->acceleration.z == Catch::Approx(9.810));
    REQUIRE(sample->angularVelocity.y == Catch::Approx(0.015));
    REQUIRE(sample->coordinates.latitude == Catch::Approx(cosmo::flightlink::kLaunchLatitude));
    REQUIRE(sample->coordinates.longitude == Catch::Approx(cosmo::flightlink::kLaunchLongitude));
    REQUIRE(sample->batteryVoltage == Catch::Approx(0.0));
    REQUIRE(sample->rssi == Catch::Approx(0.0));
}

TEST_CASE("LineTelemetryDecoder rejects malformed telemetry CSV rows", "[telemetry]") {
    REQUIRE_FALSE(cosmo::telemetry::decodeTelemetryCsvRow("13603,19.24,100896.15"));
    REQUIRE_FALSE(cosmo::telemetry::decodeTelemetryCsvRow(
        "0.00,18.50,101325.00,0.00,0.000,0.040,not-a-number,0.000,0.015,0.000,"
        "0.000,0.300,0.000,51.89350958382806,-8.492074863487407,0.00"));
    REQUIRE_FALSE(cosmo::telemetry::decodeTelemetryCsvRow(
        std::string(cosmo::flightlink::kExampleTelemetryRow) + ",1.0"));
    REQUIRE_FALSE(cosmo::telemetry::decodeTelemetryCsvRow(
        cosmo::telemetry::kLiveTelemetryCsvHeader));
}

TEST_CASE("LineTelemetryDecoder reconstructs rows split across serial chunks", "[telemetry]") {
    cosmo::flightlink::FakeFlightComputer computer;
    const cosmo::flightlink::FakeGroundStation station;
    cosmo::telemetry::LineTelemetryDecoder decoder;
    std::vector<FlightSample> samples;

    for (int packetIndex = 0; packetIndex < 2; ++packetIndex) {
        const auto packetLine = computer.nextPacketLine();
        REQUIRE(packetLine.has_value());
        for (const auto &chunk : station.chunkPacketLine(*packetLine, 11)) {
            auto decoded = decoder.ingest(chunk.data(), chunk.size());
            samples.insert(samples.end(), decoded.begin(), decoded.end());
        }
    }

    REQUIRE(samples.size() == 2);
    REQUIRE(samples[0].timestamp == 0);
    REQUIRE(samples[1].timestamp > samples[0].timestamp);
    REQUIRE(decoder.malformedLineCount() == 0);
}

TEST_CASE("Fake live packet stream decodes to ordered sample timestamps", "[telemetry]") {
    cosmo::flightlink::FakeFlightComputer computer;
    long previousTimestamp = -1;

    for (int i = 0; i < 5; ++i) {
        const auto packetLine = computer.nextPacketLine();
        REQUIRE(packetLine.has_value());
        const auto sample = cosmo::telemetry::decodeTelemetryCsvRow(*packetLine);
        REQUIRE(sample.has_value());
        REQUIRE(sample->timestamp > previousTimestamp);
        previousTimestamp = sample->timestamp;
    }
}

TEST_CASE("FlightPreviewCache builds monotonic display time without mutating raw timestamps", "[preview]") {
    FlightSession session;
    session.samples.resize(5);
    session.samples[0].timestamp = 0;
    session.samples[1].timestamp = 250;
    session.samples[2].timestamp = 500;
    session.samples[3].timestamp = -100;
    session.samples[4].timestamp = 750;

    const auto cache = cosmo::preview::FlightPreviewCache::build(session);

    REQUIRE(cache->correctedTimelineUsed());
    REQUIRE(cache->timestampDiscontinuityCount() == 1);
    REQUIRE(cache->medianPositiveDeltaMs() == 250);
    REQUIRE(session.samples[3].timestamp == -100);
    REQUIRE(cache->displaySeconds().size() == session.samples.size());
    for (std::size_t i = 1; i < cache->displaySeconds().size(); ++i) {
        REQUIRE(cache->displaySeconds()[i] > cache->displaySeconds()[i - 1]);
    }
    REQUIRE(cache->durationSeconds() == Catch::Approx(1.6));
}

TEST_CASE("FlightPreviewCache safely corrects extreme timestamp transitions", "[preview]") {
    FlightSession session;
    session.samples.resize(3);
    session.samples[0].timestamp = std::numeric_limits<long>::lowest();
    session.samples[1].timestamp = std::numeric_limits<long>::max();
    session.samples[2].timestamp = std::numeric_limits<long>::lowest();

    const auto cache = cosmo::preview::FlightPreviewCache::build(session);

    REQUIRE(cache);
    REQUIRE(cache->correctedTimelineUsed());
    REQUIRE(cache->timestampDiscontinuityCount() == 2);
    REQUIRE(cache->displaySeconds().size() == 3);
    REQUIRE(std::isfinite(cache->durationSeconds()));
    REQUIRE(cache->displaySeconds()[1] > cache->displaySeconds()[0]);
    REQUIRE(cache->displaySeconds()[2] > cache->displaySeconds()[1]);
}

TEST_CASE("FlightPreviewCache chart indices cap point count and preserve spikes", "[preview]") {
    FlightSession session;
    session.samples.resize(101);
    for (int i = 0; i <= 100; ++i) {
        auto &sample = session.samples[static_cast<std::size_t>(i)];
        sample.timestamp = i * 10;
        sample.altitude = 1.0;
    }
    session.samples[50].altitude = 1000.0;

    const auto cache = cosmo::preview::FlightPreviewCache::build(session);
    std::array<bool, cosmo::preview::FlightPreviewCache::kMetricCount> enabled{};
    enabled[0] = true;

    const auto exact = cache->chartIndices(session, 10, 15, 20, enabled);
    REQUIRE(exact.size() == 5);
    REQUIRE(exact.front() == 10);
    REQUIRE(exact.back() == 14);

    const auto reduced = cache->chartIndices(session, 0, 101, 12, enabled);
    REQUIRE(reduced.size() <= 12);
    REQUIRE(reduced.front() == 0);
    REQUIRE(reduced.back() == 100);
    REQUIRE(std::find(reduced.begin(), reduced.end(), 50) != reduced.end());
}

TEST_CASE("FlightPreviewCache filters placeholder GPS rows and caps map paths", "[preview]") {
    FlightSession session;
    session.samples.resize(12050);
    for (int i = 0; i < static_cast<int>(session.samples.size()); ++i) {
        auto &sample = session.samples[static_cast<std::size_t>(i)];
        sample.timestamp = i * 10;
        sample.coordinates.latitude = 54.0 + static_cast<double>(i) * 1e-6;
        sample.coordinates.longitude = -6.0;
    }
    session.samples[10].coordinates.latitude = 0.0;
    session.samples[10].coordinates.longitude = 0.0;
    session.samples[20].coordinates.latitude = 120.0;

    const auto cache = cosmo::preview::FlightPreviewCache::build(session);

    REQUIRE_FALSE(cache->gpsSampleValid(10));
    REQUIRE_FALSE(cache->gpsSampleValid(20));
    REQUIRE(cache->gpsSampleValid(21));
    REQUIRE(cache->droppedGpsRows() == 2);
    REQUIRE(cache->map2DIndices().size() <= cosmo::preview::FlightPreviewCache::kMap2DPointBudget);
    REQUIRE(cache->map3DIndices().size() <= cosmo::preview::FlightPreviewCache::kMap3DPointBudget);
}

TEST_CASE("FlightPreviewCache replay buckets avoid per-sample rebuilds for large logs", "[preview]") {
    FlightSession session;
    session.samples.resize(10000);
    for (int i = 0; i < static_cast<int>(session.samples.size()); ++i) {
        session.samples[static_cast<std::size_t>(i)].timestamp = i * 10;
    }

    const auto cache = cosmo::preview::FlightPreviewCache::build(session);

    REQUIRE(cache->replayBucket(50, 6000) == 50);
    REQUIRE(cache->replayBucket(6001, 6000) == cache->replayBucket(6002, 6000));
    REQUIRE(cache->replayBucket(9999, 6000) <= cache->replayBucket(10000, 6000));
}
