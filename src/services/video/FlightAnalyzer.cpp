#include "services/video/FlightAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace cosmo::video {
namespace {

constexpr double kEarthRadiusM = 6371000.0;
constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;

struct Timeline {
  std::vector<double> seconds;
  std::vector<bool> valid_segment;
  bool corrected = false;
};

[[nodiscard]] bool finite(double value) { return std::isfinite(value); }

[[nodiscard]] bool valid_coordinate(const FlightSample &sample) {
  const double lat = sample.coordinates.latitude;
  const double lon = sample.coordinates.longitude;
  return finite(lat) && finite(lon) && std::abs(lat) <= 90.0 &&
         std::abs(lon) <= 180.0 &&
         !(std::abs(lat) < 1e-9 && std::abs(lon) < 1e-9);
}

[[nodiscard]] double wrapped_longitude_delta(double longitude,
                                             double reference) {
  return std::remainder(longitude - reference, 360.0);
}

[[nodiscard]] double median(std::vector<double> values) {
  values.erase(std::remove_if(values.begin(), values.end(),
                              [](double value) { return !finite(value); }),
               values.end());
  if (values.empty()) {
    return 0.0;
  }
  const auto middle =
      values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2U);
  std::nth_element(values.begin(), middle, values.end());
  if (values.size() % 2U != 0U) {
    return *middle;
  }
  const double upper = *middle;
  const double lower = *std::max_element(values.begin(), middle);
  return (lower + upper) * 0.5;
}

[[nodiscard]] Timeline
build_timeline(const std::vector<FlightSample> &samples) {
  Timeline timeline;
  timeline.seconds.assign(samples.size(), 0.0);
  timeline.valid_segment.assign(samples.size(), true);
  if (samples.size() < 2U) {
    return timeline;
  }

  std::vector<double> positive_deltas_ms;
  positive_deltas_ms.reserve(samples.size() - 1U);
  for (std::size_t i = 1; i < samples.size(); ++i) {
    const long delta = samples[i].timestamp - samples[i - 1U].timestamp;
    if (delta > 0) {
      positive_deltas_ms.push_back(static_cast<double>(delta));
    }
  }
  const double replacement_ms =
      std::max(1.0, median(std::move(positive_deltas_ms)));
  for (std::size_t i = 1; i < samples.size(); ++i) {
    long delta_ms = samples[i].timestamp - samples[i - 1U].timestamp;
    if (delta_ms <= 0) {
      delta_ms = static_cast<long>(std::lround(replacement_ms));
      timeline.valid_segment[i] = false;
      timeline.corrected = true;
    }
    timeline.seconds[i] =
        timeline.seconds[i - 1U] + static_cast<double>(delta_ms) / 1000.0;
  }
  return timeline;
}

[[nodiscard]] double median_altitude(const std::vector<FlightSample> &samples,
                                     int begin, int end) {
  begin = std::clamp(begin, 0, static_cast<int>(samples.size()));
  end = std::clamp(end, begin, static_cast<int>(samples.size()));
  std::vector<double> altitudes;
  altitudes.reserve(static_cast<std::size_t>(end - begin));
  for (int i = begin; i < end; ++i) {
    altitudes.push_back(samples[static_cast<std::size_t>(i)].altitude);
  }
  return median(std::move(altitudes));
}

[[nodiscard]] int first_index_at_or_after(const std::vector<double> &seconds,
                                          int begin, double target) {
  const auto first =
      seconds.begin() + std::clamp(begin, 0, static_cast<int>(seconds.size()));
  const auto it = std::lower_bound(first, seconds.end(), target);
  return static_cast<int>(it - seconds.begin());
}

[[nodiscard]] std::vector<double>
raw_vertical_speeds(const std::vector<FlightSample> &samples,
                    const Timeline &timeline) {
  std::vector<double> speeds(samples.size(),
                             std::numeric_limits<double>::quiet_NaN());
  for (std::size_t i = 1; i < samples.size(); ++i) {
    if (!timeline.valid_segment[i]) {
      continue;
    }
    const double dt = timeline.seconds[i] - timeline.seconds[i - 1U];
    if (dt <= 0.0 || dt > 5.0) {
      continue;
    }
    const double delta = samples[i].altitude - samples[i - 1U].altitude;
    if (finite(delta)) {
      speeds[i] = delta / dt;
    }
  }
  return speeds;
}

