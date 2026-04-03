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

#include "domain/FlightSample.h"

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
        u"Pressure"_s,
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
    static const QColor colors[] = {
        QColor("#5b9bd5"), QColor("#70c1a5"), QColor("#f0b429"), QColor("#c084fc"),
        QColor("#7dd36f"), QColor("#67b8ff"), QColor("#ff9f6b"), QColor("#8ec5ff"),
        QColor("#f5a3b8"),
    };
    return colors[(idx + kMetricCount * 10) % kMetricCount];
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
        u"m"_s, u"\u00b0C"_s, u"(Pa)"_s, u"m/s\u00b2"_s,
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

// ── Timestamp / clock helpers ──────────────────────────────────────────────

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
    if (sessionElapsed) return static_cast<double>(tMs - refFirstMs) / 1000.0;
    return static_cast<double>(tMs) / 1000.0;
}

/** Formats an integer using the system locale's grouping separator. */
inline QString formatIntGrouped(int v)
{
    return QLocale::system().toString(v);
}

/** Formats a duration in seconds as m:ss or h:mm:ss. */
inline QString formatReplayClockHms(double sec)
{
    if (!std::isfinite(sec) || sec < 0.0) return QString(u"\u2014");
    const int total = static_cast<int>(std::floor(sec + 0.5));
    const int h = total / 3600;
    const int m = (total % 3600) / 60;
    const int s = total % 60;
    if (h > 0)
        return QStringLiteral("%1:%2:%3").arg(h)
            .arg(m, 2, 10, QLatin1Char('0'))
            .arg(s, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

} // namespace MetricDefs
