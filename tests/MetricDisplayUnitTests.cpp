#include "gui/AttitudeMath.h"
#include "gui/widgets/ChartSamples.h"
#include "gui/widgets/MetricDefs.h"
#include "services/preview/FlightPreviewCache.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

TEST_CASE("Rocket attitude uses a right handed upright XYZ reference",
          "[gui][attitude]") {
  using cosmo::gui::attitudeQuaternion;
  const auto upright = attitudeQuaternion({0, 0, 0});
  REQUIRE(upright.has_value());
  REQUIRE(upright->rotatedVector({0, 1, 0}).y() == Catch::Approx(1));
  const auto tiltX = attitudeQuaternion({90, 0, 0});
  REQUIRE(tiltX.has_value());
  REQUIRE(tiltX->rotatedVector({0, 1, 0}).z() == Catch::Approx(1));
  const auto tiltZ = attitudeQuaternion({0, 0, 90});
  REQUIRE(tiltZ.has_value());
  REQUIRE(tiltZ->rotatedVector({0, 1, 0}).x() == Catch::Approx(-1));
  const auto roll = attitudeQuaternion({0, 90, 0});
  REQUIRE(roll.has_value());
  REQUIRE(roll->rotatedVector({0, 1, 0}).y() == Catch::Approx(1));
  REQUIRE(roll->rotatedVector({1, 0, 0}).z() == Catch::Approx(-1));
  const auto wrapped = attitudeQuaternion({0, 720, 0});
  REQUIRE(wrapped.has_value());
  REQUIRE(wrapped->rotatedVector({1, 0, 0}).x() == Catch::Approx(1));
  const double missing = std::numeric_limits<double>::quiet_NaN();
  REQUIRE_FALSE(attitudeQuaternion({missing, 0, 0}).has_value());
  REQUIRE_FALSE(
      attitudeQuaternion({0, std::numeric_limits<double>::infinity(), 0})
          .has_value());
}

TEST_CASE("Chart overview preserves spikes in every trace and missing-value boundaries", "[gui][chart]") {
    std::vector<FlightSample> samples(12000);
    for (int i = 0; i < static_cast<int>(samples.size()); ++i) {
        samples[i].timestamp = i * 10;
        samples[i].altitude = 100;
        samples[i].temperature = 20;
    }
    samples[4501].altitude = 12345.678901;
    samples[4502].altitude = -9876.54321;
    samples[4503].temperature = 890.125;
    samples[4504].temperature = -765.25;
    samples[8000].altitude = std::numeric_limits<double>::quiet_NaN();
    std::array<bool, MetricDefs::kMetricCount> enabled{};
    enabled[0] = enabled[1] = true;
    const auto overview = ChartSamples::select(samples, 0, 12000, 1000, enabled);
    REQUIRE(overview.size() < samples.size());
    REQUIRE(std::is_sorted(overview.begin(), overview.end()));
    REQUIRE(overview.front() == 0);
    REQUIRE(overview.back() == 11999);
    for (int index : {4501, 4502, 4503, 4504, 7999, 8000, 8001}) {
        REQUIRE(std::binary_search(overview.begin(), overview.end(), index));
    }
    const auto detail = ChartSamples::select(samples, 4490, 4520, 1000, enabled);
    REQUIRE(detail.size() == 30);
    for (int i = 0; i < 30; ++i)
        REQUIRE(detail[i] == 4490 + i);
    REQUIRE(samples[4501].altitude == 12345.678901);
    REQUIRE(samples[4503].temperature == 890.125);
    REQUIRE(ChartSamples::select(samples, 20, 10, 0, enabled).empty());
}

