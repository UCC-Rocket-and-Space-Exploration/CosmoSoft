#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "services/import/SampleFileLoader.h"

#include <QTemporaryDir>

#include <array>
#include <fstream>
#include <string>

namespace {

[[nodiscard]] std::string sampleDataPath(const std::string &fileName) {
    return std::string(COSMO_SOURCE_DIR) + "/sample_data/" + fileName;
}

} // namespace

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
