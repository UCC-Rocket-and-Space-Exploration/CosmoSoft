#include "gui/VideoExportDialog.h"

#include "gui/SettingsKeys.h"
#include "gui/Theme.h"
#include "gui/ThemeManager.h"
#include "services/video/MapTilerTerrainProvider.h"
#include "services/video/VideoExportService.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

template <typename T, typename... Args>
[[nodiscard]] T *make_qt_owned(Args &&...args) {
  return std::make_unique<T>(std::forward<Args>(args)...).release();
}

[[nodiscard]] bool valid_coordinate(const FlightSample &sample) {
  const double lat = sample.coordinates.latitude;
  const double lon = sample.coordinates.longitude;
  return std::isfinite(lat) && std::isfinite(lon) && std::abs(lat) <= 90.0 &&
         std::abs(lon) <= 180.0 &&
         !(std::abs(lat) < 1e-9 && std::abs(lon) < 1e-9);
}

[[nodiscard]] QString format_time(double seconds) {
  const int value = std::max(0, static_cast<int>(std::lround(seconds)));
  return QStringLiteral("%1:%2")
      .arg(value / 60)
      .arg(value % 60, 2, 10, QLatin1Char('0'));
}

} // namespace

VideoExportDialog::VideoExportDialog(
    std::shared_ptr<const FlightSession> session, QWidget *parent)
    : QDialog(parent), m_session(std::move(session)),
      m_export_service(std::make_unique<cosmo::video::VideoExportService>()),
      m_terrain_provider(
          std::make_unique<cosmo::video::MapTilerTerrainProvider>()) {
  setWindowTitle(u"Save replay video"_s);
  setModal(true);
  resize(900, 760);
  build_ui();
  load_settings();

  if (m_session && !m_session->samples.empty()) {
    m_analysis = cosmo::video::FlightAnalyzer::analyze(*m_session);
    m_start_slider->setRange(
        0, std::max(0, static_cast<int>(m_session->samples.size()) - 2));
    m_end_slider->setRange(1, static_cast<int>(m_session->samples.size()));
    m_start_slider->setValue(m_analysis.start_index);
    m_end_slider->setValue(m_analysis.end_index);
    refresh_analysis();
  }

  connect(&cosmo::ThemeManager::instance(), &cosmo::ThemeManager::themeChanged,
          this, &VideoExportDialog::refresh_style_sheet);
  connect(m_terrain_provider.get(), &cosmo::video::ITerrainProvider::ready,
          this, &VideoExportDialog::on_terrain_ready);
  connect(m_terrain_provider.get(), &cosmo::video::ITerrainProvider::failed,
          this, &VideoExportDialog::on_terrain_failed);
  connect(m_export_service.get(),
          &cosmo::video::VideoExportService::phase_changed, m_phase_label,
          &QLabel::setText);
  connect(m_export_service.get(),
          &cosmo::video::VideoExportService::progress_changed, this,
          [this](int completed, int total) {
            m_progress->setRange(0, std::max(1, total));
            m_progress->setValue(completed);
          });
  connect(m_export_service.get(), &cosmo::video::VideoExportService::finished,
          this, &VideoExportDialog::on_export_finished);
  connect(m_export_service.get(), &cosmo::video::VideoExportService::failed,
          this, &VideoExportDialog::on_export_failed);
  connect(m_export_service.get(), &cosmo::video::VideoExportService::cancelled,
          this, [this]() {
            m_exporting = false;
            set_controls_enabled(true);
            m_phase_label->setText(
                u"Export cancelled. No partial video was kept."_s);
          });

  refresh_style_sheet();
}

VideoExportDialog::~VideoExportDialog() = default;

