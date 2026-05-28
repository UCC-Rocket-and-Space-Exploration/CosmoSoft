#pragma once

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "domain/FlightSample.h"
#include "gateway/comms/IComms.h"

namespace cosmo::flightlink {

/** @brief CSV header used by the fake flight-link text telemetry stream. */
inline constexpr std::string_view kTelemetryCsvHeader =
    "time,temp,pressure,altitude,ax,ay,az,gx,gy,gz,anglex,angley,anglez,"
    "latitude,longitude,distance_from_base";

/** @brief Launch-pad latitude for the deterministic fake launch profile. */
inline constexpr std::string_view kLaunchLatitudeText = "51.89350958382806";

/** @brief Launch-pad longitude for the deterministic fake launch profile. */
inline constexpr std::string_view kLaunchLongitudeText = "-8.492074863487407";

/** @brief Launch-pad latitude as a numeric value. */
inline constexpr double kLaunchLatitude = 51.89350958382806;

/** @brief Launch-pad longitude as a numeric value. */
inline constexpr double kLaunchLongitude = -8.492074863487407;

/** @brief First telemetry row emitted by the default fake launch profile. */
inline constexpr std::string_view kExampleTelemetryRow =
    "0.00,18.50,101325.00,0.00,0.000,0.040,9.810,0.000,0.015,0.000,"
    "0.000,0.300,0.000,51.89350958382806,-8.492074863487407,0.00";

/** @brief One fake telemetry packet in both wire-row and decoded-sample form. */
struct LaunchTelemetryPacket {
    std::string line;
    FlightSample sample;
};

/** @brief Format @p value with fixed decimal precision. */
inline std::string formatFixed(double value, int precision) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

/** @brief Remove CR/LF characters from the end of @p text. */
inline std::string stripTrailingLineEnding(std::string_view text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return std::string(text);
}

/** @brief Split a simple comma-separated row; quoted CSV is intentionally unsupported. */
inline std::vector<std::string> splitCsvCells(std::string_view row) {
    const std::string clean = stripTrailingLineEnding(row);
    std::vector<std::string> cells;
    std::string cell;
    for (const char ch : clean) {
        if (ch == ',') {
            cells.push_back(cell);
            cell.clear();
        } else {
            cell.push_back(ch);
        }
    }
    cells.push_back(cell);
    return cells;
}

/** @brief Return true when @p cell parses as a finite floating-point value. */
inline bool isFiniteNumber(std::string_view cell) {
    if (cell.empty()) {
        return false;
    }

    const std::string text(cell);
    char *end = nullptr;
    errno = 0;
    const double value = std::strtod(text.c_str(), &end);
    return errno != ERANGE && end == text.c_str() + text.size()
        && std::isfinite(value);
}

/** @brief Validate one fake telemetry row against the expected 16 numeric fields. */
inline bool isValidTelemetryCsvRow(std::string_view row) {
    constexpr std::size_t kFieldCount = 16;
    const auto cells = splitCsvCells(row);
    if (cells.size() != kFieldCount) {
        return false;
    }
    return std::all_of(cells.begin(), cells.end(), [](const std::string &cell) {
        return isFiniteNumber(cell);
    });
}

/** @brief Build one deterministic launch-like telemetry packet by sample index. */
inline LaunchTelemetryPacket buildLaunchTelemetryPacket(std::size_t index) {
    constexpr double kSamplePeriodSeconds = 0.25;
    constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;

    const double t = static_cast<double>(index) * kSamplePeriodSeconds;
    const double poweredAltitude = 17.5 * t * t;
    const double coastSeconds = std::max(0.0, t - 8.0);
    const double coastAltitude = 1120.0 + 115.0 * coastSeconds
                               - 4.9 * coastSeconds * coastSeconds;
    const double descentSeconds = std::max(0.0, t - 16.0);
    const double altitude = t <= 8.0
        ? poweredAltitude
        : std::max(0.0, coastAltitude - 35.0 * descentSeconds);

    const double temperature = 18.5 - 0.0065 * altitude + 0.15 * std::sin(0.7 * t);
    const double pressure = 101325.0 * std::pow(1.0 - 2.25577e-5 * altitude, 5.25588);
    const double ax = 0.03 * std::sin(t);
    const double ay = 0.04 * std::cos(0.5 * t);
    const double az = t < 0.5 ? 9.81 : (t <= 8.0 ? 26.0 - 0.55 * t : 2.5 - 0.2 * coastSeconds);
    const double gx = 0.025 * std::sin(0.8 * t);
    const double gy = 0.015 * std::cos(0.6 * t);
    const double gz = 0.02 * std::sin(0.45 * t);
    const double angleX = 0.8 * t + 0.15 * std::sin(0.4 * t);
    const double angleY = 0.3 * std::cos(0.3 * t) - 0.15 * t;
    const double angleZ = 1.6 * t;
    const double northMeters = 2.8 * t + 0.015 * altitude;
    const double eastMeters = 1.4 * t + 0.008 * altitude;
    const double distance = std::sqrt(northMeters * northMeters + eastMeters * eastMeters);

    const double latitude = kLaunchLatitude + northMeters / 111111.0;
    const double longitude = kLaunchLongitude
        + eastMeters / (111111.0 * std::cos(kLaunchLatitude * kDegreesToRadians));

    std::ostringstream row;
    row << formatFixed(t, 2) << ','
        << formatFixed(temperature, 2) << ','
        << formatFixed(pressure, 2) << ','
        << formatFixed(altitude, 2) << ','
        << formatFixed(ax, 3) << ','
        << formatFixed(ay, 3) << ','
        << formatFixed(az, 3) << ','
        << formatFixed(gx, 3) << ','
        << formatFixed(gy, 3) << ','
        << formatFixed(gz, 3) << ','
        << formatFixed(angleX, 3) << ','
        << formatFixed(angleY, 3) << ','
        << formatFixed(angleZ, 3) << ',';

    if (index == 0) {
        row << kLaunchLatitudeText << ',' << kLaunchLongitudeText << ',';
    } else {
        row << formatFixed(latitude, 14) << ',' << formatFixed(longitude, 15) << ',';
    }
    row << formatFixed(distance, 2);

    FlightSample sample{};
    sample.timestamp = static_cast<long>(std::lround(t * 1000.0));
    sample.temperature = temperature;
    sample.pressure = pressure;
    sample.altitude = altitude;
    sample.acceleration.x = ax;
    sample.acceleration.y = ay;
    sample.acceleration.z = az;
    sample.angularVelocity.x = gx;
    sample.angularVelocity.y = gy;
    sample.angularVelocity.z = gz;
    sample.coordinates.latitude = index == 0 ? kLaunchLatitude : latitude;
    sample.coordinates.longitude = index == 0 ? kLaunchLongitude : longitude;
    sample.batteryVoltage = std::max(10.8, 12.6 - 0.012 * t);
    sample.rssi = -38.0 - 0.045 * distance;

    return {row.str(), sample};
}

/** @brief Build a deterministic fake launch profile with @p sampleCount packets. */
inline std::vector<LaunchTelemetryPacket> buildLaunchProfilePackets(std::size_t sampleCount = 80) {
    std::vector<LaunchTelemetryPacket> packets;
    packets.reserve(sampleCount);
    for (std::size_t i = 0; i < sampleCount; ++i) {
        packets.push_back(buildLaunchTelemetryPacket(i));
    }
    return packets;
}

/** @brief Fake flight computer that emits deterministic newline-terminated telemetry rows. */
class FakeFlightComputer {
public:
    /** @brief Construct a fake computer using the default deterministic launch profile. */
    FakeFlightComputer()
        : m_packets(buildLaunchProfilePackets())
    {
    }

