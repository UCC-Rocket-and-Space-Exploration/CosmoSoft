/**
 * @file VideoExportService.h
 * @brief Bounded deterministic Qt Multimedia replay-video exporter.
 */

#ifndef COSMO_SOFT_VIDEOEXPORTSERVICE_H
#define COSMO_SOFT_VIDEOEXPORTSERVICE_H

#include "services/video/VideoExportTypes.h"

#include <QObject>
#include <QVariantList>
#include <QVector3D>
#include <QVideoFrame>

#include <memory>
#include <vector>

class QMediaCaptureSession;
class QMediaRecorder;
class QQuickView;
class QTemporaryDir;
class QVideoFrameInput;

namespace cosmo::video {

class VideoTimeline;

/** @brief Renders and encodes one replay video at a time with bounded memory.
 */
class VideoExportService final : public QObject {
  Q_OBJECT

public:
  /** @brief Construct an idle exporter. */
  explicit VideoExportService(QObject *parent = nullptr);
  /** @brief Stop any active encoder and remove its temporary output. */
  ~VideoExportService() override;

  /** @brief Return true while preflight, rendering, encoding, or finalization
   * is active. */
  [[nodiscard]] bool is_busy() const { return m_busy; }

  /** @brief Return true when this Qt runtime can encode H.264 in an MPEG-4
   * container. */
  [[nodiscard]] static bool h264_mp4_available();

public slots:
  /** @brief Start an immutable export request, or emit failed() if preflight
   * rejects it. */
  void start(const cosmo::video::VideoExportRequest &request);

  /** @brief Cancel at the next safe frame boundary and remove partial output.
   */
  void cancel();

signals:
  /** @brief Describes the current preflight, rendering, encoding, or
   * finalization phase. */
  void phase_changed(const QString &phase);

  /** @brief Reports completed frames and total frames. */
  void progress_changed(int completed_frames, int total_frames);

  /** @brief Emitted after atomic output finalization. */
  void finished(const QString &output_path);

  /** @brief Emitted after cancellation and partial-file cleanup. */
  void cancelled();

  /** @brief Emitted for a terminal failure after partial-file cleanup. */
  void failed(const QString &message);

private:
  bool prepare_scene(QString &error);
  bool prepare_output(QString &error);
  void render_next_frame();
  void submit_pending_frame();
  void update_scene_properties(double video_seconds);
  void finish_stream();
  void on_recorder_state_changed();
  void finalize_output();
  void fail_export(const QString &message);
  void reset_export_objects();
  void remove_temporary_output();
  void build_scene_path();
  [[nodiscard]] TerrainPackage effective_terrain() const;

  VideoExportRequest m_request;
  std::unique_ptr<VideoTimeline> m_timeline;
  std::unique_ptr<QQuickView> m_view;
  std::unique_ptr<QTemporaryDir> m_asset_directory;
  std::unique_ptr<QMediaCaptureSession> m_capture_session;
  std::unique_ptr<QMediaRecorder> m_recorder;
  std::unique_ptr<QVideoFrameInput> m_video_input;
  QVideoFrame m_pending_frame;

  std::vector<QVector3D> m_sample_positions;
  std::vector<int> m_path_sample_indices;
  QVariantList m_scene_path;
  QVariantList m_altitude_profile;
  bool m_profile_mode = false;
  double m_scene_terrain_width = 1200.0;
  double m_scene_terrain_depth = 1200.0;
  double m_scene_terrain_relief = 45.0;
  QString m_temporary_output_path;
  int m_frame_index = 0;
  bool m_busy = false;
  bool m_cancel_requested = false;
  bool m_end_frame_pending = false;
  bool m_end_frame_sent = false;
};

} // namespace cosmo::video

#endif // COSMO_SOFT_VIDEOEXPORTSERVICE_H