void VideoExportDialog::build_ui() {
  auto *root = make_qt_owned<QVBoxLayout>(this);
  root->setContentsMargins(18, 18, 18, 18);
  root->setSpacing(12);

  auto *content = make_qt_owned<QHBoxLayout>();
  content->setSpacing(16);
  auto *options = make_qt_owned<QWidget>(this);
  auto *options_layout = make_qt_owned<QVBoxLayout>(options);
  options_layout->setContentsMargins(0, 0, 0, 0);
  options_layout->setSpacing(12);

  auto *identity_group = make_qt_owned<QGroupBox>(u"Share identity"_s, options);
  auto *identity_form = make_qt_owned<QFormLayout>(identity_group);
  m_title_edit = make_qt_owned<QLineEdit>(identity_group);
  m_title_edit->setMaxLength(60);
  m_title_edit->setPlaceholderText(u"Flight title (optional)"_s);
  m_team_edit = make_qt_owned<QLineEdit>(identity_group);
  m_team_edit->setMaxLength(40);
  m_team_edit->setPlaceholderText(u"Rocket or team (optional)"_s);
  identity_form->addRow(u"Title"_s, m_title_edit);
  identity_form->addRow(u"Rocket / team"_s, m_team_edit);
  options_layout->addWidget(identity_group);

  auto *format_group = make_qt_owned<QGroupBox>(u"Video format"_s, options);
  auto *format_form = make_qt_owned<QFormLayout>(format_group);
  m_orientation_combo = make_qt_owned<QComboBox>(format_group);
  m_orientation_combo->addItem(
      u"Landscape"_s,
      static_cast<int>(cosmo::video::VideoOrientation::landscape));
  m_orientation_combo->addItem(
      u"Portrait"_s,
      static_cast<int>(cosmo::video::VideoOrientation::portrait));
  m_quality_combo = make_qt_owned<QComboBox>(format_group);
  m_quality_combo->addItem(
      u"1080p final"_s,
      static_cast<int>(cosmo::video::VideoQuality::full_hd_1080p));
  m_quality_combo->addItem(
      u"720p draft"_s,
      static_cast<int>(cosmo::video::VideoQuality::draft_720p));
  m_terrain_combo = make_qt_owned<QComboBox>(format_group);
  m_terrain_combo->addItem(
      u"Private offline scene"_s,
      static_cast<int>(cosmo::video::TerrainMode::private_scene));
  m_terrain_combo->addItem(
      u"MapTiler real terrain"_s,
      static_cast<int>(cosmo::video::TerrainMode::maptiler));
  m_maptiler_key_edit = make_qt_owned<QLineEdit>(format_group);
  m_maptiler_key_edit->setEchoMode(QLineEdit::Password);
  m_maptiler_key_edit->setPlaceholderText(u"Required only for real terrain"_s);
  format_form->addRow(u"Layout"_s, m_orientation_combo);
  format_form->addRow(u"Quality"_s, m_quality_combo);
  format_form->addRow(u"Terrain"_s, m_terrain_combo);
  format_form->addRow(u"MapTiler key"_s, m_maptiler_key_edit);
  options_layout->addWidget(format_group);

  auto *trim_group = make_qt_owned<QGroupBox>(u"Flight range"_s, options);
  auto *trim_layout = make_qt_owned<QVBoxLayout>(trim_group);
  auto *start_row = make_qt_owned<QHBoxLayout>();
  start_row->addWidget(make_qt_owned<QLabel>(u"Start"_s, trim_group));
  m_start_slider = make_qt_owned<QSlider>(Qt::Horizontal, trim_group);
  m_start_label = make_qt_owned<QLabel>(trim_group);
  start_row->addWidget(m_start_slider, 1);
  start_row->addWidget(m_start_label);
  trim_layout->addLayout(start_row);
  auto *end_row = make_qt_owned<QHBoxLayout>();
  end_row->addWidget(make_qt_owned<QLabel>(u"End"_s, trim_group));
  m_end_slider = make_qt_owned<QSlider>(Qt::Horizontal, trim_group);
  m_end_label = make_qt_owned<QLabel>(trim_group);
  end_row->addWidget(m_end_slider, 1);
  end_row->addWidget(m_end_label);
  trim_layout->addLayout(end_row);
  m_reset_trim_button =
      make_qt_owned<QPushButton>(u"Reset automatic trim"_s, trim_group);
  trim_layout->addWidget(m_reset_trim_button, 0, Qt::AlignLeft);
  options_layout->addWidget(trim_group);

  auto *output_group = make_qt_owned<QGroupBox>(u"Output"_s, options);
  auto *output_layout = make_qt_owned<QHBoxLayout>(output_group);
  m_output_edit = make_qt_owned<QLineEdit>(output_group);
  m_browse_button = make_qt_owned<QPushButton>(u"Browse…"_s, output_group);
  output_layout->addWidget(m_output_edit, 1);
  output_layout->addWidget(m_browse_button);
  options_layout->addWidget(output_group);
  options_layout->addStretch(1);
  content->addWidget(options, 3);

  auto *preview_column = make_qt_owned<QWidget>(this);
  auto *preview_layout = make_qt_owned<QVBoxLayout>(preview_column);
  preview_layout->setContentsMargins(0, 0, 0, 0);
  preview_layout->setSpacing(10);
  m_preview_label = make_qt_owned<QLabel>(preview_column);
  m_preview_label->setObjectName(u"videoPreview"_s);
  m_preview_label->setAlignment(Qt::AlignCenter);
  m_preview_label->setMinimumSize(280, 280);
  m_preview_label->setAccessibleName(u"Replay video preview"_s);
  preview_layout->addWidget(m_preview_label, 1);
  m_summary_label = make_qt_owned<QLabel>(preview_column);
  m_summary_label->setWordWrap(true);
  m_warning_label = make_qt_owned<QLabel>(preview_column);
  m_warning_label->setObjectName(u"videoWarning"_s);
  m_warning_label->setWordWrap(true);
  preview_layout->addWidget(m_summary_label);
  preview_layout->addWidget(m_warning_label);
  content->addWidget(preview_column, 2);
  root->addLayout(content, 1);

  m_phase_label = make_qt_owned<QLabel>(u"Ready to export."_s, this);
  m_phase_label->setObjectName(u"videoPhase"_s);
  m_progress = make_qt_owned<QProgressBar>(this);
  m_progress->setRange(0, 1);
  m_progress->setValue(0);
  m_progress->setTextVisible(true);
  root->addWidget(m_phase_label);
  root->addWidget(m_progress);

  auto *buttons = make_qt_owned<QHBoxLayout>();
  buttons->addStretch(1);
  m_cancel_button = make_qt_owned<QPushButton>(u"Close"_s, this);
  m_export_button = make_qt_owned<QPushButton>(u"Save replay video"_s, this);
  m_export_button->setDefault(true);
  buttons->addWidget(m_cancel_button);
  buttons->addWidget(m_export_button);
  root->addLayout(buttons);

  connect(m_start_slider, &QSlider::valueChanged, this,
          &VideoExportDialog::refresh_analysis);
  connect(m_end_slider, &QSlider::valueChanged, this,
          &VideoExportDialog::refresh_analysis);
  connect(m_title_edit, &QLineEdit::textChanged, this,
          &VideoExportDialog::refresh_preview);
  connect(m_orientation_combo,
          QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &VideoExportDialog::refresh_preview);
  connect(m_quality_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &VideoExportDialog::refresh_preview);
  connect(m_terrain_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this]() {
            m_maptiler_key_edit->setEnabled(
                selected_terrain_mode() == cosmo::video::TerrainMode::maptiler);
            refresh_preview();
          });
  connect(m_browse_button, &QPushButton::clicked, this,
          &VideoExportDialog::browse_output);
  connect(m_reset_trim_button, &QPushButton::clicked, this,
          &VideoExportDialog::reset_automatic_trim);
  connect(m_export_button, &QPushButton::clicked, this,
          &VideoExportDialog::start_export);
  connect(m_cancel_button, &QPushButton::clicked, this,
          &VideoExportDialog::cancel_or_close);
}

