/**
 * @file FlightAnalyzer.h
 * @brief Deterministic post-flight boundary and headline-metric analysis.
 */

#ifndef COSMO_SOFT_FLIGHTANALYZER_H
#define COSMO_SOFT_FLIGHTANALYZER_H

#include "domain/FlightSession.h"

#include <optional>
#include <string>
#include <vector>

namespace cosmo::video {

/** @brief Identifies how a calculated speed value was obtained. */
enum class SpeedSource {
  unavailable,
  three_dimensional,
  vertical,
};

/** @brief A non-fatal data-quality or automatic-analysis warning. */
struct AnalysisWarning {
  enum class Code {
    automatic_start_unavailable,
    automatic_end_unavailable,
    corrected_timestamps,
    speed_unavailable,
    gps_speed_unavailable,
  };

  Code code;
  std::string message;
};

/** @brief Optional caller-selected sample range, with an exclusive end index.
 */
struct FlightTrimRange {
  int start_index = 0;
  int end_index = 0;
};

/** @brief GPS coverage measured inside the selected flight range. */
struct GpsQuality {
  int valid_sample_count = 0;
  int selected_sample_count = 0;
  double coverage_ratio = 0.0;
  bool sufficient_for_speed = false;
};

/** @brief Immutable values needed by the replay-video timeline and summary. */
struct FlightAnalysis {
  int start_index = 0;
  int end_index = 0;
  int apogee_index = 0;
  bool automatic_bounds = false;

  double baseline_altitude_m = 0.0;
  double peak_height_m = 0.0;
  double max_recorded_altitude_m = 0.0;
  double duration_seconds = 0.0;
  std::optional<double> max_speed_mps;
  SpeedSource speed_source = SpeedSource::unavailable;
  GpsQuality gps_quality;

  std::vector<double> display_seconds;
  std::vector<double> speed_mps;
  std::vector<AnalysisWarning> warnings;
};

/** @brief Computes robust post-flight bounds and shareable headline metrics. */
class FlightAnalyzer {
public:
  /**
   * @brief Analyze a flight session without modifying its samples.
   * @param session Source samples in acquisition order.
   * @param trim Optional user-selected range; end_index is exclusive.
   * @return Deterministic analysis values and any quality warnings.
   */
  [[nodiscard]] static FlightAnalysis
  analyze(const FlightSession &session,
          std::optional<FlightTrimRange> trim = std::nullopt);
};

} // namespace cosmo::video

#endif // COSMO_SOFT_FLIGHTANALYZER_H
