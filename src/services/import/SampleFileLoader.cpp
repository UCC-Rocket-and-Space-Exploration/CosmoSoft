#include "services/import/SampleFileLoader.h"

#include "services/telemetry/Framer.h"
#include "services/telemetry/Parser.h"

#include <algorithm>
#include <cctype>
#include <cmath> //added
#include <fstream>
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

bool parseDouble(const std::string &s, double &out) {
    if (s.empty()) {
        return false;
    }
    std::istringstream iss(s);
    iss >> out;
    return !iss.fail();
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

    const int iTime = findColumn(header, "time");
    const int iRssi = findColumn(header, "rssi");
    const int iAccel = findColumn(header, "acceleration");
    const int iPress = findColumn(header, "pressure");
    const int iAlt = findColumn(header, "altitude");
    const int iTemp = findColumn(header, "temperature");
    const int iBatt = findColumn(header, "battery_voltage");
    const int iLat  = findColumn(header, "latitude");
    const int iLon  = findColumn(header, "longitude");
    if (iTime < 0 || iAlt < 0) {
        return std::string("CSV missing required columns (need time, altitude): ") + path;
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
        double tSec = 0;
        if (static_cast<int>(cells.size()) > iTime && parseDouble(cells[static_cast<std::size_t>(iTime)], tSec)) {
            sample.timestamp = static_cast<long>(std::lround(tSec * 1000.0)); // fixed by including cmath
        }
        if (iRssi >= 0 && static_cast<int>(cells.size()) > iRssi) {
            parseDouble(cells[static_cast<std::size_t>(iRssi)], sample.rssi);
        }
        if (iAccel >= 0 && static_cast<int>(cells.size()) > iAccel) {
            double a = 0;
            if (parseDouble(cells[static_cast<std::size_t>(iAccel)], a)) {
                sample.acceleration.z = a;
            }
        }
        if (iPress >= 0 && static_cast<int>(cells.size()) > iPress) {
            parseDouble(cells[static_cast<std::size_t>(iPress)], sample.pressure);
        }
        if (static_cast<int>(cells.size()) > iAlt) {
            parseDouble(cells[static_cast<std::size_t>(iAlt)], sample.altitude);
        }
        if (iTemp >= 0 && static_cast<int>(cells.size()) > iTemp) {
            parseDouble(cells[static_cast<std::size_t>(iTemp)], sample.temperature);
        }
        if (iBatt >= 0 && static_cast<int>(cells.size()) > iBatt) {
            parseDouble(cells[static_cast<std::size_t>(iBatt)], sample.batteryVoltage);
        }
        if (iLat >= 0 && static_cast<int>(cells.size()) > iLat) {
            parseDouble(cells[static_cast<std::size_t>(iLat)], sample.coordinates.latitude);
        }
        if (iLon >= 0 && static_cast<int>(cells.size()) > iLon) {
            parseDouble(cells[static_cast<std::size_t>(iLon)], sample.coordinates.longitude);
        }
        samples.push_back(sample);
    }

    if (samples.empty()) {
        return std::string("No data rows in CSV: ") + path;
    }

    out.samples = std::move(samples);
    // Trace panel rows only for columns that exist in this file (see FlightSession::metricsInSource order).
    out.metricsInSource = {
        true,               // 0 altitude — required column
        iTemp >= 0,         // 1 temperature
        iPress >= 0,        // 2 pressure
        iAccel >= 0,        // 3 |acceleration| (single-axis column in CSV)
        iBatt >= 0,         // 4 battery
        iRssi >= 0,         // 5 RSSI
        false,              // 6 gyro — not in Theseus CSV schema
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