#include <QTemporaryDir>

#include <cstddef>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "domain/FlightSession.h"
#include "services/import/SampleFileLoader.h"
#include "services/preview/FlightPreviewCache.h"
#include "services/telemetry/Framer.h"
#include "services/telemetry/Parser.h"

TEST_CASE("SampleFileLoader cancellable overloads preserve output on immediate cancellation",
          "[import][cancel]")
{
    FlightSession session;
    session.samples.resize(1);
    session.samples.front().timestamp = 4242;
    const cosmo::CancellationCheck canceled = [] { return true; };

    const auto xlsxResult = SampleFileLoader::loadXlsx(
        "unused.xlsx", session, canceled);
    REQUIRE(xlsxResult.canceled);
    REQUIRE_FALSE(xlsxResult.error);
    REQUIRE(session.samples.size() == 1);
    REQUIRE(session.samples.front().timestamp == 4242);

    Framer framer;
    Parser parser;
    const auto telemResult = SampleFileLoader::loadTelemFile(
        "unused.telem", session, framer, parser, canceled);
    REQUIRE(telemResult.canceled);
    REQUIRE_FALSE(telemResult.error);
    REQUIRE(session.samples.size() == 1);
    REQUIRE(session.samples.front().timestamp == 4242);
}

TEST_CASE("SampleFileLoader cancels promptly during a large CSV parse", "[import][cancel]")
{
    constexpr int ROW_COUNT = 500000;
    constexpr std::size_t CANCEL_AFTER_POLLS = 6U;

    QTemporaryDir temporary_directory;
    REQUIRE(temporary_directory.isValid());
    const std::string path = (temporary_directory.path() + QStringLiteral("/large.csv")).toStdString();

    {
        std::ofstream output(path);
        REQUIRE(output.good());
        output << "time_ms,altitude_m\n";
        for (int row = 0; row < ROW_COUNT; ++row) {
            output << row << ',' << row % 1000 << '\n';
        }
        output.close();
        REQUIRE(output.good());
    }

    FlightSession session;
    session.samples.resize(1);
    session.samples.front().timestamp = 4242;
    std::size_t cancellation_polls = 0;
    const auto result = SampleFileLoader::loadTheseusCsv(
        path,
        session,
        [&cancellation_polls] {
            ++cancellation_polls;
            return cancellation_polls >= CANCEL_AFTER_POLLS;
        });

    REQUIRE(result.canceled);
    REQUIRE_FALSE(result.error.has_value());
    REQUIRE_FALSE(result.succeeded());
    REQUIRE(cancellation_polls == CANCEL_AFTER_POLLS);
    REQUIRE(session.samples.size() == 1);
    REQUIRE(session.samples.front().timestamp == 4242);
}

TEST_CASE("FlightPreviewCache cancels before traversing a large session", "[preview][cancel]")
{
    constexpr int SAMPLE_COUNT = 250000;
    constexpr std::size_t CANCEL_AFTER_POLLS = 6U;

    FlightSession session;
    session.samples.resize(SAMPLE_COUNT);
    for (int index = 0; index < SAMPLE_COUNT; ++index) {
        FlightSample &sample = session.samples[static_cast<std::size_t>(index)];
        sample.timestamp = index * 10L;
        sample.altitude = static_cast<double>(index % 1000);
        sample.coordinates.latitude = 54.0;
        sample.coordinates.longitude = -6.0;
    }

    std::size_t cancellation_polls = 0;
    const auto preview = cosmo::preview::FlightPreviewCache::build(
        session,
        [&cancellation_polls] {
            ++cancellation_polls;
            return cancellation_polls >= CANCEL_AFTER_POLLS;
        });

    REQUIRE_FALSE(preview);
    REQUIRE(cancellation_polls == CANCEL_AFTER_POLLS);
    REQUIRE(session.samples.front().timestamp == 0);
    REQUIRE(session.samples.back().timestamp == (SAMPLE_COUNT - 1) * 10L);
}
