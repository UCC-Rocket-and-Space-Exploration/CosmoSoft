#include "services/import/SampleFileLoader.h"

#include "services/telemetry/Framer.h"
#include "services/telemetry/Parser.h"

#include <QByteArray>
#include <QString>
#include <QXmlStreamReader>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string_view>
#include <vector>

#include <zlib.h>

namespace {

constexpr std::uint32_t kZipLocalFileHeaderSignature = 0x04034b50;
constexpr std::uint32_t kZipCentralDirectorySignature = 0x02014b50;
constexpr std::uint32_t kZipEndOfCentralDirectorySignature = 0x06054b50;
constexpr std::uint16_t kZipCompressionStored = 0;
constexpr std::uint16_t kZipCompressionDeflated = 8;
constexpr std::size_t kMaxXlsxEntryBytes = 64U * 1024U * 1024U;

struct ZipEntry {
    std::uint16_t compression = 0;
    std::uint32_t compressedSize = 0;
    std::uint32_t uncompressedSize = 0;
    std::uint32_t localHeaderOffset = 0;
};

struct TelemetryColumns {
    int iTime = -1;
    bool timeInMilliseconds = false;
    int iRssi = -1;
    int iAccel = -1;
    int iAccelX = -1;
    int iAccelY = -1;
    int iAccelZ = -1;
    int iGyroX = -1;
    int iGyroY = -1;
    int iGyroZ = -1;
    int iPress = -1;
    int iAlt = -1;
    int iTemp = -1;
    int iBatt = -1;
    int iLat = -1;
    int iLon = -1;
};

std::uint16_t readLe16(const std::vector<std::uint8_t> &bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes[offset])
        | (static_cast<std::uint16_t>(bytes[offset + 1]) << 8));
}

std::uint32_t readLe32(const std::vector<std::uint8_t> &bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset])
        | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8)
        | (static_cast<std::uint32_t>(bytes[offset + 2]) << 16)
        | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

bool rangeFits(std::size_t size, std::size_t offset, std::size_t count) {
    return offset <= size && count <= size - offset;
}

std::optional<std::vector<std::uint8_t>> readFileBytes(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }

    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0 || static_cast<std::uint64_t>(size) > std::numeric_limits<std::size_t>::max()) {
        return std::nullopt;
    }
    in.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        in.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    if (!in && !in.eof()) {
        return std::nullopt;
    }
    return bytes;
}

std::optional<std::size_t> findEndOfCentralDirectory(const std::vector<std::uint8_t> &bytes) {
    if (bytes.size() < 22) {
        return std::nullopt;
    }

    const std::size_t maxCommentLength = 0xffff;
    const std::size_t searchStart = bytes.size() > 22 + maxCommentLength
        ? bytes.size() - 22 - maxCommentLength
        : 0;
    for (std::size_t offset = bytes.size() - 22 + 1; offset-- > searchStart;) {
        if (readLe32(bytes, offset) == kZipEndOfCentralDirectorySignature) {
            return offset;
        }
        if (offset == 0) {
            break;
        }
    }
    return std::nullopt;
}