[[nodiscard]] int detect_launch(const std::vector<FlightSample> &samples,
                                const Timeline &timeline,
                                const std::vector<double> &vertical_speeds,
                                bool &detected) {
  detected = false;
  if (samples.front().timestamp < 0) {
    const auto it = std::find_if(
        samples.begin(), samples.end(),
        [](const FlightSample &sample) { return sample.timestamp >= 0; });
    if (it != samples.end()) {
      detected = true;
      return static_cast<int>(it - samples.begin());
    }
  }

  const int baseline_end =
      std::max(1, first_index_at_or_after(timeline.seconds, 0, 2.0));
  const double baseline = median_altitude(samples, 0, baseline_end);
  for (int candidate = 1; candidate < static_cast<int>(samples.size());
       ++candidate) {
    const double speed = vertical_speeds[static_cast<std::size_t>(candidate)];
    const double height =
        samples[static_cast<std::size_t>(candidate)].altitude - baseline;
    if (!finite(speed) || speed < 3.0 || height < 3.0) {
      continue;
    }
    const double until =
        timeline.seconds[static_cast<std::size_t>(candidate)] + 0.5;
    const int end = first_index_at_or_after(timeline.seconds, candidate, until);
    if (end >= static_cast<int>(samples.size())) {
      continue;
    }
    bool sustained = true;
    for (int i = candidate; i <= end; ++i) {
      const double current = vertical_speeds[static_cast<std::size_t>(i)];
      if (!finite(current) || current < 3.0) {
        sustained = false;
        break;
      }
    }
    if (sustained) {
      detected = true;
      return candidate;
    }
  }
  return 0;
}

[[nodiscard]] int detect_landing(const std::vector<FlightSample> &samples,
                                 const Timeline &timeline,
                                 const std::vector<double> &vertical_speeds,
                                 int apogee, double baseline,
                                 double peak_height, bool &detected) {
  detected = false;
  const double landing_height = std::max(5.0, peak_height * 0.05);
  for (int candidate = std::max(apogee + 1, 1);
       candidate < static_cast<int>(samples.size()); ++candidate) {
    const double speed = vertical_speeds[static_cast<std::size_t>(candidate)];
    const double height =
        samples[static_cast<std::size_t>(candidate)].altitude - baseline;
    if (!finite(speed) || std::abs(speed) > 1.0 ||
        std::abs(height) > landing_height) {
      continue;
    }
    const double until =
        timeline.seconds[static_cast<std::size_t>(candidate)] + 3.0;
    const int end = first_index_at_or_after(timeline.seconds, candidate, until);
    if (end >= static_cast<int>(samples.size())) {
      continue;
    }
    bool sustained = true;
    for (int i = candidate; i <= end; ++i) {
      const double current_speed = vertical_speeds[static_cast<std::size_t>(i)];
      const double current_height =
          samples[static_cast<std::size_t>(i)].altitude - baseline;
      if (!finite(current_speed) || std::abs(current_speed) > 1.0 ||
          std::abs(current_height) > landing_height) {
        sustained = false;
        break;
      }
    }
    if (sustained) {
      detected = true;
      return end + 1;
    }
  }
  return static_cast<int>(samples.size());
}

