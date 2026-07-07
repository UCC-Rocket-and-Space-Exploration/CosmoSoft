#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "services/video/FlightAnalyzer.h"
#include "services/video/VideoExportTypes.h"
#include "services/video/VideoTimeline.h"

#include <array>

namespace {

FlightSample sample(long timestamp_ms, double altitude_m, double latitude = 0.0,
                    double longitude = 0.0) {
  FlightSample value;
  value.timestamp = timestamp_ms;
  value.altitude = altitude_m;
  value.coordinates.latitude = latitude;
  value.coordinates.longitude = longitude;
  return value;
}

} // namespace

TEST_CASE("FlightAnalyzer uses zero time and calculates height",
          "[video][analysis]") {
  FlightSession session;
  session.samples = {
      sample(-1000, 100.0), sample(-500, 100.0), sample(0, 101.0),
      sample(500, 106.0),   sample(1000, 115.0), sample(1500, 121.0),
      sample(2000, 116.0),  sample(2500, 108.0), sample(3000, 102.0),
  };

  const auto analysis = cosmo::video::FlightAnalyzer::analyze(session);

  REQUIRE(analysis.start_index == 2);
  REQUIRE(analysis.apogee_index == 5);
  REQUIRE(analysis.max_recorded_altitude_m == 121.0);
  REQUIRE(analysis.peak_height_m == 21.0);
}

TEST_CASE("FlightAnalyzer detects sustained ascent and post-apogee landing",
          "[video][analysis]") {
  FlightSession session;
  const std::array<double, 18> altitudes{0.0,  0.0, 0.0, 2.0, 4.0, 6.0,
                                         10.0, 8.0, 4.0, 0.0, 0.0, 0.0,
                                         0.0,  0.0, 0.0, 0.0, 0.0, 0.0};
  for (int i = 0; i < static_cast<int>(altitudes.size()); ++i) {
    session.samples.push_back(
        sample(1000 + i * 500, altitudes[static_cast<std::size_t>(i)]));
  }

  const auto analysis = cosmo::video::FlightAnalyzer::analyze(session);

  REQUIRE(analysis.start_index == 4);
  REQUIRE(analysis.apogee_index == 6);
  REQUIRE(analysis.end_index == 17);
  REQUIRE(analysis.automatic_bounds);
  REQUIRE(analysis.baseline_altitude_m == Catch::Approx(0.0));
  REQUIRE(analysis.peak_height_m == Catch::Approx(10.0));
}

TEST_CASE("FlightAnalyzer honors an explicit trim range", "[video][analysis]") {
  FlightSession session;
  for (int i = 0; i < 20; ++i) {
    session.samples.push_back(sample(i * 100, static_cast<double>(i)));
  }

  const auto analysis = cosmo::video::FlightAnalyzer::analyze(
      session, cosmo::video::FlightTrimRange{5, 15});

  REQUIRE(analysis.start_index == 5);
  REQUIRE(analysis.end_index == 15);
  REQUIRE_FALSE(analysis.automatic_bounds);
  REQUIRE(analysis.duration_seconds == Catch::Approx(0.9));
  REQUIRE(analysis.baseline_altitude_m == Catch::Approx(2.0));
}

TEST_CASE("FlightAnalyzer calculates three dimensional speed from GPS",
          "[video][analysis]") {
  FlightSession session;
  constexpr double latitude = 51.0;
  for (int i = 0; i < 12; ++i) {
    session.samples.push_back(
        sample(i * 1000, 100.0 + i, latitude,
               -1.0 + static_cast<double>(i) * 0.00001425));
  }

  const auto analysis = cosmo::video::FlightAnalyzer::analyze(
      session, cosmo::video::FlightTrimRange{
                   0, static_cast<int>(session.samples.size())});

  REQUIRE(analysis.speed_source ==
          cosmo::video::SpeedSource::three_dimensional);
  REQUIRE(analysis.max_speed_mps.has_value());
  REQUIRE(*analysis.max_speed_mps > 1.0);
  REQUIRE(*analysis.max_speed_mps < 2.0);
  REQUIRE(analysis.gps_quality.valid_sample_count == 12);
  REQUIRE(analysis.gps_quality.coverage_ratio == Catch::Approx(1.0));
  REQUIRE(analysis.gps_quality.sufficient_for_speed);
}

TEST_CASE(
    "FlightAnalyzer robust speed rejects an isolated GPS position outlier",
    "[video][analysis]") {
  FlightSession session;
  constexpr double latitude = 51.0;
  for (int i = 0; i < 15; ++i) {
    double longitude = -1.0 + static_cast<double>(i) * 0.00001425;
    if (i == 7)
      longitude += 1.0;
    session.samples.push_back(sample(i * 1000, 100.0 + i, latitude, longitude));
  }

  const auto analysis = cosmo::video::FlightAnalyzer::analyze(
      session, cosmo::video::FlightTrimRange{
                   0, static_cast<int>(session.samples.size())});

  REQUIRE(analysis.max_speed_mps.has_value());
  REQUIRE(*analysis.max_speed_mps < 2.0);
}

TEST_CASE("FlightAnalyzer unwraps GPS motion across the dateline",
          "[video][analysis]") {
  FlightSession session;
  for (int i = 0; i < 12; ++i) {
    double longitude = 179.9995 + static_cast<double>(i) * 0.0001;
    if (longitude > 180.0)
      longitude -= 360.0;
    session.samples.push_back(sample(i * 1000, 50.0, 0.1, longitude));
  }

  const auto analysis = cosmo::video::FlightAnalyzer::analyze(
      session, cosmo::video::FlightTrimRange{
                   0, static_cast<int>(session.samples.size())});

  REQUIRE(analysis.max_speed_mps.has_value());
  REQUIRE(*analysis.max_speed_mps > 10.0);
  REQUIRE(*analysis.max_speed_mps < 12.0);
}

