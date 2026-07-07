/**
 * @file VideoTimeline.h
 * @brief Exact frame and flight-time mapping for short-form replay videos.
 */

#ifndef COSMO_SOFT_VIDEOTIMELINE_H
#define COSMO_SOFT_VIDEOTIMELINE_H

#include "services/video/FlightAnalyzer.h"

namespace cosmo::video {

/** @brief Maps fixed-rate video time to corrected flight time. */
class VideoTimeline {
public:
  /** @brief Construct the automatic 20-45 second edit for @p analysis. */
  explicit VideoTimeline(const FlightAnalysis &analysis,
                         int frames_per_second = 30);

  /** @brief Total encoded duration in seconds. */
  [[nodiscard]] double total_seconds() const { return m_total_seconds; }

  /** @brief Number of encoded frames. */
  [[nodiscard]] int frame_count() const { return m_frame_count; }

  /** @brief Fixed title-section duration. */
  [[nodiscard]] double intro_seconds() const { return 2.0; }

  /** @brief Duration assigned to animated flight playback. */
  [[nodiscard]] double replay_seconds() const { return m_replay_seconds; }

  /** @brief Fixed summary-section duration. */
  [[nodiscard]] double summary_seconds() const { return 5.0; }

  /** @brief Return source corrected seconds for an encoded video timestamp. */
  [[nodiscard]] double source_seconds_at(double video_seconds) const;

  /** @brief Exact microsecond timestamp for the start of @p frame_index. */
  [[nodiscard]] long long frame_start_microseconds(int frame_index) const;

private:
  double m_source_start = 0.0;
  double m_source_apogee = 0.0;
  double m_source_end = 0.0;
  double m_replay_seconds = 13.0;
  double m_total_seconds = 20.0;
  double m_apogee_hold_seconds = 0.75;
  int m_frames_per_second = 30;
  int m_frame_count = 600;
};

} // namespace cosmo::video

#endif // COSMO_SOFT_VIDEOTIMELINE_H
