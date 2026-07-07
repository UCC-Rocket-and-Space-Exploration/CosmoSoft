#include "services/video/VideoTimeline.h"

#include <algorithm>
#include <cmath>

namespace cosmo::video {

VideoTimeline::VideoTimeline(const FlightAnalysis &analysis,
                             int frames_per_second)
    : m_frames_per_second(std::max(1, frames_per_second)) {
  if (!analysis.display_seconds.empty() &&
      analysis.end_index > analysis.start_index) {
    const int last =
        std::clamp(analysis.end_index - 1, 0,
                   static_cast<int>(analysis.display_seconds.size()) - 1);
    const int start =
        std::clamp(analysis.start_index, 0,
                   static_cast<int>(analysis.display_seconds.size()) - 1);
    const int apogee = std::clamp(analysis.apogee_index, start, last);
    m_source_start = analysis.display_seconds[static_cast<std::size_t>(start)];
    m_source_apogee =
        analysis.display_seconds[static_cast<std::size_t>(apogee)];
    m_source_end = analysis.display_seconds[static_cast<std::size_t>(last)];
  }
  m_replay_seconds = std::clamp(analysis.duration_seconds * 0.15, 13.0, 38.0);
  m_total_seconds = 2.0 + m_replay_seconds + 5.0;
  m_apogee_hold_seconds = std::min(0.75, m_replay_seconds * 0.05);
  m_frame_count = static_cast<int>(
      std::llround(m_total_seconds * static_cast<double>(m_frames_per_second)));
}

double VideoTimeline::source_seconds_at(double video_seconds) const {
  if (video_seconds <= 2.0) {
    return m_source_start;
  }
  if (video_seconds >= 2.0 + m_replay_seconds) {
    return m_source_end;
  }
  const double local = video_seconds - 2.0;
  const double moving =
      std::max(0.001, m_replay_seconds - m_apogee_hold_seconds);
  const double ascent = moving * 0.45;
  const double hold_end = ascent + m_apogee_hold_seconds;
  if (local < ascent) {
    return m_source_start +
           (m_source_apogee - m_source_start) * (local / ascent);
  }
  if (local <= hold_end) {
    return m_source_apogee;
  }
  const double descent = std::max(0.001, m_replay_seconds - hold_end);
  return m_source_apogee +
         (m_source_end - m_source_apogee) * ((local - hold_end) / descent);
}

long long VideoTimeline::frame_start_microseconds(int frame_index) const {
  const int safe_index = std::clamp(frame_index, 0, m_frame_count);
  return (static_cast<long long>(safe_index) * 1000000LL) /
         static_cast<long long>(m_frames_per_second);
}

} // namespace cosmo::video