[[nodiscard]] bool valid_time_window(const Timeline &timeline, int begin,
                                     int end) {
  for (int i = begin + 1; i <= end; ++i) {
    if (!timeline.valid_segment[static_cast<std::size_t>(i)]) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::optional<double>
robust_regression_slope(const std::vector<double> &times,
                        const std::vector<double> &values, int begin, int end) {
  if (end - begin + 1 < 3) {
    return std::nullopt;
  }
  std::vector<double> slopes;
  slopes.reserve(
      static_cast<std::size_t>((end - begin + 1) * (end - begin) / 2));
  for (int left = begin; left < end; ++left) {
    const double left_value = values[static_cast<std::size_t>(left)];
    if (!finite(left_value)) {
      return std::nullopt;
    }
    for (int right = left + 1; right <= end; ++right) {
      const double right_value = values[static_cast<std::size_t>(right)];
      const double delta_time = times[static_cast<std::size_t>(right)] -
                                times[static_cast<std::size_t>(left)];
      if (!finite(right_value) || delta_time <= 1e-9) {
        return std::nullopt;
      }
      slopes.push_back((right_value - left_value) / delta_time);
    }
  }
  return slopes.empty() ? std::nullopt
                        : std::optional<double>(median(std::move(slopes)));
}

[[nodiscard]] std::vector<double>
calculate_speeds(const std::vector<FlightSample> &samples,
                 const Timeline &timeline, int begin, int end,
                 SpeedSource &source) {
  std::vector<double> speeds(samples.size(),
                             std::numeric_limits<double>::quiet_NaN());
  const int count = end - begin;
  const int gps_count = static_cast<int>(std::count_if(
      samples.begin() + begin, samples.begin() + end, valid_coordinate));
  const bool use_gps =
      count >= 5 && gps_count >= 5 &&
      static_cast<double>(gps_count) / static_cast<double>(count) >= 0.6;
  source = use_gps ? SpeedSource::three_dimensional : SpeedSource::vertical;

  std::vector<double> altitude(samples.size(),
                               std::numeric_limits<double>::quiet_NaN());
  std::vector<double> east(samples.size(),
                           std::numeric_limits<double>::quiet_NaN());
  std::vector<double> north(samples.size(),
                            std::numeric_limits<double>::quiet_NaN());
  double reference_lat = 0.0;
  double reference_lon = 0.0;
  if (use_gps) {
    const auto reference = std::find_if(
        samples.begin() + begin, samples.begin() + end, valid_coordinate);
    reference_lat = reference->coordinates.latitude;
    reference_lon = reference->coordinates.longitude;
  }
  const double cos_lat = std::cos(reference_lat * kDegreesToRadians);
  for (int i = begin; i < end; ++i) {
    const auto &sample = samples[static_cast<std::size_t>(i)];
    altitude[static_cast<std::size_t>(i)] = sample.altitude;
    if (use_gps && valid_coordinate(sample)) {
      east[static_cast<std::size_t>(i)] =
          wrapped_longitude_delta(sample.coordinates.longitude, reference_lon) *
          kDegreesToRadians * kEarthRadiusM * cos_lat;
      north[static_cast<std::size_t>(i)] =
          (sample.coordinates.latitude - reference_lat) * kDegreesToRadians *
          kEarthRadiusM;
    }
  }

  for (int i = begin; i < end; ++i) {
    int left = std::max(begin, i - 2);
    int right = std::min(end - 1, i + 2);
    if (right - left + 1 < 3 || !valid_time_window(timeline, left, right) ||
        timeline.seconds[static_cast<std::size_t>(right)] -
                timeline.seconds[static_cast<std::size_t>(left)] >
            5.0) {
      continue;
    }
    const auto vertical =
        robust_regression_slope(timeline.seconds, altitude, left, right);
    if (!vertical) {
      continue;
    }
    if (!use_gps) {
      speeds[static_cast<std::size_t>(i)] = std::abs(*vertical);
      continue;
    }
    const auto vx =
        robust_regression_slope(timeline.seconds, east, left, right);
    const auto vy =
        robust_regression_slope(timeline.seconds, north, left, right);
    if (vx && vy) {
      speeds[static_cast<std::size_t>(i)] =
          std::sqrt((*vx * *vx) + (*vy * *vy) + (*vertical * *vertical));
    }
  }
  return speeds;
}

[[nodiscard]] std::optional<double>
supported_peak(const std::vector<double> &speeds, int begin, int end) {
  std::optional<double> peak;
  int supported_count = 0;
  for (int i = begin + 1; i + 1 < end; ++i) {
    const double value = speeds[static_cast<std::size_t>(i)];
    const double previous = speeds[static_cast<std::size_t>(i - 1)];
    const double next = speeds[static_cast<std::size_t>(i + 1)];
    if (!finite(value) || !finite(previous) || !finite(next) || value < 0.1 ||
        std::max(previous, next) < value * 0.5) {
      continue;
    }
    ++supported_count;
    peak = peak ? std::max(*peak, value) : value;
  }
  return supported_count >= 3 ? peak : std::nullopt;
}

} // namespace

FlightAnalysis FlightAnalyzer::analyze(const FlightSession &session,
                                       std::optional<FlightTrimRange> trim) {
  FlightAnalysis result;
  const auto &samples = session.samples;
  if (samples.empty()) {
    result.warnings.push_back({AnalysisWarning::Code::speed_unavailable,
                               "The flight contains no samples."});
    return result;
  }

  const Timeline timeline = build_timeline(samples);
  result.display_seconds = timeline.seconds;
  if (timeline.corrected) {
    result.warnings.push_back(
        {AnalysisWarning::Code::corrected_timestamps,
         "Timestamp repairs were excluded from speed analysis."});
  }

  const auto vertical_speeds = raw_vertical_speeds(samples, timeline);
  bool automatic_start = false;
  const int start =
      trim ? std::clamp(trim->start_index, 0,
                        static_cast<int>(samples.size()) - 1)
           : detect_launch(samples, timeline, vertical_speeds, automatic_start);
  if (!trim && !automatic_start) {
    result.warnings.push_back(
        {AnalysisWarning::Code::automatic_start_unavailable,
         "Launch was not detected confidently; session start selected."});
  }

  int provisional_apogee = start;
  for (int i = start + 1; i < static_cast<int>(samples.size()); ++i) {
    if (samples[static_cast<std::size_t>(i)].altitude >
        samples[static_cast<std::size_t>(provisional_apogee)].altitude) {
      provisional_apogee = i;
    }
  }
  int baseline_begin = start;
  while (
      baseline_begin > 0 &&
      timeline.seconds[static_cast<std::size_t>(start)] -
              timeline.seconds[static_cast<std::size_t>(baseline_begin - 1)] <=
          2.0) {
    --baseline_begin;
  }
  int baseline_end = start;
  if (start == 0) {
    baseline_end =
        std::max(1, first_index_at_or_after(timeline.seconds, 0,
                                            timeline.seconds.front() + 0.5));
  } else if (baseline_begin == baseline_end) {
    baseline_end = start + 1;
  }
  const double baseline =
      median_altitude(samples, baseline_begin, baseline_end);
  const double provisional_peak = std::max(
      0.0, samples[static_cast<std::size_t>(provisional_apogee)].altitude -
               baseline);

  bool automatic_end = false;
  const int end = trim ? std::clamp(trim->end_index, start + 1,
                                    static_cast<int>(samples.size()))
                       : detect_landing(samples, timeline, vertical_speeds,
                                        provisional_apogee, baseline,
                                        provisional_peak, automatic_end);
  if (!trim && !automatic_end) {
    result.warnings.push_back(
        {AnalysisWarning::Code::automatic_end_unavailable,
         "Landing was not detected confidently; session end selected."});
  }

  result.start_index = start;
  result.end_index = end;
  result.automatic_bounds = !trim && automatic_start && automatic_end;
  result.baseline_altitude_m = baseline;
  result.apogee_index = start;
  for (int i = start + 1; i < end; ++i) {
    if (samples[static_cast<std::size_t>(i)].altitude >
        samples[static_cast<std::size_t>(result.apogee_index)].altitude) {
      result.apogee_index = i;
    }
  }
  result.max_recorded_altitude_m =
      samples[static_cast<std::size_t>(result.apogee_index)].altitude;
  result.peak_height_m = std::max(0.0, result.max_recorded_altitude_m -
                                           result.baseline_altitude_m);
  result.duration_seconds =
      std::max(0.0, timeline.seconds[static_cast<std::size_t>(end - 1)] -
                        timeline.seconds[static_cast<std::size_t>(start)]);

  result.gps_quality.selected_sample_count = end - start;
  result.gps_quality.valid_sample_count = static_cast<int>(std::count_if(
      samples.begin() + start, samples.begin() + end, valid_coordinate));
  result.gps_quality.coverage_ratio =
      result.gps_quality.selected_sample_count > 0
          ? static_cast<double>(result.gps_quality.valid_sample_count) /
                static_cast<double>(result.gps_quality.selected_sample_count)
          : 0.0;
  result.gps_quality.sufficient_for_speed =
      result.gps_quality.selected_sample_count >= 5 &&
      result.gps_quality.valid_sample_count >= 5 &&
      result.gps_quality.coverage_ratio >= 0.6;

  result.speed_mps =
      calculate_speeds(samples, timeline, start, end, result.speed_source);
  result.max_speed_mps = supported_peak(result.speed_mps, start, end);
  if (!result.max_speed_mps) {
    result.speed_source = SpeedSource::unavailable;
    result.warnings.push_back(
        {AnalysisWarning::Code::speed_unavailable,
         "Top speed was omitted because consistent samples were unavailable."});
  } else if (result.speed_source == SpeedSource::vertical) {
    result.warnings.push_back(
        {AnalysisWarning::Code::gps_speed_unavailable,
         "GPS coverage was insufficient; speed uses vertical motion."});
  }
  return result;
}

} // namespace cosmo::video
