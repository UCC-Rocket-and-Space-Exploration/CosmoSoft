/**
 * @file LineTelemetryDecoder.h
 * @brief CSV line decoder for live text telemetry packets.
 *
 * This decoder supports the current fake flight-link wire format while the
 * binary Framer/Parser protocol remains reserved for the future hardware
 * protocol implementation.
 */

#ifndef COSMO_SOFT_LINETELEMETRYDECODER_H
#define COSMO_SOFT_LINETELEMETRYDECODER_H

#include "domain/FlightSample.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cosmo::telemetry {

/** @brief Number of numeric columns in the live CSV telemetry row. */
inline constexpr std::size_t kLiveTelemetryCsvFieldCount = 16;

/** @brief CSV header emitted by the current text telemetry stream. */
inline constexpr std::string_view kLiveTelemetryCsvHeader =
    "time,temp,pressure,altitude,ax,ay,az,gx,gy,gz,anglex,angley,anglez,"
    "latitude,longitude,distance_from_base";

/** @brief Remove trailing CR/LF bytes from a serial text line. */
inline std::string stripLineEnding(std::string_view line) {
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.remove_suffix(1);
    }
    return std::string(line);
}

/** @brief Split an unquoted comma-separated telemetry line into cells. */
inline std::vector<std::string> splitTelemetryCsvCells(std::string_view line) {
    const std::string clean = stripLineEnding(line);
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

/** @brief Parse a finite floating-point value, rejecting partial parses. */
inline std::optional<double> parseFiniteDouble(std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }

    const std::string valueText(text);
    char *end = nullptr;
    errno = 0;
    const double value = std::strtod(valueText.c_str(), &end);
    if (errno == ERANGE || end != valueText.c_str() + valueText.size() || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

/**
 * @brief Decode one live telemetry CSV row into a FlightSample.
 * @return Decoded sample, or std::nullopt when the row is a header or malformed.
 */
inline std::optional<FlightSample> decodeTelemetryCsvRow(std::string_view row) {
    const std::string clean = stripLineEnding(row);
    if (clean.empty() || clean == kLiveTelemetryCsvHeader) {
        return std::nullopt;
    }

    const auto cells = splitTelemetryCsvCells(clean);
    if (cells.size() != kLiveTelemetryCsvFieldCount) {
        return std::nullopt;
    }

    std::vector<double> values;
    values.reserve(cells.size());
    for (const auto &cell : cells) {
        auto parsed = parseFiniteDouble(cell);
        if (!parsed) {
            return std::nullopt;
        }
        values.push_back(*parsed);
    }

    FlightSample sample{};
    sample.timestamp = static_cast<long>(std::llround(values[0] * 1000.0));
    sample.temperature = values[1];
    sample.pressure = values[2];
    sample.altitude = values[3];
    sample.acceleration.x = values[4];
    sample.acceleration.y = values[5];
    sample.acceleration.z = values[6];
    sample.angularVelocity.x = values[7];
    sample.angularVelocity.y = values[8];
    sample.angularVelocity.z = values[9];
    sample.coordinates.latitude = values[13];
    sample.coordinates.longitude = values[14];
    return sample;
}

/**
 * @class LineTelemetryDecoder
 * @brief Accumulates serial chunks and decodes complete newline-delimited rows.
 */
class LineTelemetryDecoder {
public:
    /** @brief Reset pending partial line and malformed-line count. */
    void reset() {
        m_pending.clear();
        m_malformedLines = 0;
    }

    /** @brief Return the cumulative number of malformed non-empty rows seen. */
    [[nodiscard]] std::size_t malformedLineCount() const { return m_malformedLines; }

    /**
     * @brief Ingest a raw byte chunk and return all complete decoded samples.
     * Header and empty lines are ignored. Malformed data rows increment
     * malformedLineCount().
     */
    std::vector<FlightSample> ingest(const uint8_t *data, std::size_t size) {
        std::vector<FlightSample> samples;
        if (!data || size == 0) {
            return samples;
        }

        m_pending.append(reinterpret_cast<const char *>(data), size);
        std::size_t lineStart = 0;
        while (true) {
            const std::size_t lineEnd = m_pending.find('\n', lineStart);
            if (lineEnd == std::string::npos) {
                break;
            }

            processLine(std::string_view(m_pending).substr(lineStart, lineEnd - lineStart), samples);
            lineStart = lineEnd + 1;
        }

        if (lineStart > 0) {
            m_pending.erase(0, lineStart);
        }
        return samples;
    }

private:
    void processLine(std::string_view line, std::vector<FlightSample> &samples) {
        const std::string clean = stripLineEnding(line);
        if (clean.empty() || clean == kLiveTelemetryCsvHeader) {
            return;
        }

        auto sample = decodeTelemetryCsvRow(clean);
        if (sample) {
            samples.push_back(*sample);
            return;
        }
        ++m_malformedLines;
    }

    std::string m_pending;
    std::size_t m_malformedLines = 0;
};

} // namespace cosmo::telemetry

#endif // COSMO_SOFT_LINETELEMETRYDECODER_H