std::optional<std::map<std::string, ZipEntry>> readZipCentralDirectory(
    const std::vector<std::uint8_t> &bytes,
    std::string &error) {
    const auto eocdOffset = findEndOfCentralDirectory(bytes);
    if (!eocdOffset) {
        error = "XLSX ZIP directory not found";
        return std::nullopt;
    }
    if (!rangeFits(bytes.size(), *eocdOffset, 22)) {
        error = "XLSX ZIP directory is truncated";
        return std::nullopt;
    }

    const std::uint16_t entryCount = readLe16(bytes, *eocdOffset + 10);
    const std::uint32_t centralDirectorySize = readLe32(bytes, *eocdOffset + 12);
    const std::uint32_t centralDirectoryOffset = readLe32(bytes, *eocdOffset + 16);
    if (!rangeFits(bytes.size(), centralDirectoryOffset, centralDirectorySize)) {
        error = "XLSX ZIP directory points outside the file";
        return std::nullopt;
    }

    std::map<std::string, ZipEntry> entries;
    std::size_t offset = centralDirectoryOffset;
    for (std::uint16_t i = 0; i < entryCount; ++i) {
        if (!rangeFits(bytes.size(), offset, 46)
            || readLe32(bytes, offset) != kZipCentralDirectorySignature) {
            error = "XLSX ZIP directory entry is invalid";
            return std::nullopt;
        }

        ZipEntry entry;
        entry.compression = readLe16(bytes, offset + 10);
        entry.compressedSize = readLe32(bytes, offset + 20);
        entry.uncompressedSize = readLe32(bytes, offset + 24);
        const std::uint16_t nameLength = readLe16(bytes, offset + 28);
        const std::uint16_t extraLength = readLe16(bytes, offset + 30);
        const std::uint16_t commentLength = readLe16(bytes, offset + 32);
        entry.localHeaderOffset = readLe32(bytes, offset + 42);

        const std::size_t nameOffset = offset + 46;
        if (!rangeFits(bytes.size(), nameOffset, nameLength)) {
            error = "XLSX ZIP entry name points outside the file";
            return std::nullopt;
        }
        const std::string name(
            reinterpret_cast<const char *>(bytes.data() + nameOffset),
            nameLength);
        entries[name] = entry;

        const std::size_t nextOffset = nameOffset
            + static_cast<std::size_t>(nameLength)
            + static_cast<std::size_t>(extraLength)
            + static_cast<std::size_t>(commentLength);
        if (nextOffset < offset || nextOffset > bytes.size()) {
            error = "XLSX ZIP directory entry length is invalid";
            return std::nullopt;
        }
        offset = nextOffset;
    }

    return entries;
}

std::optional<std::vector<std::uint8_t>> unzipEntry(
    const std::vector<std::uint8_t> &bytes,
    const std::map<std::string, ZipEntry> &entries,
    const std::string &name,
    std::string &error) {
    const auto it = entries.find(name);
    if (it == entries.end()) {
        error = "XLSX ZIP entry missing: " + name;
        return std::nullopt;
    }
    const ZipEntry &entry = it->second;
    if (entry.uncompressedSize > kMaxXlsxEntryBytes) {
        error = "XLSX ZIP entry too large: " + name;
        return std::nullopt;
    }
    if (!rangeFits(bytes.size(), entry.localHeaderOffset, 30)
        || readLe32(bytes, entry.localHeaderOffset) != kZipLocalFileHeaderSignature) {
        error = "XLSX ZIP local header is invalid: " + name;
        return std::nullopt;
    }

    const std::uint16_t localNameLength = readLe16(bytes, entry.localHeaderOffset + 26);
    const std::uint16_t localExtraLength = readLe16(bytes, entry.localHeaderOffset + 28);
    const std::size_t dataOffset = static_cast<std::size_t>(entry.localHeaderOffset)
        + 30U
        + static_cast<std::size_t>(localNameLength)
        + static_cast<std::size_t>(localExtraLength);
    if (!rangeFits(bytes.size(), dataOffset, entry.compressedSize)) {
        error = "XLSX ZIP entry data points outside the file: " + name;
        return std::nullopt;
    }

    const auto *compressed = reinterpret_cast<const Bytef *>(bytes.data() + dataOffset);
    std::vector<std::uint8_t> out(entry.uncompressedSize);
    if (entry.compression == kZipCompressionStored) {
        if (entry.compressedSize != entry.uncompressedSize) {
            error = "XLSX ZIP stored entry size mismatch: " + name;
            return std::nullopt;
        }
        std::copy(
            bytes.begin() + static_cast<std::ptrdiff_t>(dataOffset),
            bytes.begin() + static_cast<std::ptrdiff_t>(dataOffset + entry.compressedSize),
            out.begin());
        return out;
    }
    if (entry.compression != kZipCompressionDeflated) {
        error = "XLSX ZIP entry uses unsupported compression: " + name;
        return std::nullopt;
    }

    z_stream stream{};
    stream.next_in = const_cast<Bytef *>(compressed);
    stream.avail_in = entry.compressedSize;
    stream.next_out = reinterpret_cast<Bytef *>(out.data());
    stream.avail_out = entry.uncompressedSize;
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        error = "Could not initialize XLSX deflate reader";
        return std::nullopt;
    }
    const int inflateResult = inflate(&stream, Z_FINISH);
    inflateEnd(&stream);
    if (inflateResult != Z_STREAM_END || stream.total_out != entry.uncompressedSize) {
        error = "Could not inflate XLSX ZIP entry: " + name;
        return std::nullopt;
    }
    return out;
}

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

