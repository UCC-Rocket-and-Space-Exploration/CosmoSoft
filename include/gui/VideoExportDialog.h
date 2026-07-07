/**
 * @file VideoExportDialog.h
 * @brief Theme-aware setup, terrain preflight, and progress UI for replay-video
 * export.
 */

#ifndef COSMO_SOFT_VIDEOEXPORTDIALOG_H
#define COSMO_SOFT_VIDEOEXPORTDIALOG_H

#include "services/video/FlightAnalyzer.h"
#include "services/video/VideoExportTypes.h"

#include <QDialog>

#include <memory>

class QCloseEvent;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSlider;

namespace cosmo::video {
class MapTilerTerrainProvider;
class VideoExportService;
} // namespace cosmo::video

/** @brief Configures and runs one shareable replay-video export. */
class VideoExportDialog final : public QDialog {
  Q_OBJECT

public:
  /**
   * @brief Construct an export dialog for an immutable completed flight.
   * @param session Completed imported or recorded session snapshot.
   * @param parent Optional parent window.
   */
  explicit VideoExportDialog(std::shared_ptr<const FlightSession> session,
                             QWidget *parent = nullptr);
  /** @brief Destroy the dialog and cancel any remaining export resources. */
  ~VideoExportDialog() override;

protected:
  /** @brief Prevent accidental close while an export is active. */
  void closeEvent(QCloseEvent *event) override;

private slots:
  void refresh_style_sheet();
  void refresh_analysis();
  void browse_output();
  void reset_automatic_trim();
  void start_export();
  void cancel_or_close();
  void on_terrain_ready(const cosmo::video::TerrainPackage &terrain);
  void on_terrain_failed(const QString &message);
  void on_export_finished(const QString &path);
  void on_export_failed(const QString &message);

private:
  void build_ui();
  void load_settings();
  void save_settings() const;
  void refresh_preview();
  void refresh_trim_labels();
  void begin_encoding(const cosmo::video::TerrainPackage &terrain = {});
  void set_controls_enabled(bool enabled);
  [[nodiscard]] cosmo::video::VideoExportPreset selected_preset() const;
  [[nodiscard]] cosmo::video::TerrainMode selected_terrain_mode() const;
  [[nodiscard]] std::optional<cosmo::video::TerrainBounds>
  selected_bounds() const;

  std::shared_ptr<const FlightSession> m_session;
  cosmo::video::FlightAnalysis m_analysis;
  std::unique_ptr<cosmo::video::VideoExportService> m_export_service;
  std::unique_ptr<cosmo::video::MapTilerTerrainProvider> m_terrain_provider;

  QLineEdit *m_title_edit = nullptr;
  QLineEdit *m_team_edit = nullptr;
  QComboBox *m_orientation_combo = nullptr;
  QComboBox *m_quality_combo = nullptr;
  QComboBox *m_terrain_combo = nullptr;
  QLineEdit *m_maptiler_key_edit = nullptr;
  QSlider *m_start_slider = nullptr;
  QSlider *m_end_slider = nullptr;
  QLabel *m_start_label = nullptr;
  QLabel *m_end_label = nullptr;
  QLabel *m_summary_label = nullptr;
  QLabel *m_warning_label = nullptr;
  QLabel *m_preview_label = nullptr;
  QLineEdit *m_output_edit = nullptr;
  QPushButton *m_browse_button = nullptr;
  QPushButton *m_reset_trim_button = nullptr;
  QPushButton *m_export_button = nullptr;
  QPushButton *m_cancel_button = nullptr;
  QProgressBar *m_progress = nullptr;
  QLabel *m_phase_label = nullptr;

  bool m_preflighting = false;
  bool m_exporting = false;
  bool m_overwrite_existing = false;
};

#endif // COSMO_SOFT_VIDEOEXPORTDIALOG_H
