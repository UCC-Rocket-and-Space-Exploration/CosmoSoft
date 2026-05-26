#include <catch2/catch_test_macros.hpp>

#include "services/flight/FakeFlightLink.h"

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