std::optional<TelemetryColumns> resolveTelemetryColumns(
    const std::vector<std::string> &header,
    const std::string &sourceName,
    std::string &error) {
    TelemetryColumns columns;
    const int iTimeMs = findColumnAny(header, {"time_ms", "timestamp_ms"});
    const int iTimeSec = findColumnAny(header, {"time", "time_s", "timestamp"});
    columns.iTime = iTimeMs >= 0 ? iTimeMs : iTimeSec;
    columns.timeInMilliseconds = iTimeMs >= 0;
    columns.iRssi = findColumn(header, "rssi");
    columns.iAccel = findColumnAny(header, {"acceleration", "accel"});
    columns.iAccelX = findColumnAny(header, {"acceleration_x", "accel_x", "ax", "ax_ms2"});
    columns.iAccelY = findColumnAny(header, {"acceleration_y", "accel_y", "ay", "ay_ms2"});
    columns.iAccelZ = findColumnAny(header, {"acceleration_z", "accel_z", "az", "az_ms2"});
    columns.iGyroX = findColumnAny(header, {"angular_velocity_x", "gyro_x", "gx", "gx_rads"});
    columns.iGyroY = findColumnAny(header, {"angular_velocity_y", "gyro_y", "gy", "gy_rads"});
    columns.iGyroZ = findColumnAny(header, {"angular_velocity_z", "gyro_z", "gz", "gz_rads"});
    columns.iPress = findColumnAny(header, {"pressure", "pressure_pa"});
    columns.iAlt = findColumnAny(header, {"altitude", "altitude_m", "alt_m"});
    columns.iTemp = findColumnAny(header, {"temperature", "temperature_c", "temp", "temp_c"});
    columns.iBatt = findColumnAny(header, {"battery_voltage", "battery", "voltage", "main_voltage"});
    columns.iLat = findColumnAny(header, {"latitude", "lat"});
    columns.iLon = findColumnAny(header, {"longitude", "lon", "lng"});

    if (columns.iTime < 0 || columns.iAlt < 0) {
        error = "Telemetry source missing required columns (need time/time_ms and altitude/altitude_m): "
            + sourceName;
        return std::nullopt;
    }
    return columns;
}

FlightSample sampleFromCells(const std::vector<std::string> &cells, const TelemetryColumns &columns) {
    FlightSample sample{};
    double timeValue = 0;
    if (parseCellDouble(cells, columns.iTime, timeValue)) {
        sample.timestamp = static_cast<long>(
            std::lround(columns.timeInMilliseconds ? timeValue : timeValue * 1000.0));
    }
    parseCellDouble(cells, columns.iRssi, sample.rssi);
    parseCellDouble(cells, columns.iPress, sample.pressure);
    parseCellDouble(cells, columns.iAlt, sample.altitude);
    parseCellDouble(cells, columns.iTemp, sample.temperature);
    parseCellDouble(cells, columns.iBatt, sample.batteryVoltage);
    parseCellDouble(cells, columns.iLat, sample.coordinates.latitude);
    parseCellDouble(cells, columns.iLon, sample.coordinates.longitude);

    double acceleration = 0;
    if (parseCellDouble(cells, columns.iAccel, acceleration)) {
        sample.acceleration.z = acceleration;
    }
    parseCellDouble(cells, columns.iAccelX, sample.acceleration.x);
    parseCellDouble(cells, columns.iAccelY, sample.acceleration.y);
    parseCellDouble(cells, columns.iAccelZ, sample.acceleration.z);
    parseCellDouble(cells, columns.iGyroX, sample.angularVelocity.x);
    parseCellDouble(cells, columns.iGyroY, sample.angularVelocity.y);
    parseCellDouble(cells, columns.iGyroZ, sample.angularVelocity.z);
    return sample;
}

