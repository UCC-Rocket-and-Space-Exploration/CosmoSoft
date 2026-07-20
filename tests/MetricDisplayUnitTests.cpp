#include "gui/widgets/MetricDefs.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>

TEST_CASE("Metric display conversion preserves SI values in metric mode",
          "[gui][units]")
{
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
