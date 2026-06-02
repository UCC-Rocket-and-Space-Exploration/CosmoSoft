#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "services/flight/FakeFlightLink.h"
#include "services/telemetry/LineTelemetryDecoder.h"

#include <array>
#include <cstdlib>
#include <cstdint>
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