void applyMetricsInSource(FlightSession &out, const TelemetryColumns &columns) {
    const bool hasAcceleration = columns.iAccel >= 0
        || columns.iAccelX >= 0
        || columns.iAccelY >= 0
        || columns.iAccelZ >= 0;
    const bool hasGyro = columns.iGyroX >= 0
        || columns.iGyroY >= 0
        || columns.iGyroZ >= 0;
    out.metricsInSource = {
        true,                       // 0 altitude — required column
        columns.iTemp >= 0,         // 1 temperature
        columns.iPress >= 0,        // 2 pressure
        hasAcceleration,            // 3 |acceleration|
        columns.iBatt >= 0,         // 4 battery
        columns.iRssi >= 0,         // 5 RSSI
        hasGyro,                    // 6 gyro
        columns.iLat >= 0,          // 7 latitude
        columns.iLon >= 0,          // 8 longitude
    };
}

QByteArray byteArrayFromBytes(const std::vector<std::uint8_t> &bytes) {
    return QByteArray(
        reinterpret_cast<const char *>(bytes.data()),
        static_cast<qsizetype>(bytes.size()));
}

QString readTextRuns(QXmlStreamReader &xml, QStringView endElementName) {
    QString text;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == QStringLiteral("t")) {
            text += xml.readElementText();
        } else if (xml.isEndElement() && xml.name() == endElementName) {
            break;
        }
    }
    return text;
}

std::optional<std::vector<std::string>> readSharedStrings(
    const std::vector<std::uint8_t> &bytes,
    std::string &error) {
    std::vector<std::string> strings;
    QXmlStreamReader xml(byteArrayFromBytes(bytes));
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == QStringLiteral("si")) {
            strings.push_back(readTextRuns(xml, QStringLiteral("si")).toStdString());
        }
    }
    if (xml.hasError()) {
        error = "XLSX shared strings XML parse error: " + xml.errorString().toStdString();
        return std::nullopt;
    }
    return strings;
}

std::optional<std::size_t> columnIndexFromCellRef(const QString &ref) {
    std::size_t value = 0;
    bool sawLetter = false;
    for (const QChar ch : ref) {
        const ushort code = ch.toUpper().unicode();
        if (code < 'A' || code > 'Z') {
            break;
        }
        sawLetter = true;
        value = value * 26U + static_cast<std::size_t>(code - 'A' + 1U);
    }
    if (!sawLetter || value == 0) {
        return std::nullopt;
    }
    return value - 1U;
}

std::string readWorksheetCell(
    QXmlStreamReader &xml,
    const std::vector<std::string> &sharedStrings) {
    const QString cellType = xml.attributes().value(QStringLiteral("t")).toString();
    QString rawValue;
    QString inlineText;

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == QStringLiteral("v")) {
            rawValue = xml.readElementText();
        } else if (xml.isStartElement() && xml.name() == QStringLiteral("is")) {
            inlineText = readTextRuns(xml, QStringLiteral("is"));
        } else if (xml.isEndElement() && xml.name() == QStringLiteral("c")) {
            break;
        }
    }

    if (cellType == QStringLiteral("s")) {
        bool ok = false;
        const long long sharedIndex = rawValue.toLongLong(&ok);
        if (ok && sharedIndex >= 0
            && static_cast<std::size_t>(sharedIndex) < sharedStrings.size()) {
            return sharedStrings[static_cast<std::size_t>(sharedIndex)];
        }
        return {};
    }
    if (cellType == QStringLiteral("inlineStr")) {
        return inlineText.toStdString();
    }
    return rawValue.toStdString();
}

std::vector<std::string> readWorksheetRow(
    QXmlStreamReader &xml,
    const std::vector<std::string> &sharedStrings) {
    std::vector<std::string> row;
    std::size_t nextColumn = 0;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == QStringLiteral("c")) {
            const QString cellRef = xml.attributes().value(QStringLiteral("r")).toString();
            const std::size_t column = columnIndexFromCellRef(cellRef).value_or(nextColumn);
            if (row.size() <= column) {
                row.resize(column + 1U);
            }
            row[column] = readWorksheetCell(xml, sharedStrings);
            nextColumn = column + 1U;
        } else if (xml.isEndElement() && xml.name() == QStringLiteral("row")) {
            break;
        }
    }
    return row;
}

std::optional<std::vector<std::vector<std::string>>> readWorksheetRows(
    const std::vector<std::uint8_t> &bytes,
    const std::vector<std::string> &sharedStrings,
    std::string &error) {
    std::vector<std::vector<std::string>> rows;
    QXmlStreamReader xml(byteArrayFromBytes(bytes));
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == QStringLiteral("row")) {
            rows.push_back(readWorksheetRow(xml, sharedStrings));
        }
    }
    if (xml.hasError()) {
        error = "XLSX worksheet XML parse error: " + xml.errorString().toStdString();
        return std::nullopt;
    }
    return rows;
}