    /** @brief Construct a fake computer with explicit packets for targeted tests. */
    explicit FakeFlightComputer(std::vector<LaunchTelemetryPacket> packets)
        : m_packets(std::move(packets))
    {
    }

    /** @brief Return the next packet, or std::nullopt after the profile ends. */
    [[nodiscard]] std::optional<LaunchTelemetryPacket> nextPacket() {
        if (m_nextPacket >= m_packets.size()) {
            return std::nullopt;
        }
        return m_packets[m_nextPacket++];
    }

    /** @brief Return the next packet row with a trailing newline. */
    [[nodiscard]] std::optional<std::string> nextPacketLine() {
        auto packet = nextPacket();
        if (!packet) {
            return std::nullopt;
        }
        return packet->line + '\n';
    }

private:
    std::vector<LaunchTelemetryPacket> m_packets;
    std::size_t m_nextPacket = 0;
};

/** @brief In-memory IComms implementation for fake serial behavior. */
class FakeComms final : public IComms {
public:
    /** @brief Open the fake connection. */
    bool open() override {
        m_open = true;
        return true;
    }

    /** @brief Close the fake connection. */
    void close() override {
        m_open = false;
    }

    /** @brief Return whether the fake connection is open. */
    [[nodiscard]] bool isOpen() const override {
        return m_open;
    }