TEST_CASE("FlightAnalyzer falls back to vertical speed without GPS",
          "[video][analysis]") {
  FlightSession session;
  for (int i = 0; i < 12; ++i) {
    session.samples.push_back(sample(i * 1000, static_cast<double>(i) * 4.0));
  }

  const auto analysis = cosmo::video::FlightAnalyzer::analyze(
      session, cosmo::video::FlightTrimRange{
                   0, static_cast<int>(session.samples.size())});

  REQUIRE(analysis.speed_source == cosmo::video::SpeedSource::vertical);
  REQUIRE(analysis.max_speed_mps.has_value());
  REQUIRE(*analysis.max_speed_mps == Catch::Approx(4.0));
  REQUIRE_FALSE(analysis.gps_quality.sufficient_for_speed);
}

TEST_CASE("FlightAnalyzer rejects invalid coordinates and uses vertical speed",
          "[video][analysis]") {
  FlightSession session;
  for (int i = 0; i < 12; ++i) {
    session.samples.push_back(sample(i * 1000, i * 3.0, 95.0, 240.0));
  }

  const auto analysis = cosmo::video::FlightAnalyzer::analyze(
      session, cosmo::video::FlightTrimRange{
                   0, static_cast<int>(session.samples.size())});

  REQUIRE(analysis.speed_source == cosmo::video::SpeedSource::vertical);
  REQUIRE(analysis.gps_quality.valid_sample_count == 0);
  REQUIRE(analysis.max_speed_mps == Catch::Approx(3.0));
}

TEST_CASE("FlightAnalyzer omits speed across repaired timestamps",
          "[video][analysis]") {
  FlightSession session;
  session.samples = {
      sample(0, 0.0),      sample(100, 1.0),    sample(100, 1000.0),
      sample(100, 2000.0), sample(200, 2001.0),
  };

  const auto analysis = cosmo::video::FlightAnalyzer::analyze(
      session, cosmo::video::FlightTrimRange{
                   0, static_cast<int>(session.samples.size())});

  REQUIRE_FALSE(analysis.max_speed_mps.has_value());
  REQUIRE_FALSE(analysis.warnings.empty());
}

TEST_CASE("FlightAnalyzer handles flat and single-sample sessions",
          "[video][analysis]") {
  FlightSession flat;
  for (int i = 0; i < 8; ++i)
    flat.samples.push_back(sample(i * 500, 42.0));
  const auto flat_analysis = cosmo::video::FlightAnalyzer::analyze(flat);
  REQUIRE(flat_analysis.peak_height_m == Catch::Approx(0.0));
  REQUIRE_FALSE(flat_analysis.max_speed_mps.has_value());

  FlightSession single;
  single.samples.push_back(sample(0, 123.0));
  const auto single_analysis = cosmo::video::FlightAnalyzer::analyze(single);
  REQUIRE(single_analysis.start_index == 0);
  REQUIRE(single_analysis.end_index == 1);
  REQUIRE(single_analysis.duration_seconds == Catch::Approx(0.0));
  REQUIRE_FALSE(single_analysis.max_speed_mps.has_value());
}

TEST_CASE("VideoTimeline clamps edits and uses exact frame timestamps",
          "[video][timeline]") {
  cosmo::video::FlightAnalysis short_flight;
  short_flight.duration_seconds = 10.0;
  short_flight.start_index = 0;
  short_flight.apogee_index = 1;
  short_flight.end_index = 3;
  short_flight.display_seconds = {0.0, 5.0, 10.0};
  const cosmo::video::VideoTimeline short_timeline(short_flight);
  REQUIRE(short_timeline.total_seconds() == Catch::Approx(20.0));
  REQUIRE(short_timeline.frame_count() == 600);
  REQUIRE(short_timeline.frame_start_microseconds(30) == 1000000LL);

  auto long_flight = short_flight;
  long_flight.duration_seconds = 600.0;
  long_flight.display_seconds = {0.0, 300.0, 600.0};
  const cosmo::video::VideoTimeline long_timeline(long_flight);
  REQUIRE(long_timeline.total_seconds() == Catch::Approx(45.0));
  REQUIRE(long_timeline.source_seconds_at(0.0) == Catch::Approx(0.0));
  REQUIRE(long_timeline.source_seconds_at(45.0) == Catch::Approx(600.0));
  double previous_source = -1.0;
  long long previous_timestamp = -1;
  for (int frame = 0; frame <= long_timeline.frame_count(); ++frame) {
    const double video_seconds = static_cast<double>(frame) / 30.0;
    const double source_seconds =
        long_timeline.source_seconds_at(video_seconds);
    const long long timestamp = long_timeline.frame_start_microseconds(frame);
    REQUIRE(source_seconds >= previous_source);
    REQUIRE(timestamp > previous_timestamp);
    previous_source = source_seconds;
    previous_timestamp = timestamp;
  }
}

TEST_CASE("VideoExportPreset selects all agreed resolutions",
          "[video][preset]") {
  const auto portrait = cosmo::video::VideoExportPreset::make(
      cosmo::video::VideoOrientation::portrait,
      cosmo::video::VideoQuality::full_hd_1080p);
  REQUIRE(portrait.resolution == QSize(1080, 1920));
  REQUIRE(portrait.video_bit_rate == 8000000);

  const auto draft = cosmo::video::VideoExportPreset::make(
      cosmo::video::VideoOrientation::landscape,
      cosmo::video::VideoQuality::draft_720p);
  REQUIRE(draft.resolution == QSize(1280, 720));
  REQUIRE(draft.video_bit_rate == 4000000);
}