bool rowIsEmpty(const std::vector<std::string> &row) {
    return std::all_of(row.begin(), row.end(), [](const std::string &cell) {
        std::string trimmed = cell;
        trimInPlace(trimmed);
        return trimmed.empty();
    });
}

bool hasRequiredCells(const std::vector<std::string> &cells, const TelemetryColumns &columns) {
    return static_cast<int>(cells.size()) > columns.iTime
        && static_cast<int>(cells.size()) > columns.iAlt;
}

std::optional<std::string> finishTelemetrySession(
    const std::string &sourceName,
    const TelemetryColumns &columns,
    std::vector<FlightSample> samples,
    FlightSession &out) {
    if (samples.empty()) {
        return std::string("No data rows in telemetry source: ") + sourceName;
    }

    out.samples = std::move(samples);
    applyMetricsInSource(out, columns);
    return std::nullopt;
}

std::optional<std::string> loadTelemetryRows(
    const std::string &sourceName,
    const std::vector<std::string> &header,
    const std::vector<std::vector<std::string>> &dataRows,
    FlightSession &out) {
    std::string error;
    const auto columns = resolveTelemetryColumns(header, sourceName, error);
    if (!columns) {
        return error;
    }

    std::vector<FlightSample> samples;
    samples.reserve(dataRows.size());
    for (const auto &cells : dataRows) {
        if (rowIsEmpty(cells) || !hasRequiredCells(cells, *columns)) {
            continue;
        }
        samples.push_back(sampleFromCells(cells, *columns));
    }

    return finishTelemetrySession(sourceName, *columns, std::move(samples), out);
}

std::optional<QString> firstWorksheetRelationshipId(
    const std::vector<std::uint8_t> &workbookBytes,
    std::string &error) {
    QXmlStreamReader xml(byteArrayFromBytes(workbookBytes));
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == QStringLiteral("sheet")) {
            const auto attrs = xml.attributes();
            QString id = attrs.value(
                QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships"),
                QStringLiteral("id")).toString();
            if (id.isEmpty()) {
                id = attrs.value(QStringLiteral("r:id")).toString();
            }
            if (id.isEmpty()) {
                id = attrs.value(QStringLiteral("id")).toString();
            }
            if (!id.isEmpty()) {
                return id;
            }
        }
    }
    if (xml.hasError()) {
        error = "XLSX workbook XML parse error: " + xml.errorString().toStdString();
    }
    return std::nullopt;
}

std::optional<QString> worksheetRelationshipTarget(
    const std::vector<std::uint8_t> &relsBytes,
    const QString &relationshipId,
    std::string &error) {
    QXmlStreamReader xml(byteArrayFromBytes(relsBytes));
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == QStringLiteral("Relationship")) {
            const auto attrs = xml.attributes();
            if (attrs.value(QStringLiteral("Id")).toString() == relationshipId) {
                return attrs.value(QStringLiteral("Target")).toString();
            }
        }
    }
    if (xml.hasError()) {
        error = "XLSX workbook relationships XML parse error: " + xml.errorString().toStdString();
    }
    return std::nullopt;
}

std::string worksheetTargetToZipEntryName(QString target) {
    target.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (target.startsWith(QStringLiteral("./"))) {
        target.remove(0, 2);
    }
    if (target.startsWith(QLatin1Char('/'))) {
        target.remove(0, 1);
    } else if (!target.startsWith(QStringLiteral("xl/"))) {
        target.prepend(QStringLiteral("xl/"));
    }
    return target.toStdString();
}