void VideoExportDialog::refresh_style_sheet() {
  setStyleSheet(QString(uR"(
        QDialog { background-color: %1; color: %2; }
        QGroupBox {
            background-color: %3;
            border: 1px solid %4;
            border-radius: %5px;
            margin-top: 10px;
            padding-top: 8px;
            font-family: %6;
            font-weight: 600;
        }
        QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }
        QLabel { color: %2; font-family: %6; }
        QLabel#videoWarning { color: %7; }
        QLabel#videoPhase { color: %8; }
        QLabel#videoPreview {
            background-color: %9;
            border: 1px solid %4;
            border-radius: %5px;
        }
        QLineEdit, QComboBox {
            background-color: %9;
            color: %2;
            border: 1px solid %10;
            border-radius: %11px;
            padding: 6px;
            font-family: %6;
        }
        QPushButton {
            background-color: %12;
            color: %2;
            border: 1px solid %10;
            border-radius: %11px;
            padding: 7px 12px;
            font-family: %6;
        }
        QPushButton:hover { background-color: %13; border-color: %14; }
        QPushButton:pressed { background-color: %15; }
        QPushButton:disabled { color: %16; border-color: %4; }
        QProgressBar {
            background-color: %9;
            color: %2;
            border: 1px solid %10;
            border-radius: %11px;
            text-align: center;
        }
        QProgressBar::chunk { background-color: %14; border-radius: %11px; }
    )"_s)
                    .arg(Theme::kBgBase())
                    .arg(Theme::kTextPrimary())
                    .arg(Theme::kBgPanel())
                    .arg(Theme::kBorderPanel())
                    .arg(Theme::kRadiusMd)
                    .arg(Theme::kFontMono)
                    .arg(Theme::kWarning())
                    .arg(Theme::kTextMid())
                    .arg(Theme::kBgInput())
                    .arg(Theme::kBorderDefault())
                    .arg(Theme::kRadiusSm)
                    .arg(Theme::kBgButton())
                    .arg(Theme::kBtnHover())
                    .arg(Theme::kAccentLink())
                    .arg(Theme::kBtnPressed())
                    .arg(Theme::kTextDim()));
  refresh_preview();
}

