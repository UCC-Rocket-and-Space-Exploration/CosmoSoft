/**
 * @file MetricDefs.h
 * @brief Shared flight-metric metadata used by DashboardPage, TracesPanel, and ReplayBar.
 *
 * All functions are inline so this is a header-only unit with no corresponding .cpp.
 * The static local arrays are safe with inline linkage (one instance per program).
 */

#pragma once

#include <QColor>
#include <QLocale>
#include <QString>

#include <cmath>
#include <limits>

#include "domain/FlightSample.h"
#include "gui/Theme.h"

namespace MetricDefs {

/** Number of supported telemetry metrics (matches DashboardPage::kMetricCount). */
constexpr int kMetricCount = 9;

/** Full axis / legend title for metric @p idx. */
inline QString metricTitle(int idx)
{
    using namespace Qt::StringLiterals;
    static const QString titles[] = {
        u"Altitude (m)"_s,
        u"Temperature (\u00b0C)"_s,
        u"Pressure (Pa)"_s,
        u"|Acceleration| (m/s\u00b2)"_s,
        u"Battery (V)"_s,
        u"RSSI"_s,
        u"|Gyro| (rad/s)"_s,
        u"Latitude (\u00b0)"_s,
        u"Longitude (\u00b0)"_s,
    };
    if (idx < 0 || idx >= kMetricCount) return {};
    return titles[idx];
}

/** Short label used on the traces panel checkbox. */
inline QString metricTraceShortName(int idx)
{
    using namespace Qt::StringLiterals;
    static const QString names[] = {
        u"Alt"_s, u"Temp"_s, u"Press"_s, u"|a|"_s,
        u"Batt"_s, u"RSSI"_s, u"Gyro"_s, u"Lat"_s, u"Lon"_s,
    };
    if (idx < 0 || idx >= kMetricCount) return {};
    return names[idx];
}

/** Accent color for metric @p idx. */
inline QColor metricColor(int idx)
{
    if (idx < 0 || idx >= kMetricCount) return {};
    return QColor(Theme::kTraceColor(idx));
}

/** Extracts the scalar value for metric @p idx from a sample. */
inline double sampleValueForMetric(const FlightSample &s, int idx)
{
    switch (idx) {
    case 0: return s.altitude;
    case 1: return s.temperature;
    case 2: return s.pressure;
    case 3: {
        const double x = s.acceleration.x, y = s.acceleration.y, z = s.acceleration.z;
        return std::sqrt(x * x + y * y + z * z);
    }
    case 4: return s.batteryVoltage;
    case 5: return s.rssi;
    case 6: {
        const double x = s.angularVelocity.x, y = s.angularVelocity.y, z = s.angularVelocity.z;
        return std::sqrt(x * x + y * y + z * z);
    }
    case 7: return s.coordinates.latitude;
    case 8: return s.coordinates.longitude;
    default: return 0.0;
    }
}

/** Human-readable quantity name used in hover readouts. */
inline QString metricQuantityName(int idx)
{
    using namespace Qt::StringLiterals;
    static const QString names[] = {
        u"Altitude"_s, u"Temperature"_s, u"Pressure"_s, u"|a|"_s,
        u"Battery"_s, u"RSSI"_s, u"|\u03c9|"_s, u"Latitude"_s, u"Longitude"_s,
    };
    if (idx < 0 || idx >= kMetricCount) return {};
    return names[idx];
}

/** Short SI unit string used on the Y axis and hover overlay. */
inline QString metricAxisUnitShort(int idx)
{
    using namespace Qt::StringLiterals;
    static const QString units[] = {
        u"m"_s, u"\u00b0C"_s, u"Pa"_s, u"m/s\u00b2"_s,
        u"V"_s, u"dBm"_s, u"rad/s"_s, u"\u00b0"_s, u"\u00b0"_s,
    };
    if (idx < 0 || idx >= kMetricCount) return {};
    return units[idx];
}

/** Formats a raw metric value with the appropriate number of decimal places. */
inline QString formatMetricValuePretty(int idx, double v)
{
    switch (idx) {
    case 7: case 8: return QString::number(v, 'f', 6);
    case 0: case 1: case 4: return QString::number(v, 'f', 2);
    case 5: return QString::number(v, 'f', 1);
    default: return QString::number(v, 'g', 6);
    }
}

// ── Timestamp / X-axis helpers ─────────────────────────────────────────────
//
// These helpers convert raw sample timestamps (which may be either Unix-epoch
// milliseconds or boot-relative milliseconds) to the floating-point seconds
// used as chart X values, and format elapsed durations for the replay clock.

/** Threshold separating Unix-epoch milliseconds from boot-relative milliseconds. */
constexpr long long kUnixEpochMsThreshold = 100'000'000'000LL;

/** Returns true when @p tMs looks like a Unix timestamp (not a boot-time counter). */
inline bool timestampLooksLikeUnixMs(long tMs)
{
    return std::fabs(static_cast<double>(tMs)) >= static_cast<double>(kUnixEpochMsThreshold);
}

/**
 * Returns true when the session's time axis should show elapsed time from the
 * first sample rather than raw milliseconds.
 */
inline bool useSessionElapsedTimeAxis(long tFirstMs, long tLastMs)
{
    return timestampLooksLikeUnixMs(tFirstMs) || timestampLooksLikeUnixMs(tLastMs);
}

/**
 * Converts a raw timestamp to the X-axis value in seconds.
 *
 * In session-elapsed mode the first sample is t = 0; in boot-time mode the
 * raw value is simply divided by 1000.
 */
inline double chartXSeconds(long refFirstMs, long tMs, bool sessionElapsed)
{
    const long double milliseconds = sessionElapsed
        ? static_cast<long double>(tMs) - static_cast<long double>(refFirstMs)
        : static_cast<long double>(tMs);
    return static_cast<double>(milliseconds / 1000.0L);
}

/** Formats an integer using the system locale's grouping separator. */
inline QString formatIntGrouped(int v)
{
    return QLocale::system().toString(v);
}

/**
 * @brief Computes a non-negative replay duration without overflowing signed timestamps.
 * @param firstTimestampMs Timestamp of the first sample, in milliseconds.
 * @param lastTimestampMs Timestamp of the last sample, in milliseconds.
 * @return Elapsed seconds, or zero when the timestamps are not increasing.
 */
inline double replayDurationSeconds(long firstTimestampMs, long lastTimestampMs)
{
    const long double milliseconds = static_cast<long double>(lastTimestampMs)
        - static_cast<long double>(firstTimestampMs);
    if (milliseconds <= 0.0L) {
        return 0.0;
    }
    return static_cast<double>(milliseconds / 1000.0L);
}

/** Formats a duration in seconds as m:ss or h:mm:ss. */
inline QString formatReplayClockHms(double sec)
{
    if (!std::isfinite(sec) || sec < 0.0) return QString(u"\u2014");

    // Converting an out-of-range floating-point value to an integer is
    // undefined behaviour.  2^63 is exactly representable as a double, so
    // every representable non-negative value below this bound fits qint64.
    const double qint64UpperExclusive =
        std::ldexp(1.0, std::numeric_limits<qint64>::digits);
    if (sec >= qint64UpperExclusive)
        return QStringLiteral("duration too large");

    const double rounded = std::floor(sec + 0.5);
    if (rounded >= qint64UpperExclusive)
        return QStringLiteral("duration too large");

    const qint64 total = static_cast<qint64>(rounded);
    const qint64 h = total / qint64{3600};
    const qint64 m = (total % qint64{3600}) / qint64{60};
    const qint64 s = total % qint64{60};
    if (h > 0)
        return QStringLiteral("%1:%2:%3").arg(h)
            .arg(m, 2, 10, QLatin1Char('0'))
            .arg(s, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

// ── Display-unit helpers ───────────────────────────────────────────────────
//
// Domain samples always remain in SI units. These helpers are the only layer
// that translates telemetry into the user's selected presentation units.

/** Exact international conversion factor from metres to feet. */
constexpr double kFeetPerMetre = 3.280839895013123;

/** Conversion factor from pascals to pounds per square inch. */
constexpr double kPsiPerPascal = 0.00014503773773020923;

/** @brief Converts an SI metric value to the selected display unit. */
inline double metricDisplayValue(int metricIdx, double siValue, bool imperial)
{
    if (!imperial) {
        return siValue;
    }
    switch (metricIdx) {
    case 0: return siValue * kFeetPerMetre;
    case 1: return siValue * (9.0 / 5.0) + 32.0;
    case 2: return siValue * kPsiPerPascal;
    case 3: return siValue * kFeetPerMetre;
    default: return siValue;
    }
}

/** @brief Returns the selected short unit string for metric @p idx. */
inline QString metricDisplayUnitShort(int idx, bool imperial)
{
    using namespace Qt::StringLiterals;
    if (!imperial) {
        return metricAxisUnitShort(idx);
    }
    static const QString units[] = {
        u"ft"_s, u"\u00b0F"_s, u"psi"_s, u"ft/s\u00b2"_s,
        u"V"_s, u"dBm"_s, u"rad/s"_s, u"\u00b0"_s, u"\u00b0"_s,
    };
    if (idx < 0 || idx >= kMetricCount) return {};
    return units[idx];
}

/** @brief Returns a chart or tooltip title using the selected display unit. */
inline QString metricDisplayTitle(int idx, bool imperial)
{
    using namespace Qt::StringLiterals;
    if (!imperial) {
        return metricTitle(idx);
    }
    static const QString titles[] = {
        u"Altitude (ft)"_s,
        u"Temperature (\u00b0F)"_s,
        u"Pressure (psi)"_s,
        u"|Acceleration| (ft/s\u00b2)"_s,
        u"Battery (V)"_s,
        u"RSSI"_s,
        u"|Gyro| (rad/s)"_s,
        u"Latitude (\u00b0)"_s,
        u"Longitude (\u00b0)"_s,
    };
    if (idx < 0 || idx >= kMetricCount) return {};
    return titles[idx];
}

/**
 * @brief Converts and formats an SI metric value for the selected unit system.
 * @return A locale-independent numeric string suitable for charts and telemetry.
 */
inline QString formatMetricDisplayValue(int idx, double siValue, bool imperial)
{
    if (!std::isfinite(siValue)) {
        return QString(u"\u2014");
    }
    const double displayValue = metricDisplayValue(idx, siValue, imperial);
    if (idx == 2) {
        return QString::number(displayValue, 'f', imperial ? 3 : 0);
    }
    if (idx == 3) {
        return QString::number(displayValue, 'f', 2);
    }
    return formatMetricValuePretty(idx, displayValue);
}

/** @brief Converts SI vertical speed to metres/second or feet/second for display. */
inline double verticalSpeedDisplayValue(double metresPerSecond, bool imperial)
{
    return imperial ? metresPerSecond * kFeetPerMetre : metresPerSecond;
}

/** @brief Returns the selected vertical-speed unit string. */
inline QString verticalSpeedDisplayUnit(bool imperial)
{
    using namespace Qt::StringLiterals;
    return imperial ? u"ft/s"_s : u"m/s"_s;
}

} // namespace MetricDefs