std::optional<std::string> worksheetEntryNameFromWorkbook(
    const std::vector<std::uint8_t> &bytes,
    const std::map<std::string, ZipEntry> &entries,
    std::string &error) {
    if (entries.find("xl/workbook.xml") == entries.end()
        || entries.find("xl/_rels/workbook.xml.rels") == entries.end()) {
        return std::nullopt;
    }

    const auto workbookBytes = unzipEntry(bytes, entries, "xl/workbook.xml", error);
    if (!workbookBytes) {
        return std::nullopt;
    }
    const auto relationshipId = firstWorksheetRelationshipId(*workbookBytes, error);
    if (!relationshipId) {
        return std::nullopt;
    }

    const auto relsBytes = unzipEntry(bytes, entries, "xl/_rels/workbook.xml.rels", error);
    if (!relsBytes) {
        return std::nullopt;
    }
    const auto target = worksheetRelationshipTarget(*relsBytes, *relationshipId, error);
    if (!target) {
        return std::nullopt;
    }

    const std::string entryName = worksheetTargetToZipEntryName(*target);
    if (entries.find(entryName) == entries.end()) {
        error = "XLSX worksheet relationship target missing: " + entryName;
        return std::nullopt;
    }
    return entryName;
}

std::optional<std::string> firstWorksheetEntryNameByPath(const std::map<std::string, ZipEntry> &entries) {
    constexpr std::string_view kWorksheetPrefix = "xl/worksheets/sheet";
    constexpr std::string_view kWorksheetSuffix = ".xml";
    for (const auto &[name, entry] : entries) {
        (void)entry;
        if (name.rfind(kWorksheetPrefix, 0) == 0
            && name.size() >= kWorksheetSuffix.size()
            && name.compare(name.size() - kWorksheetSuffix.size(), kWorksheetSuffix.size(), kWorksheetSuffix) == 0) {
            return name;
        }
    }
    return std::nullopt;
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

    std::string error;
    const auto columns = resolveTelemetryColumns(header, path, error);
    if (!columns) {
        return error;
    }

    std::vector<FlightSample> samples;
    while (std::getline(in, line)) {
        trimInPlace(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const auto cells = splitCsvLine(line);
        if (!hasRequiredCells(cells, *columns)) {
            continue;
        }

        samples.push_back(sampleFromCells(cells, *columns));
    }

    return finishTelemetrySession(path, *columns, std::move(samples), out);
}

std::optional<std::string> SampleFileLoader::loadXlsx(const std::string &path, FlightSession &out) {
    const auto fileBytes = readFileBytes(path);
    if (!fileBytes) {
        return std::string("Could not open file: ") + path;
    }

    std::string error;
    const auto entries = readZipCentralDirectory(*fileBytes, error);
    if (!entries) {
        return std::string("Could not read XLSX file: ") + error + ": " + path;
    }

    std::vector<std::string> sharedStrings;
    if (entries->find("xl/sharedStrings.xml") != entries->end()) {
        const auto sharedBytes = unzipEntry(*fileBytes, *entries, "xl/sharedStrings.xml", error);
        if (!sharedBytes) {
            return std::string("Could not read XLSX shared strings: ") + error + ": " + path;
        }
        const auto parsedSharedStrings = readSharedStrings(*sharedBytes, error);
        if (!parsedSharedStrings) {
            return std::string("Could not parse XLSX shared strings: ") + error + ": " + path;
        }
        sharedStrings = *parsedSharedStrings;
    }

    std::string workbookError;
    auto worksheetName = worksheetEntryNameFromWorkbook(*fileBytes, *entries, workbookError);
    if (!worksheetName) {
        worksheetName = firstWorksheetEntryNameByPath(*entries);
    }
    if (!worksheetName) {
        const std::string detail = workbookError.empty() ? "XLSX file has no worksheet XML" : workbookError;
        return detail + ": " + path;
    }
    const auto worksheetBytes = unzipEntry(*fileBytes, *entries, *worksheetName, error);
    if (!worksheetBytes) {
        return std::string("Could not read XLSX worksheet: ") + error + ": " + path;
    }

    const auto rows = readWorksheetRows(*worksheetBytes, sharedStrings, error);
    if (!rows) {
        return std::string("Could not parse XLSX worksheet: ") + error + ": " + path;
    }

    auto headerIt = std::find_if(rows->begin(), rows->end(), [](const auto &row) {
        return !rowIsEmpty(row);
    });
    if (headerIt == rows->end()) {
        return std::string("XLSX worksheet has no header row: ") + path;
    }

    std::vector<std::vector<std::string>> dataRows;
    dataRows.reserve(static_cast<std::size_t>(std::distance(headerIt, rows->end())));
    for (auto it = std::next(headerIt); it != rows->end(); ++it) {
        dataRows.push_back(*it);
    }

    return loadTelemetryRows(path, *headerIt, dataRows, out);
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