void VideoExportDialog::load_settings() {
  QSettings settings(kSettingsOrg, kSettingsApp);
  const QString orientation =
      settings.value(kSettingsVideoOrientation, u"landscape"_s).toString();
  m_orientation_combo->setCurrentIndex(orientation == u"portrait"_s ? 1 : 0);
  const QString quality =
      settings.value(kSettingsVideoQuality, u"full_hd"_s).toString();
  m_quality_combo->setCurrentIndex(quality == u"draft"_s ? 1 : 0);
  const QString terrain =
      settings.value(kSettingsVideoTerrain, u"private"_s).toString();
  m_terrain_combo->setCurrentIndex(terrain == u"maptiler"_s ? 1 : 0);
  m_maptiler_key_edit->setText(
      settings.value(kSettingsMapTilerApiKey).toString());
  m_maptiler_key_edit->setEnabled(selected_terrain_mode() ==
                                  cosmo::video::TerrainMode::maptiler);
  const QString directory =
      settings.value(kSettingsVideoExportDir, QDir::homePath()).toString();
  m_output_edit->setText(QDir(directory).filePath(u"cosmosoft-flight.mp4"_s));
}

void VideoExportDialog::save_settings() const {
  QSettings settings(kSettingsOrg, kSettingsApp);
  settings.setValue(kSettingsVideoOrientation,
                    selected_preset().orientation ==
                            cosmo::video::VideoOrientation::portrait
                        ? u"portrait"_s
                        : u"landscape"_s);
  settings.setValue(kSettingsVideoQuality,
                    selected_preset().quality ==
                            cosmo::video::VideoQuality::draft_720p
                        ? u"draft"_s
                        : u"full_hd"_s);
  settings.setValue(kSettingsVideoTerrain,
                    selected_terrain_mode() ==
                            cosmo::video::TerrainMode::maptiler
                        ? u"maptiler"_s
                        : u"private"_s);
  settings.setValue(kSettingsMapTilerApiKey,
                    m_maptiler_key_edit->text().trimmed());
  settings.setValue(kSettingsVideoExportDir,
                    QFileInfo(m_output_edit->text()).absolutePath());
}