TEST_CASE("Chart overview bounds alternating missing measurements",
          "[gui][chart][chart-budget]") {
  std::vector<FlightSample> samples(100000);
  for (std::size_t i = 0; i < samples.size(); ++i) {
    samples[i].altitude = i % 2 == 0 ? static_cast<double>(i)
                                     : std::numeric_limits<double>::quiet_NaN();
  }
  std::array<bool, MetricDefs::kMetricCount> enabled{};
  enabled[0] = true;
  const auto indices = ChartSamples::select(samples, 0, 100000, 1000, enabled);
  REQUIRE(indices.size() <= 1000);
  REQUIRE(indices.front() == 0);
  REQUIRE(indices.back() == 99999);
  for (const int budget : {0, 1, 2, 3, 7}) {
    REQUIRE(ChartSamples::select(samples, 0, 100000, budget, enabled).size() <=
            static_cast<std::size_t>(budget));
  }
}

TEST_CASE("Replay overview retains every metric's extrema and gap boundaries",
          "[preview][chart][chart-budget]") {
  FlightSession session;
  session.samples.resize(12000);
  for (int i = 0; i < 12000; ++i) {
    session.samples[i].timestamp = i * 10;
    session.samples[i].altitude = 100;
    session.samples[i].temperature = 20;
  }
  session.samples[4501].altitude = 12345;
  session.samples[4502].altitude = -9876;
  session.samples[4503].temperature = 890;
  session.samples[4504].temperature = -765;
  session.samples[8000].altitude = std::numeric_limits<double>::quiet_NaN();
  std::array<bool, MetricDefs::kMetricCount> enabled{};
  enabled[0] = enabled[1] = true;
  const auto preview = cosmo::preview::FlightPreviewCache::build(session);
  REQUIRE(preview);
  const auto indices = preview->chartIndices(session, 0, 12000, 1000, enabled);
  REQUIRE(indices.size() <= 1000);
  for (const int index : {4501, 4502, 4503, 4504, 7999, 8000, 8001}) {
    REQUIRE(std::binary_search(indices.begin(), indices.end(), index));
  }
}

TEST_CASE("Replay gap queries respect metric and half-open range boundaries",
          "[preview][chart][chart-budget]") {
  FlightSession session;
  session.samples.resize(300);
  const double missing = std::numeric_limits<double>::quiet_NaN();
  for (int i = 0; i < 300; ++i)
    session.samples[i].timestamp = i;
  for (const int index : {0, 63, 64, 127, 128, 299})
    session.samples[index].altitude = missing;
  const auto preview = cosmo::preview::FlightPreviewCache::build(session);
  REQUIRE(preview);
  for (const int index : {0, 63, 64, 127, 128, 299}) {
    REQUIRE(preview->chartHasMissing(session, 0, index, index + 1));
    REQUIRE_FALSE(preview->chartHasMissing(session, 1, index, index + 1));
    REQUIRE_FALSE(preview->chartHasMissing(session, 0, index, index));
  }
  REQUIRE_FALSE(preview->chartHasMissing(session, 0, 1, 63));
  REQUIRE_FALSE(preview->chartHasMissing(session, 0, 129, 299));
  REQUIRE(preview->chartHasMissing(session, 0, 1, 299));
  FlightSession other = session;
  REQUIRE(preview->chartHasMissing(other, 0, 1, 63));
}

TEST_CASE("Replay chart summaries observe cancellation",
          "[preview][chart][chart-budget]") {
  std::vector<FlightSample> samples(100000);
  cosmo::detail::CancellationState cancellation([] { return true; });
  cosmo::preview::ChartOverview overview;
  REQUIRE_FALSE(overview.build(samples, cancellation));
}

TEST_CASE("Metric display conversion preserves SI values in metric mode", "[gui][units]") {
    constexpr double sourceValue = 123.456;
    for (int metricIndex = 0; metricIndex < MetricDefs::kMetricCount; ++metricIndex) {
        REQUIRE(MetricDefs::metricDisplayValue(metricIndex, sourceValue, false)
                == Catch::Approx(sourceValue));
    }
}

