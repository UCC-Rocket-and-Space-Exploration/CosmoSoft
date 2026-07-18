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
#include <array>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cosmo::telemetry {

/** @brief Number of numeric columns in the live CSV telemetry row. */
inline constexpr std::size_t kLiveTelemetryCsvFieldCount = 16;

/** @brief Maximum buffered bytes accepted for one live telemetry line. */
inline constexpr std::size_t kMaxLiveTelemetryLineBytes = 64U * 1024U;

/** @brief Generous absolute bound for numeric live telemetry fields. */
inline constexpr double kMaxLiveTelemetryMagnitude = 1.0e12;

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

    std::array<double, kLiveTelemetryCsvFieldCount> values{};
    std::size_t cellStart = 0;
    for (std::size_t index = 0; index < values.size(); ++index) {
        const std::size_t separator = clean.find(',', cellStart);
        const bool isLastCell = index + 1 == values.size();
        if ((!isLastCell && separator == std::string::npos)
            || (isLastCell && separator != std::string::npos)) {
            return std::nullopt;
        }

        const std::size_t cellEnd = isLastCell ? clean.size() : separator;
        auto parsed = parseFiniteDouble(
            std::string_view(clean).substr(cellStart, cellEnd - cellStart));
        if (!parsed) {
            return std::nullopt;
        }
        values[index] = *parsed;
        cellStart = cellEnd + 1;
    }

    if (std::any_of(values.begin(), values.end(), [](const double value) {
            return std::abs(value) > kMaxLiveTelemetryMagnitude;
        })) {
        return std::nullopt;
    }

    const double timestampMilliseconds = values[0] * 1000.0;
    if (!std::isfinite(timestampMilliseconds)
        // Strict bounds avoid rounding a double representation of LONG_MAX
        // into an out-of-range integer on platforms where long has 64 bits.
        || timestampMilliseconds <= static_cast<double>(std::numeric_limits<long>::lowest())
        || timestampMilliseconds >= static_cast<double>(std::numeric_limits<long>::max())
        || values[13] < -90.0
        || values[13] > 90.0
        || values[14] < -180.0
        || values[14] > 180.0) {
        return std::nullopt;
    }

    FlightSample sample{};
    sample.timestamp = static_cast<long>(std::llround(timestampMilliseconds));
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
    /**
     * @brief Construct a line decoder with a bounded partial-line buffer.
     * @param maxLineBytes Maximum bytes accepted before a line is discarded.
     */
    explicit LineTelemetryDecoder(std::size_t maxLineBytes = kMaxLiveTelemetryLineBytes)
        : m_maxLineBytes(std::max<std::size_t>(1, maxLineBytes)) {
        m_pending.reserve(std::min<std::size_t>(m_maxLineBytes, 1024));
    }

    /** @brief Reset pending partial line and malformed-line count. */
    void reset() {
        m_pending.clear();
        m_malformedLines = 0;
        m_discardingDamagedLine = false;
    }

    /** @brief Return the cumulative number of malformed non-empty rows seen. */
    [[nodiscard]] std::size_t malformedLineCount() const { return m_malformedLines; }

    /**
     * @brief Invalidate a line spanning a known byte-stream gap.
     *
     * Pending prefix bytes are cleared and subsequent bytes are ignored through
     * the next newline so separated fragments can never be spliced together.
     * Queue-drop counters report the gap, so this does not increment the
     * malformed-line counter.
     */
    void notifyDataGap() {
        m_pending.clear();
        m_discardingDamagedLine = true;
    }

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

        for (std::size_t index = 0; index < size; ++index) {
            const char byte = static_cast<char>(data[index]);

            if (m_discardingDamagedLine) {
                if (byte == '\n') {
                    m_discardingDamagedLine = false;
                }
                continue;
            }

            if (byte == '\n') {
                processLine(m_pending, samples);
                m_pending.clear();
                continue;
            }

            if (m_pending.size() >= m_maxLineBytes) {
                m_pending.clear();
                m_discardingDamagedLine = true;
                ++m_malformedLines;
                continue;
            }

            m_pending.push_back(byte);
        }
        return samples;
    }

    /**
     * @brief Finish the stream and decode a final row without a trailing newline.
     * @return Zero or one decoded samples. A truncated non-empty row increments
     * malformedLineCount().
     */
    std::vector<FlightSample> finish() {
        std::vector<FlightSample> samples;
        if (!m_discardingDamagedLine && !m_pending.empty()) {
            processLine(m_pending, samples);
        }
        m_pending.clear();
        m_discardingDamagedLine = false;
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
    std::size_t m_maxLineBytes;
    bool m_discardingDamagedLine = false;
};

} // namespace cosmo::telemetry

#endif // COSMO_SOFT_LINETELEMETRYDECODER_H