void VideoExportDialog::refresh_analysis() {
  if (!m_session || m_session->samples.size() < 2U) {
    return;
  }
  if (m_start_slider->value() >= m_end_slider->value()) {
    if (sender() == m_start_slider) {
      m_end_slider->setValue(m_start_slider->value() + 1);
    } else {
      m_start_slider->setValue(m_end_slider->value() - 1);
    }
  }
  m_analysis = cosmo::video::FlightAnalyzer::analyze(
      *m_session, cosmo::video::FlightTrimRange{m_start_slider->value(),
                                                m_end_slider->value()});
  refresh_trim_labels();
  m_summary_label->setText(
      QStringLiteral(
          "Peak height %1 m  ·  Max altitude %2 m  ·  %3  ·  Flight time %4")
          .arg(m_analysis.peak_height_m, 0, 'f', 1)
          .arg(m_analysis.max_recorded_altitude_m, 0, 'f', 1)
          .arg(m_analysis.max_speed_mps
                   ? QStringLiteral("Top speed %1 m/s")
                         .arg(*m_analysis.max_speed_mps, 0, 'f', 1)
                   : u"Top speed omitted"_s)
          .arg(format_time(m_analysis.duration_seconds)));
  QStringList warnings;
  for (const auto &warning : m_analysis.warnings) {
    warnings.append(QString::fromStdString(warning.message));
  }
  m_warning_label->setText(warnings.join(u"\n"_s));
  refresh_preview();
}

void VideoExportDialog::refresh_trim_labels() {
  if (m_analysis.display_seconds.empty()) {
    return;
  }
  const int start =
      std::clamp(m_start_slider->value(), 0,
                 static_cast<int>(m_analysis.display_seconds.size()) - 1);
  const int end =
      std::clamp(m_end_slider->value() - 1, 0,
                 static_cast<int>(m_analysis.display_seconds.size()) - 1);
  m_start_label->setText(
      format_time(m_analysis.display_seconds[static_cast<std::size_t>(start)]));
  m_end_label->setText(
      format_time(m_analysis.display_seconds[static_cast<std::size_t>(end)]));
}

void VideoExportDialog::refresh_preview() {
  if (!m_preview_label || !m_session || m_session->samples.empty()) {
    return;
  }
  const bool portrait =
      selected_preset().orientation == cosmo::video::VideoOrientation::portrait;
  const QSize canvas_size = portrait ? QSize(225, 400) : QSize(400, 225);
  QPixmap canvas(canvas_size);
  canvas.fill(QColor(Theme::kBgDark()));
  QPainter painter(&canvas);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.fillRect(canvas.rect(), QColor(Theme::kBgDark()));
  painter.setPen(QPen(QColor(Theme::kBorderSubtle()), 1));
  for (int x = 0; x < canvas.width(); x += 32)
    painter.drawLine(x, 0, x, canvas.height());
  for (int y = 0; y < canvas.height(); y += 32)
    painter.drawLine(0, y, canvas.width(), y);

  if (m_analysis.end_index - m_analysis.start_index > 1) {
    QPainterPath path;
    const double maximum = std::max(1.0, m_analysis.peak_height_m);
    const QRectF plot(20, 42, canvas.width() - 40, canvas.height() - 88);
    for (int i = m_analysis.start_index; i < m_analysis.end_index; ++i) {
      const double progress = static_cast<double>(i - m_analysis.start_index) /
                              static_cast<double>(m_analysis.end_index -
                                                  m_analysis.start_index - 1);
      const double height = std::max(
          0.0, m_session->samples[static_cast<std::size_t>(i)].altitude -
                   m_analysis.baseline_altitude_m);
      const QPointF point(plot.left() + progress * plot.width(),
                          plot.bottom() - height / maximum * plot.height());
      if (i == m_analysis.start_index)
        path.moveTo(point);
      else
        path.lineTo(point);
    }
    painter.setPen(
        QPen(QColor(Theme::kAccentLink()), 4, Qt::SolidLine, Qt::RoundCap));
    painter.drawPath(path);
  }
  painter.setPen(QColor(Theme::kTextPrimary()));
  QFont title_font(QString::fromUtf8(Theme::kFontMono));
  title_font.setBold(true);
  title_font.setPixelSize(portrait ? 16 : 18);
  painter.setFont(title_font);
  painter.drawText(
      QRect(16, 12, canvas.width() - 32, 28), Qt::AlignLeft | Qt::AlignVCenter,
      m_title_edit && !m_title_edit->text().isEmpty() ? m_title_edit->text()
                                                      : u"FLIGHT REPLAY"_s);
  painter.setPen(QColor(Theme::kTextMuted()));
  title_font.setPixelSize(10);
  painter.setFont(title_font);
  painter.drawText(QRect(16, canvas.height() - 34, canvas.width() - 32, 22),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   selected_terrain_mode() ==
                           cosmo::video::TerrainMode::maptiler
                       ? u"REAL TERRAIN · PREFLIGHT REQUIRED"_s
                       : u"PRIVATE OFFLINE SCENE"_s);
  painter.setPen(QColor(Theme::kTextMid()));
  painter.drawText(QRect(16, 12, canvas.width() - 32, 22),
                   Qt::AlignRight | Qt::AlignVCenter, u"COSMOSOFT"_s);
  m_preview_label->setPixmap(canvas);
}