    /** @brief Store bytes written by the caller; returns -1 when closed. */
    ssize_t write(const uint8_t *data, size_t size) override {
        if (!m_open || (!data && size > 0)) {
            return -1;
        }
        if (size == 0) {
            return 0;
        }
        m_written.insert(m_written.end(), data, data + size);
        return static_cast<ssize_t>(size);
    }

    /** @brief Read injected incoming bytes; returns -1 when closed. */
    ssize_t read(uint8_t *buffer, size_t maxSize) override {
        if (!m_open || (!buffer && maxSize > 0)) {
            return -1;
        }
        if (m_incoming.empty() || maxSize == 0) {
            return 0;
        }

        const std::size_t count = std::min(maxSize, m_incoming.size());
        for (std::size_t i = 0; i < count; ++i) {
            buffer[i] = m_incoming.front();
            m_incoming.pop_front();
        }
        return static_cast<ssize_t>(count);
    }

    /** @brief Return a display name for the fake connection. */
    [[nodiscard]] std::string getDeviceName() const override {
        return "FakeComms";
    }

    /** @brief Inject text bytes that will be returned by read(). */
    void injectIncoming(std::string_view bytes) {
        for (const char ch : bytes) {
            m_incoming.push_back(static_cast<uint8_t>(ch));
        }
    }

    /** @brief Inject raw bytes that will be returned by read(). */
    void injectIncoming(const std::vector<uint8_t> &bytes) {
        for (const uint8_t byte : bytes) {
            m_incoming.push_back(byte);
        }
    }

    /** @brief Return bytes written by the caller as text. */
    [[nodiscard]] std::string writtenText() const {
        return std::string(m_written.begin(), m_written.end());
    }

private:
    bool m_open = false;
    std::deque<uint8_t> m_incoming;
    std::vector<uint8_t> m_written;
};

/** @brief Fake ground station that chunks flight-computer rows like a serial stream. */
class FakeGroundStation {
public:
    /** @brief Split @p packetLine into deterministic serial chunks of @p chunkSize. */
    [[nodiscard]] std::vector<std::vector<uint8_t>> chunkPacketLine(
        std::string_view packetLine,
        std::size_t chunkSize) const
    {
        if (chunkSize == 0) {
            throw std::invalid_argument("chunkSize must be greater than zero");
        }

        std::vector<std::vector<uint8_t>> chunks;
        for (std::size_t offset = 0; offset < packetLine.size(); offset += chunkSize) {
            const std::size_t count = std::min(chunkSize, packetLine.size() - offset);
            chunks.emplace_back(
                reinterpret_cast<const uint8_t *>(packetLine.data() + offset),
                reinterpret_cast<const uint8_t *>(packetLine.data() + offset + count));
        }
        return chunks;
    }

    /** @brief Forward the next flight-computer row into fake comms as serial chunks. */
    bool forwardNextPacket(
        FakeFlightComputer &computer,
        FakeComms &comms,
        std::size_t chunkSize) const
    {
        const auto packetLine = computer.nextPacketLine();
        if (!packetLine) {
            return false;
        }
        for (const auto &chunk : chunkPacketLine(*packetLine, chunkSize)) {
            comms.injectIncoming(chunk);
        }
        return true;
    }
};

} // namespace cosmo::flightlink
