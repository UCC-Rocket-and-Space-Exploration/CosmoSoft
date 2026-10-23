#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "services/import/SampleFileLoader.h"
#include "services/persistence/FlightLogManager.h"
#include "services/preview/FlightPreviewCache.h"
#include "services/telemetry/GpsCoordinateCorrection.h"
#include "services/telemetry/LineTelemetryDecoder.h"

#include <QTemporaryDir>

#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>
#include <zlib.h>

namespace {

[[nodiscard]] std::string sampleDataPath(const std::string &fileName) {
    return std::string(COSMO_SOURCE_DIR) + "/sample_data/" + fileName;
}

void writeWorksheetZip(const std::string &path, const std::string &xml) {
  const std::string name = "xl/worksheets/sheet1.xml";
  std::vector<unsigned char> bytes;
  const auto checksum = static_cast<std::uint32_t>(
      crc32(0, reinterpret_cast<const Bytef *>(xml.data()),
            static_cast<uInt>(xml.size())));
  const auto integer = [&](std::uint32_t value, int count) {
    for (int i = 0; i < count; ++i) {
      bytes.push_back(static_cast<unsigned char>(value >> (8 * i)));
    }
  };
  integer(0x04034b50, 4);
  integer(20, 2);
  integer(0, 2); // flags
  integer(0, 2); // stored
  integer(0, 4); // timestamp
  integer(checksum, 4);
  integer(static_cast<std::uint32_t>(xml.size()), 4);
  integer(static_cast<std::uint32_t>(xml.size()), 4);
  integer(static_cast<std::uint32_t>(name.size()), 2);
  integer(0, 2);
  bytes.insert(bytes.end(), name.begin(), name.end());
  bytes.insert(bytes.end(), xml.begin(), xml.end());
  const auto directoryOffset = static_cast<std::uint32_t>(bytes.size());
  integer(0x02014b50, 4);
  integer(20, 2);
  integer(20, 2);
  integer(0, 2);
  integer(0, 2);
  integer(0, 4);
  integer(checksum, 4);
  integer(static_cast<std::uint32_t>(xml.size()), 4);
  integer(static_cast<std::uint32_t>(xml.size()), 4);
  integer(static_cast<std::uint32_t>(name.size()), 2);
  integer(0, 2);
  integer(0, 2);
  integer(0, 2);
  integer(0, 2);
  integer(0, 4);
  integer(0, 4);
  bytes.insert(bytes.end(), name.begin(), name.end());
  const auto directorySize =
      static_cast<std::uint32_t>(bytes.size()) - directoryOffset;
  integer(0x06054b50, 4);
  integer(0, 2);
  integer(0, 2);
  integer(1, 2);
  integer(1, 2);
  integer(directorySize, 4);
  integer(directoryOffset, 4);
  integer(0, 2);
  std::ofstream file(path, std::ios::binary);
  file.write(reinterpret_cast<const char *>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  REQUIRE(file.good());
}

} // namespace

TEST_CASE(
    "Blank scalar acceleration remains missing while measured zero is retained",
    "[import][regression]") {
  QTemporaryDir directory;
  REQUIRE(directory.isValid());
  const auto path = directory.filePath("scalar.csv").toStdString();
  {
    std::ofstream file(path);
    file << "timestamp_ms,altitude,acceleration\n0,10,\n1,11,0\n2,12,9.81\n";
  }
  FlightSession session;
  REQUIRE_FALSE(SampleFileLoader::loadTheseusCsv(path, session));
  REQUIRE(session.samples.size() == 3);
  CHECK(std::isnan(session.samples[0].acceleration.x));
  CHECK(std::isnan(session.samples[0].acceleration.y));
  CHECK(std::isnan(session.samples[0].acceleration.z));
  CHECK(session.samples[1].acceleration.z == 0);
  CHECK(session.samples[2].acceleration.z == Catch::Approx(9.81));
}

TEST_CASE(
    "XLSX rejects rows beyond the column bound without replacing a session",
    "[import][regression]") {
  QTemporaryDir directory;
  REQUIRE(directory.isValid());
  const auto path = directory.filePath("columns.xlsx").toStdString();
  std::string xml =
      "<worksheet><sheetData><row><c "
      "t=\"inlineStr\"><is><t>time_ms</t></is></c>"
      "<c t=\"inlineStr\"><is><t>altitude</t></is></c></row><row>";
  for (int i = 0; i < 16385; ++i) {
    xml += "<c><v>1</v></c>";
  }
  xml += "</row></sheetData></worksheet>";
  writeWorksheetZip(path, xml);
  FlightSession session;
  FlightSample sample;
  sample.altitude = 42;
  session.samples.push_back(sample);
  REQUIRE(SampleFileLoader::loadXlsx(path, session));
  REQUIRE(session.samples.size() == 1);
  CHECK(session.samples[0].altitude == 42);
}

TEST_CASE("XLSX bounds cumulative worksheet rows", "[import][regression]") {
  QTemporaryDir directory;
  REQUIRE(directory.isValid());
  const auto path = directory.filePath("rows.xlsx").toStdString();
  std::string xml = "<worksheet><sheetData><row><c "
                    "t=\"inlineStr\"><is><t>time_ms</t></is></c>"
                    "<c t=\"inlineStr\"><is><t>altitude</t></is></c></row>"
                    "<row><c><v>0</v></c><c><v>10</v></c></row>";
  for (int i = 0; i < 1048575; ++i) {
    xml += "<row/>";
  }
  xml += "</sheetData></worksheet>";
  writeWorksheetZip(path, xml);
  FlightSession session;
  REQUIRE(SampleFileLoader::loadXlsx(path, session));
  CHECK(session.samples.empty());
}

TEST_CASE("XLSX retains sparse telemetry columns and ignores unrelated cells",
          "[import][regression]") {
  QTemporaryDir directory;
  REQUIRE(directory.isValid());
  const auto path = directory.filePath("sparse.xlsx").toStdString();
  writeWorksheetZip(
      path, "<worksheet><sheetData><row>"
            "<c r=\"A1\" t=\"inlineStr\"><is><t>time_ms</t></is></c>"
            "<c r=\"XFD1\" t=\"inlineStr\"><is><t>altitude</t></is></c></row>"
            "<row><c r=\"A2\"><v>25</v></c><c r=\"C2\"><v>unrelated</v></c>"
            "<c r=\"XFD2\"><v>123.5</v></c></row></sheetData></worksheet>");
  FlightSession session;
  REQUIRE_FALSE(SampleFileLoader::loadXlsx(path, session));
  REQUIRE(session.samples.size() == 1);
  CHECK(session.samples[0].timestamp == 25);
  CHECK(session.samples[0].altitude == Catch::Approx(123.5));
}

TEST_CASE("Received RSSI survives export and missing RSSI stays unavailable",
          "[import][export][rssi]") {
  QTemporaryDir directory;
  REQUIRE(directory.isValid());
  const std::string row =
      "251618,21,100227,221,0,0,9.81,0,0,0,0,0,0,51.89,-8.49,84";
  const auto measured = cosmo::telemetry::decodeTelemetryCsvRow(row + ",-57");
  const auto missing = cosmo::telemetry::decodeTelemetryCsvRow(row);
  REQUIRE(measured);
  REQUIRE(missing);
  FlightLogManager logs;
  logs.setOutputPath(directory.filePath("rssi.csv").toStdString());
  REQUIRE(logs.appendSample(*measured));
  REQUIRE(logs.appendSample(*missing));
  REQUIRE(logs.exportSessionToCsv());
  FlightSession reloaded;
  REQUIRE_FALSE(SampleFileLoader::loadTheseusCsv(logs.outputPath(), reloaded)
                    .has_value());
  REQUIRE(reloaded.samples.size() == 2);
  REQUIRE(reloaded.samples[0].rssi == -57);
  REQUIRE(std::isnan(reloaded.samples[1].rssi));
}

TEST_CASE("Corrected live GPS remains in the western hemisphere after export",
          "[import][export][gps-correction]") {
  QTemporaryDir directory;
  REQUIRE(directory.isValid());
  FlightSample sample;
  sample.timestamp = 251618;
  sample.coordinates = {51.892754, 8.491923};
  cosmo::telemetry::applyWestLongitudeCorrection(sample, true);
  FlightLogManager logs;
  logs.setOutputPath(directory.filePath("west.csv").toStdString());
  REQUIRE(logs.appendSample(sample));
  REQUIRE(logs.exportSessionToCsv());
  FlightSession reloaded;
  REQUIRE_FALSE(SampleFileLoader::loadTheseusCsv(logs.outputPath(), reloaded)
                    .has_value());
  REQUIRE(reloaded.samples.size() == 1);
  REQUIRE(reloaded.samples.front().coordinates.latitude ==
          Catch::Approx(51.892754));
  REQUIRE(reloaded.samples.front().coordinates.longitude ==
          Catch::Approx(-8.491923));
}

TEST_CASE("Live timestamps retain milliseconds through saving and replay",
          "[import][export][timestamp-ms]") {
  QTemporaryDir directory;
  REQUIRE(directory.isValid());
  FlightSession session;
  for (const long timestamp : {251618L, 251619L, 252618L}) {
    const auto sample = cosmo::telemetry::decodeTelemetryCsvRow(
        std::to_string(timestamp) +
        ",21,100227,221,0,0,9.81,0,0,0,0,0,0,51.89,-8.49,84");
    REQUIRE(sample.has_value());
    session.samples.push_back(*sample);
  }
  FlightLogManager logs;
  logs.setOutputPath(directory.filePath("milliseconds.csv").toStdString());
  REQUIRE(logs.exportSessionToCsv(session));
  std::ifstream file(logs.outputPath());
  std::string header;
  REQUIRE(std::getline(file, header));
  REQUIRE(header.substr(0, header.find(',')) == "timestamp_ms");
  std::string row;
  REQUIRE(std::getline(file, row));
  REQUIRE(row.substr(0, row.find(',')) == "251618");

  FlightSession reloaded;
  REQUIRE_FALSE(SampleFileLoader::loadTheseusCsv(logs.outputPath(), reloaded)
                    .has_value());
  REQUIRE(reloaded.samples.size() == 3);
  REQUIRE(reloaded.samples[0].timestamp == 251618);
  REQUIRE(reloaded.samples[1].timestamp == 251619);
  REQUIRE(reloaded.samples[2].timestamp == 252618);
  const auto preview = cosmo::preview::FlightPreviewCache::build(reloaded);
  REQUIRE(preview->displaySeconds()[1] == Catch::Approx(0.001));
  REQUIRE(preview->durationSeconds() == Catch::Approx(1.0));
}

TEST_CASE("Replay identifies old and new timestamp units from CSV headers",
          "[import][timestamp-ms]") {
  QTemporaryDir directory;
  REQUIRE(directory.isValid());
  const auto path = directory.filePath("units.csv").toStdString();
  for (const std::string timeHeader : {"time", "time_ms", "timestamp_ms"}) {
    {
      std::ofstream file(path);
      file << timeHeader << ",altitude\n";
      file << (timeHeader == "time" ? "251.618,100\n251.619,101\n"
                                    : "251618,100\n251619,101\n");
    }
    FlightSession session;
    REQUIRE_FALSE(SampleFileLoader::loadTheseusCsv(path, session).has_value());
    REQUIRE(session.samples.size() == 2);
    REQUIRE(session.samples[0].timestamp == 251618);
    REQUIRE(session.samples[1].timestamp == 251619);
  }
}

TEST_CASE("Explicit degree attitude survives CSV import and export",
          "[import][attitude]") {
  QTemporaryDir directory;
  REQUIRE(directory.isValid());
  const auto path = directory.filePath("attitude.csv").toStdString();
  {
    std::ofstream file(path);
    file << "time_ms,altitude,attitude_x_deg,attitude_y_deg,attitude_z_deg\n"
            "0,100,90,0,-30\n100,101,,,\n200,102,0,359,0\n";
  }
  FlightSession session;
  REQUIRE_FALSE(SampleFileLoader::loadTheseusCsv(path, session).has_value());
  REQUIRE(session.samples.size() == 3);
  REQUIRE(session.samples[0].angularRotation.x == Catch::Approx(90));
  REQUIRE(session.samples[0].angularRotation.z == Catch::Approx(-30));
  REQUIRE(std::isnan(session.samples[1].angularRotation.x));
  REQUIRE(session.samples[2].angularRotation.y == Catch::Approx(359));
  FlightLogManager logs;
  logs.setOutputPath(directory.filePath("export.csv").toStdString());
  REQUIRE(logs.exportSessionToCsv(session));
  FlightSession roundTrip;
  REQUIRE_FALSE(SampleFileLoader::loadTheseusCsv(logs.outputPath(), roundTrip)
                    .has_value());
  REQUIRE(roundTrip.samples[0].angularRotation.x == Catch::Approx(90));
  REQUIRE(roundTrip.samples[0].angularRotation.z == Catch::Approx(-30));
  REQUIRE(std::isnan(roundTrip.samples[1].angularRotation.y));
}

TEST_CASE("SampleFileLoader loads telemetry XLSX workbook", "[import]") {
    FlightSession session;

    const auto error = SampleFileLoader::loadXlsx(
        sampleDataPath("flight_2026-05-30_17-34-27.xlsx"),
        session);

    INFO(error.value_or("no error"));
    REQUIRE_FALSE(error.has_value());
    REQUIRE(session.samples.size() == 7167);

    const FlightSample &first = session.samples.front();
    REQUIRE(first.timestamp == 0);
    REQUIRE(first.temperature == Catch::Approx(18.08));
    REQUIRE(first.pressure == Catch::Approx(101622.5));
    REQUIRE(first.altitude == Catch::Approx(105.26));
    REQUIRE(first.acceleration.x == Catch::Approx(0.565));
    REQUIRE(first.acceleration.y == Catch::Approx(-0.177));
    REQUIRE(first.acceleration.z == Catch::Approx(-9.804));
    REQUIRE(first.angularVelocity.x == Catch::Approx(0.027));
    REQUIRE(first.angularVelocity.y == Catch::Approx(-0.073));
    REQUIRE(first.angularVelocity.z == Catch::Approx(0.015));
    REQUIRE(first.coordinates.latitude == Catch::Approx(54.303349));
    REQUIRE(first.coordinates.longitude == Catch::Approx(-5.582283));
    REQUIRE(first.angularRotation.x == Catch::Approx(-9.785));
    REQUIRE(first.angularRotation.y == Catch::Approx(-3.195));
    REQUIRE(first.angularRotation.z == Catch::Approx(-19.417));

    const std::array<bool, FlightSession::kTraceMetricCount> expectedMetrics{
        true, true, true, true, false, false, true, true, true};
    REQUIRE(session.metricsInSource == expectedMetrics);
}

TEST_CASE("SampleFileLoader loads corrected telemetry CSV coordinates", "[import]") {
    FlightSession session;

    const auto error = SampleFileLoader::loadTheseusCsv(
        sampleDataPath("flight_2026-05-30_17-34-27.csv"),
        session);

    INFO(error.value_or("no error"));
    REQUIRE_FALSE(error.has_value());
    REQUIRE(session.samples.size() == 7167);

    const FlightSample &first = session.samples.front();
    REQUIRE(first.coordinates.latitude == Catch::Approx(54.303349));
    REQUIRE(first.coordinates.longitude == Catch::Approx(-5.582283));
    REQUIRE(first.angularRotation.x == Catch::Approx(-9.785));
    REQUIRE(first.angularRotation.y == Catch::Approx(-3.195));
    REQUIRE(first.angularRotation.z == Catch::Approx(-19.417));
}

TEST_CASE("SampleFileLoader skips malformed and unsafe numeric CSV rows", "[import]") {
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    const std::string path = temporary.filePath(QStringLiteral("unsafe.csv")).toStdString();
    {
        std::ofstream output(path);
        REQUIRE(output.good());
        output << "time_ms,altitude_m,temperature_c,latitude,longitude\n"
               << "0,10,20,51.9,-8.5\n"
               << "1junk,11,21,51.9,-8.5\n"
               << "2,1e300,22,51.9,-8.5\n"
               << "3,13,nan,51.9,-8.5\n"
               << "4,14,24,91,-8.5\n";
    }

    FlightSession session;
    const auto error = SampleFileLoader::loadTheseusCsv(path, session);

    INFO(error.value_or("no error"));
    REQUIRE_FALSE(error);
    REQUIRE(session.samples.size() == 1);
    REQUIRE(session.samples.front().timestamp == 0);
    REQUIRE(session.samples.front().altitude == Catch::Approx(10.0));
}
