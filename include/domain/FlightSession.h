#ifndef COSMO_SOFT_FLIGHTSESSION_H
#define COSMO_SOFT_FLIGHTSESSION_H

#include <array>
#include <vector>

#include "FlightSample.h"

class FlightSession {
public:
    /** Matches dashboard trace ordering (alt, temp, press, |a|, batt, RSSI, gyro, lat, lon). */
    static constexpr std::size_t kTraceMetricCount = 9;

    std::vector<FlightSample> samples;

    /**
     * Per-metric: column/field was present in the loaded source (e.g. CSV header).
     * When false, the traces panel hides that row in replay. Defaults to all true
     * (live telemetry, TELEM binary, or callers that do not set column flags).
     */
    std::array<bool, kTraceMetricCount> metricsInSource{
        true, true, true, true, true, true, true, true, true};
};

#endif //COSMO_SOFT_FLIGHTSESSION_H