TEST_CASE("Metric display conversion applies imperial presentation units",
          "[gui][units]")
{
    REQUIRE(MetricDefs::metricDisplayValue(0, 100.0, true)
            == Catch::Approx(328.0839895013123));
    REQUIRE(MetricDefs::metricDisplayValue(1, 0.0, true)
            == Catch::Approx(32.0));
    REQUIRE(MetricDefs::metricDisplayValue(1, 100.0, true)
            == Catch::Approx(212.0));
    REQUIRE(MetricDefs::metricDisplayValue(2, 101325.0, true)
            == Catch::Approx(14.69594877551345));
    REQUIRE(MetricDefs::metricDisplayValue(3, 9.80665, true)
            == Catch::Approx(32.17404855643044));

    // Domain-standard values do not change between display systems.
    REQUIRE(MetricDefs::metricDisplayValue(4, 12.4, true)
            == Catch::Approx(12.4));
    REQUIRE(MetricDefs::metricDisplayValue(6, 2.5, true)
            == Catch::Approx(2.5));

    REQUIRE(MetricDefs::verticalSpeedDisplayValue(10.0, true)
            == Catch::Approx(32.80839895013123));
    REQUIRE(MetricDefs::verticalSpeedDisplayValue(10.0, false)
            == Catch::Approx(10.0));
}

TEST_CASE("Metric display labels and formatting follow the selected units",
          "[gui][units]")
{
    REQUIRE(MetricDefs::metricDisplayTitle(0, false) == QStringLiteral("Altitude (m)"));
    REQUIRE(MetricDefs::metricDisplayTitle(0, true) == QStringLiteral("Altitude (ft)"));
    REQUIRE(MetricDefs::metricDisplayTitle(3, true)
            == QStringLiteral("|Acceleration| (ft/s\u00b2)"));
    REQUIRE(MetricDefs::metricDisplayUnitShort(1, false) == QStringLiteral("\u00b0C"));
    REQUIRE(MetricDefs::metricDisplayUnitShort(1, true) == QStringLiteral("\u00b0F"));
    REQUIRE(MetricDefs::metricDisplayUnitShort(2, true) == QStringLiteral("psi"));
    REQUIRE(MetricDefs::metricDisplayUnitShort(3, true) == QStringLiteral("ft/s\u00b2"));
    REQUIRE(MetricDefs::verticalSpeedDisplayUnit(false) == QStringLiteral("m/s"));
    REQUIRE(MetricDefs::verticalSpeedDisplayUnit(true) == QStringLiteral("ft/s"));

    REQUIRE(MetricDefs::formatMetricDisplayValue(0, 100.0, true)
            == QStringLiteral("328.08"));
    REQUIRE(MetricDefs::formatMetricDisplayValue(1, 0.0, true)
            == QStringLiteral("32.00"));
    REQUIRE(MetricDefs::formatMetricDisplayValue(2, 101325.0, false)
            == QStringLiteral("101325"));
    REQUIRE(MetricDefs::formatMetricDisplayValue(2, 101325.0, true)
            == QStringLiteral("14.696"));
    REQUIRE(MetricDefs::formatMetricDisplayValue(3, 9.80665, true)
            == QStringLiteral("32.17"));
    REQUIRE(MetricDefs::formatMetricDisplayValue(
                0, std::numeric_limits<double>::quiet_NaN(), true)
            == QStringLiteral("\u2014"));
}

TEST_CASE("Metric display helpers reject invalid metadata indices safely",
          "[gui][units]")
{
    REQUIRE(MetricDefs::metricDisplayTitle(-1, true).isEmpty());
    REQUIRE(MetricDefs::metricDisplayTitle(MetricDefs::kMetricCount, false).isEmpty());
    REQUIRE(MetricDefs::metricDisplayUnitShort(-1, false).isEmpty());
    REQUIRE(MetricDefs::metricDisplayUnitShort(MetricDefs::kMetricCount, true).isEmpty());
}

