#include "services/import/SampleFileLoader.h"

#include "services/telemetry/Framer.h"
#include "services/telemetry/Parser.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string_view>

namespace {

void trimInPlace(std::string &s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.erase(s.begin());
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
}

std::vector<std::string> splitCsvLine(const std::string &line) {
    std::vector<std::string> cells;
    std::string cur;
    for (char c : line) {
        if (c == ',') {
            trimInPlace(cur);
            cells.push_back(std::move(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    trimInPlace(cur);
    cells.push_back(std::move(cur));
    return cells;
}

int findColumn(const std::vector<std::string> &header, const char *name) {
    for (std::size_t i = 0; i < header.size(); ++i) {
        std::string h = header[i];
        trimInPlace(h);
        for (char &ch : h) {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        if (h == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int findColumnAny(
    const std::vector<std::string> &header,
    std::initializer_list<const char *> names) {
    for (const char *name : names) {
        const int column = findColumn(header, name);
        if (column >= 0) {
            return column;
        }
    }
    return -1;
}

bool parseDouble(const std::string &s, double &out) {
    if (s.empty()) {
        return false;
    }
    std::istringstream iss(s);
    iss >> out;
    return !iss.fail();
}

bool parseCellDouble(const std::vector<std::string> &cells, int index, double &out) {
    return index >= 0
        && static_cast<int>(cells.size()) > index
        && parseDouble(cells[static_cast<std::size_t>(index)], out);
}

int hexNibble(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return -1;
}

std::optional<std::vector<uint8_t>> hexDecodeLine(std::string_view hex) {
    std::string s(hex.begin(), hex.end());
    trimInPlace(s);
    if (s.empty() || (s.size() % 2) != 0) {
        return std::nullopt;
    }
    std::vector<uint8_t> out;
    out.reserve(s.size() / 2);
    for (std::size_t i = 0; i < s.size(); i += 2) {
        const int hi = hexNibble(s[i]);
        const int lo = hexNibble(s[i + 1]);
        if (hi < 0 || lo < 0) {
            return std::nullopt;
        }
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return out;
}

} // namespace

std::optional<std::string> SampleFileLoader::loadTheseusCsv(const std::string &path, FlightSession &out) {
    std::ifstream in(path);
    if (!in) {
        return std::string("Could not open file: ") + path;
    }

    std::string line;
    std::vector<std::string> header;
    while (std::getline(in, line)) {
        trimInPlace(line);
        if (line.empty()) {
            continue;
        }
        if (!line.empty() && line[0] == '#') {
            line.erase(line.begin());
            trimInPlace(line);
        }
        if (line.empty()) {
            continue;
        }
        header = splitCsvLine(line);
        break;
    }
    if (header.empty()) {
        return std::string("CSV has no header row: ") + path;
    }

    const int iTimeMs = findColumnAny(header, {"time_ms", "timestamp_ms"});
    const int iTimeSec = findColumnAny(header, {"time", "time_s", "timestamp"});
    const int iTime = iTimeMs >= 0 ? iTimeMs : iTimeSec;
    const bool timeInMilliseconds = iTimeMs >= 0;
    const int iRssi = findColumn(header, "rssi");
    const int iAccel = findColumnAny(header, {"acceleration", "accel"});
    const int iAccelX = findColumnAny(header, {"acceleration_x", "accel_x", "ax", "ax_ms2"});
    const int iAccelY = findColumnAny(header, {"acceleration_y", "accel_y", "ay", "ay_ms2"});
    const int iAccelZ = findColumnAny(header, {"acceleration_z", "accel_z", "az", "az_ms2"});
    const int iGyroX = findColumnAny(header, {"angular_velocity_x", "gyro_x", "gx", "gx_rads"});
    const int iGyroY = findColumnAny(header, {"angular_velocity_y", "gyro_y", "gy", "gy_rads"});
    const int iGyroZ = findColumnAny(header, {"angular_velocity_z", "gyro_z", "gz", "gz_rads"});
    const int iPress = findColumnAny(header, {"pressure", "pressure_pa"});
    const int iAlt = findColumnAny(header, {"altitude", "altitude_m", "alt_m"});
    const int iTemp = findColumnAny(header, {"temperature", "temperature_c", "temp", "temp_c"});
    const int iBatt = findColumnAny(header, {"battery_voltage", "battery", "voltage", "main_voltage"});
    const int iLat  = findColumnAny(header, {"latitude", "lat"});
    const int iLon  = findColumnAny(header, {"longitude", "lon", "lng"});
    if (iTime < 0 || iAlt < 0) {
        return std::string("CSV missing required columns (need time/time_ms and altitude/altitude_m): ") + path;
    }

    std::vector<FlightSample> samples;
    while (std::getline(in, line)) {
        trimInPlace(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const auto cells = splitCsvLine(line);
        if (cells.size() < header.size()) {
            continue;
        }

        FlightSample sample{};
        double timeValue = 0;
        if (parseCellDouble(cells, iTime, timeValue)) {
            sample.timestamp = static_cast<long>(
                std::lround(timeInMilliseconds ? timeValue : timeValue * 1000.0));
        }
        parseCellDouble(cells, iRssi, sample.rssi);
        parseCellDouble(cells, iPress, sample.pressure);
        parseCellDouble(cells, iAlt, sample.altitude);
        parseCellDouble(cells, iTemp, sample.temperature);
        parseCellDouble(cells, iBatt, sample.batteryVoltage);
        parseCellDouble(cells, iLat, sample.coordinates.latitude);
        parseCellDouble(cells, iLon, sample.coordinates.longitude);

        double a = 0;
        if (parseCellDouble(cells, iAccel, a)) {
            sample.acceleration.z = a;
        }
        parseCellDouble(cells, iAccelX, sample.acceleration.x);
        parseCellDouble(cells, iAccelY, sample.acceleration.y);
        parseCellDouble(cells, iAccelZ, sample.acceleration.z);
        parseCellDouble(cells, iGyroX, sample.angularVelocity.x);
        parseCellDouble(cells, iGyroY, sample.angularVelocity.y);
        parseCellDouble(cells, iGyroZ, sample.angularVelocity.z);
        samples.push_back(sample);
    }

    if (samples.empty()) {
        return std::string("No data rows in CSV: ") + path;
    }

    out.samples = std::move(samples);
    // Trace panel rows only for columns that exist in this file (see FlightSession::metricsInSource order).
    const bool hasAcceleration = iAccel >= 0
        || iAccelX >= 0
        || iAccelY >= 0
        || iAccelZ >= 0;
    const bool hasGyro = iGyroX >= 0 || iGyroY >= 0 || iGyroZ >= 0;
    out.metricsInSource = {
        true,               // 0 altitude — required column
        iTemp >= 0,         // 1 temperature
        iPress >= 0,        // 2 pressure
        hasAcceleration,    // 3 |acceleration|
        iBatt >= 0,         // 4 battery
        iRssi >= 0,         // 5 RSSI
        hasGyro,            // 6 gyro
        iLat >= 0,          // 7 latitude
        iLon >= 0,          // 8 longitude
    };
    return std::nullopt;
}

std::optional<std::string> SampleFileLoader::loadTelemFile(
    const std::string &path,
    FlightSession &out,
    Framer &framer,
    Parser &parser) {
    std::ifstream in(path);
    if (!in) {
        return std::string("Could not open file: ") + path;
    }

    std::string line;
    constexpr std::string_view kPrefix = "TELEM ";
    while (std::getline(in, line)) {
        trimInPlace(line);
        if (line.size() < kPrefix.size()) {
            continue;
        }
        if (line.compare(0, kPrefix.size(), kPrefix) != 0) {
            continue;
        }
        const std::string_view hexPart(line.data() + kPrefix.size(), line.size() - kPrefix.size());
        auto bytesOpt = hexDecodeLine(hexPart);
        if (!bytesOpt || bytesOpt->empty()) {
            continue;
        }
        framer.ingest(bytesOpt->data(), bytesOpt->size());
        Frame frame{};
        while (framer.try_next_frame(frame)) {
            auto decoded = parser.decode(frame);
            if (decoded) {
                out.samples.push_back(std::move(*decoded));
            }
        }
    }

    if (out.samples.empty()) {
        return std::string(
            "No telemetry samples decoded from TELEM file. "
            "Framer/Parser must implement the binary protocol for this to produce data.");
    }
    return std::nullopt;
}
