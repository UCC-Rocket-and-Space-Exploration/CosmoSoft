#pragma once

#include "gui/widgets/MetricDefs.h"
#include "services/preview/ChartOverview.h"

namespace ChartSamples {

/**
 * @brief Select original extrema and representative gap boundaries within a
 * strict budget. Renderers must inspect skipped raw ranges before connecting
 * selected points.
 */
inline std::vector<int>
select(const std::vector<FlightSample> &samples, int begin, int end, int budget,
       const std::array<bool, MetricDefs::kMetricCount> &enabled) {
  return cosmo::preview::ChartOverview{}.select(samples, begin, end, budget,
                                                enabled);
}

} // namespace ChartSamples