TEST_CASE("Replay clock formatting rounds ordinary durations", "[gui][replay]")
{
    REQUIRE(MetricDefs::formatReplayClockHms(0.0) == QStringLiteral("0:00"));
    REQUIRE(MetricDefs::formatReplayClockHms(59.49) == QStringLiteral("0:59"));
    REQUIRE(MetricDefs::formatReplayClockHms(59.5) == QStringLiteral("1:00"));
    REQUIRE(MetricDefs::formatReplayClockHms(3599.5) == QStringLiteral("1:00:00"));
}

TEST_CASE("Replay elapsed-time calculation avoids signed timestamp overflow",
          "[gui][replay]")
{
    REQUIRE(MetricDefs::replayDurationSeconds(1000L, 2500L)
            == Catch::Approx(1.5));
    REQUIRE(MetricDefs::replayDurationSeconds(2500L, 1000L) == 0.0);
    REQUIRE(MetricDefs::replayDurationSeconds(2500L, 2500L) == 0.0);

    const double extremeDuration = MetricDefs::replayDurationSeconds(
        std::numeric_limits<long>::min(),
        std::numeric_limits<long>::max());
    const double expectedExtremeDuration = static_cast<double>(
        (static_cast<long double>(std::numeric_limits<long>::max())
         - static_cast<long double>(std::numeric_limits<long>::min())) / 1000.0L);
    REQUIRE(std::isfinite(extremeDuration));
    REQUIRE(extremeDuration == Catch::Approx(expectedExtremeDuration));
}

TEST_CASE("Replay clock formatting rejects invalid and overflowing durations",
          "[gui][replay]")
{
    const QString unavailable = QStringLiteral("\u2014");
    REQUIRE(MetricDefs::formatReplayClockHms(-0.01) == unavailable);
    REQUIRE(MetricDefs::formatReplayClockHms(
                std::numeric_limits<double>::quiet_NaN()) == unavailable);
    REQUIRE(MetricDefs::formatReplayClockHms(
                std::numeric_limits<double>::infinity()) == unavailable);

    const double qint64UpperExclusive =
        std::ldexp(1.0, std::numeric_limits<qint64>::digits);
    const double largestSafeDouble = std::nextafter(qint64UpperExclusive, 0.0);
    const qint64 largestSafeTotal = static_cast<qint64>(largestSafeDouble);
    const qint64 hours = largestSafeTotal / qint64{3600};
    const qint64 minutes = (largestSafeTotal % qint64{3600}) / qint64{60};
    const qint64 seconds = largestSafeTotal % qint64{60};
    const QString expectedLargestSafe = QStringLiteral("%1:%2:%3")
        .arg(hours)
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));

    REQUIRE(MetricDefs::formatReplayClockHms(largestSafeDouble)
            == expectedLargestSafe);
    REQUIRE(MetricDefs::formatReplayClockHms(qint64UpperExclusive)
            == QStringLiteral("duration too large"));
    REQUIRE(MetricDefs::formatReplayClockHms(std::numeric_limits<double>::max())
            == QStringLiteral("duration too large"));
}

#include "gui/NavballMath.h"
TEST_CASE(
    "Navball looks along the rocket nose rather than assuming aircraft axes",
    "[mission][navball]") {
  using cosmo::gui::NavballFrame;
  const NavballFrame upright(QQuaternion{});
  REQUIRE(upright.ray(0, 0)->y() > 0.99);
  REQUIRE_FALSE(upright.ray(1, 1).has_value());
  const NavballFrame horizontal(QQuaternion::fromAxisAndAngle(1, 0, 0, 90));
  REQUIRE(std::abs(horizontal.ray(0, 0)->y()) < 0.001);
  REQUIRE(horizontal.ray(0, 0)->z() > 0.99);
  const NavballFrame inverted(QQuaternion::fromAxisAndAngle(0, 0, 1, 180));
  REQUIRE(inverted.ray(0, 0)->y() < -0.99);
  REQUIRE(upright.ray(0.6F, 0)->x() > 0.59F);
  REQUIRE(std::abs(upright.ray(0.6F, 0)->length() - 1) < 0.001);
}