void VideoExportDialog::browse_output() {
  QString path = QFileDialog::getSaveFileName(this, u"Save replay video"_s,
                                              m_output_edit->text(),
                                              u"MP4 video (*.mp4)"_s);
  if (path.isEmpty())
    return;
  if (!path.endsWith(u".mp4"_s, Qt::CaseInsensitive))
    path += u".mp4"_s;
  m_output_edit->setText(path);
}

void VideoExportDialog::reset_automatic_trim() {
  if (!m_session)
    return;
  const auto automatic = cosmo::video::FlightAnalyzer::analyze(*m_session);
  m_start_slider->setValue(automatic.start_index);
  m_end_slider->setValue(automatic.end_index);
  refresh_analysis();
}

void VideoExportDialog::start_export() {
  if (m_preflighting || m_exporting || !m_session)
    return;
  if (!cosmo::video::VideoExportService::h264_mp4_available()) {
    QMessageBox::critical(
        this, u"Video encoder unavailable"_s,
        u"Replay video export requires Qt's FFmpeg multimedia backend with H.264 MP4 support. Install the required backend and restart CosmoSoft."_s);
    return;
  }
  QString path = m_output_edit->text().trimmed();
  if (!path.endsWith(u".mp4"_s, Qt::CaseInsensitive)) {
    path += u".mp4"_s;
    m_output_edit->setText(path);
  }
  m_overwrite_existing = QFileInfo::exists(path);
  if (m_overwrite_existing) {
    const auto answer = QMessageBox::question(
        this, u"Replace video?"_s,
        u"The selected video already exists. Replace it?"_s,
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
      return;
  }
  save_settings();
  set_controls_enabled(false);
  if (selected_terrain_mode() == cosmo::video::TerrainMode::maptiler) {
    const auto bounds = selected_bounds();
    if (!bounds) {
      set_controls_enabled(true);
      QMessageBox::warning(
          this, u"Real terrain unavailable"_s,
          u"This flight does not contain enough valid GPS coordinates. Use the private scene."_s);
      return;
    }
    m_preflighting = true;
    m_phase_label->setText(u"Downloading and validating real terrain…"_s);
    m_terrain_provider->request_terrain(*bounds, selected_preset().resolution,
                                        m_maptiler_key_edit->text());
    return;
  }
  begin_encoding();
}

void VideoExportDialog::cancel_or_close() {
  if (m_preflighting) {
    m_terrain_provider->cancel();
    m_preflighting = false;
    set_controls_enabled(true);
    m_phase_label->setText(u"Terrain preflight cancelled."_s);
  } else if (m_exporting) {
    m_export_service->cancel();
  } else {
    reject();
  }
}

void VideoExportDialog::on_terrain_ready(
    const cosmo::video::TerrainPackage &terrain) {
  if (!m_preflighting)
    return;
  m_preflighting = false;
  begin_encoding(terrain);
}

void VideoExportDialog::on_terrain_failed(const QString &message) {
  if (!m_preflighting)
    return;
  m_preflighting = false;
  QMessageBox box(QMessageBox::Warning, u"Real terrain unavailable"_s, message,
                  QMessageBox::NoButton, this);
  auto *retry = box.addButton(u"Retry"_s, QMessageBox::AcceptRole);
  auto *use_private =
      box.addButton(u"Use private scene"_s, QMessageBox::ActionRole);
  box.addButton(QMessageBox::Cancel);
  box.exec();
  if (box.clickedButton() == retry) {
    m_preflighting = true;
    m_terrain_provider->request_terrain(*selected_bounds(),
                                        selected_preset().resolution,
                                        m_maptiler_key_edit->text());
  } else if (box.clickedButton() == use_private) {
    m_terrain_combo->setCurrentIndex(0);
    begin_encoding();
  } else {
    set_controls_enabled(true);
    m_phase_label->setText(u"Export not started."_s);
  }
}

void VideoExportDialog::begin_encoding(
    const cosmo::video::TerrainPackage &terrain) {
  cosmo::video::VideoExportRequest request;
  request.session = m_session;
  request.analysis = m_analysis;
  request.preset = selected_preset();
  request.terrain_mode = selected_terrain_mode();
  request.terrain = terrain;
  request.output_path = m_output_edit->text();
  request.overwrite_existing = m_overwrite_existing;
  request.title = m_title_edit->text().trimmed();
  request.rocket_or_team = m_team_edit->text().trimmed();
  m_exporting = true;
  m_cancel_button->setText(u"Cancel export"_s);
  m_export_service->start(request);
}

void VideoExportDialog::on_export_finished(const QString &path) {
  m_exporting = false;
  save_settings();
  QMessageBox::information(
      this, u"Replay video saved"_s,
      QStringLiteral("The replay video was saved to:\n%1").arg(path));
  accept();
}

void VideoExportDialog::on_export_failed(const QString &message) {
  m_exporting = false;
  set_controls_enabled(true);
  m_phase_label->setText(u"Export failed. No partial video was kept."_s);
  QMessageBox::critical(this, u"Replay video export failed"_s, message);
}

void VideoExportDialog::set_controls_enabled(bool enabled) {
  const std::array<QWidget *, 13> controls{
      m_title_edit,    m_team_edit,     m_orientation_combo,
      m_quality_combo, m_terrain_combo, m_maptiler_key_edit,
      m_start_slider,  m_end_slider,    m_reset_trim_button,
      m_output_edit,   m_browse_button, m_export_button,
      m_cancel_button};
  for (QWidget *widget : controls) {
    widget->setEnabled(enabled);
  }
  m_cancel_button->setEnabled(true);
  m_cancel_button->setText(enabled ? u"Close"_s : u"Cancel"_s);
}

cosmo::video::VideoExportPreset VideoExportDialog::selected_preset() const {
  return cosmo::video::VideoExportPreset::make(
      static_cast<cosmo::video::VideoOrientation>(
          m_orientation_combo->currentData().toInt()),
      static_cast<cosmo::video::VideoQuality>(
          m_quality_combo->currentData().toInt()));
}

cosmo::video::TerrainMode VideoExportDialog::selected_terrain_mode() const {
  return static_cast<cosmo::video::TerrainMode>(
      m_terrain_combo->currentData().toInt());
}

std::optional<cosmo::video::TerrainBounds>
VideoExportDialog::selected_bounds() const {
  if (!m_session)
    return std::nullopt;
  cosmo::video::TerrainBounds bounds{90.0, 180.0, -90.0, -180.0};
  int count = 0;
  for (int i = m_analysis.start_index; i < m_analysis.end_index; ++i) {
    const auto &sample = m_session->samples[static_cast<std::size_t>(i)];
    if (!valid_coordinate(sample))
      continue;
    bounds.south = std::min(bounds.south, sample.coordinates.latitude);
    bounds.north = std::max(bounds.north, sample.coordinates.latitude);
    bounds.west = std::min(bounds.west, sample.coordinates.longitude);
    bounds.east = std::max(bounds.east, sample.coordinates.longitude);
    ++count;
  }
  if (count < 2 || bounds.east - bounds.west > 180.0)
    return std::nullopt;
  const double latitude_padding =
      std::max(0.0005, (bounds.north - bounds.south) * 0.2);
  const double longitude_padding =
      std::max(0.0005, (bounds.east - bounds.west) * 0.2);
  bounds.south -= latitude_padding;
  bounds.north += latitude_padding;
  bounds.west -= longitude_padding;
  bounds.east += longitude_padding;
  return bounds;
}

void VideoExportDialog::closeEvent(QCloseEvent *event) {
  if (!m_preflighting && !m_exporting) {
    QDialog::closeEvent(event);
    return;
  }
  const auto answer = QMessageBox::question(
      this, u"Cancel video export?"_s,
      u"Closing this window will cancel the export and remove the partial file."_s,
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  if (answer == QMessageBox::Yes) {
    cancel_or_close();
    event->accept();
  } else {
    event->ignore();
  }
}
