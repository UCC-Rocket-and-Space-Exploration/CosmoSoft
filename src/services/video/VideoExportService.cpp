#include "services/video/VideoExportService.h"

#include "services/video/VideoTimeline.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLinearGradient>
#include <QMediaCaptureSession>
#include <QMediaFormat>
#include <QMediaPlayer>
#include <QMediaRecorder>
#include <QPainter>
#include <QQuickItem>
#include <QQuickView>
#include <QSaveFile>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVariantList>
#include <QVariantMap>
#include <QVideoFrameFormat>
#include <QVideoFrameInput>
#include <QVideoSink>

#include <algorithm>
#include <cmath>
#include <limits>

void initialize_video_export_resources() { Q_INIT_RESOURCE(video_export); }

namespace {

constexpr double kEarthRadiusM = 6371000.0;
constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;

[[nodiscard]] bool valid_coordinate(const FlightSample &sample) {
  const double lat = sample.coordinates.latitude;
  const double lon = sample.coordinates.longitude;
  return std::isfinite(lat) && std::isfinite(lon) && std::abs(lat) <= 90.0 &&
         std::abs(lon) <= 180.0 &&
         !(std::abs(lat) < 1e-9 && std::abs(lon) < 1e-9);
}

[[nodiscard]] double wrapped_longitude_delta(double longitude,
                                             double reference) {
  return std::remainder(longitude - reference, 360.0);
}

[[nodiscard]] QImage private_texture() {
  QImage image(1024, 1024, QImage::Format_RGB32);
  QPainter painter(&image);
  QLinearGradient gradient(0, 0, 0, image.height());
  gradient.setColorAt(0.0, QColor(25, 43, 65));
  gradient.setColorAt(1.0, QColor(8, 17, 28));
  painter.fillRect(image.rect(), gradient);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(QPen(QColor(79, 125, 158, 55), 2));
  for (int value = 0; value <= image.width(); value += 64) {
    painter.drawLine(value, 0, value, image.height());
    painter.drawLine(0, value, image.width(), value);
  }
  painter.setPen(QPen(QColor(97, 190, 226, 45), 1));
  for (int radius = 100; radius < 900; radius += 110) {
    painter.drawEllipse(image.rect().center(), radius, radius / 2);
  }
  return image;
}

[[nodiscard]] QImage private_height_map() {
  QImage image(128, 128, QImage::Format_Grayscale8);
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const double wave = std::sin(static_cast<double>(x) * 0.09) +
                          std::cos(static_cast<double>(y) * 0.075) +
                          0.5 * std::sin(static_cast<double>(x + y) * 0.045);
      const int gray =
          std::clamp(static_cast<int>(128.0 + wave * 28.0), 0, 255);
      image.setPixelColor(x, y, QColor(gray, gray, gray));
    }
  }
  return image;
}

[[nodiscard]] double finite_speed(const std::vector<double> &speeds,
                                  int index) {
  if (index >= 0 && index < static_cast<int>(speeds.size())) {
    const double value = speeds[static_cast<std::size_t>(index)];
    if (std::isfinite(value)) {
      return value;
    }
  }
  for (int offset = 1; offset < 20; ++offset) {
    for (const int candidate : {index - offset, index + offset}) {
      if (candidate >= 0 && candidate < static_cast<int>(speeds.size())) {
        const double value = speeds[static_cast<std::size_t>(candidate)];
        if (std::isfinite(value)) {
          return value;
        }
      }
    }
  }
  return 0.0;
}

[[nodiscard]] bool validate_encoded_video(const QString &path,
                                          const QSize &expected_size,
                                          double expected_duration_seconds,
                                          QString &error) {
  QMediaPlayer player;
  QVideoSink sink;
  QEventLoop loop;
  QTimer timeout;
  timeout.setSingleShot(true);
  timeout.setInterval(10000);

  bool media_error = false;
  const auto evaluate = [&]() {
    if (sink.videoSize().isValid() && player.duration() > 0) {
      loop.quit();
    }
  };
  QObject::connect(&sink, &QVideoSink::videoSizeChanged, &loop, evaluate);
  QObject::connect(&player, &QMediaPlayer::durationChanged, &loop, evaluate);
  QObject::connect(&player, &QMediaPlayer::errorOccurred, &loop,
                   [&](QMediaPlayer::Error, const QString &) {
                     media_error = true;
                     loop.quit();
                   });
  QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
  player.setVideoOutput(&sink);
  player.setSource(QUrl::fromLocalFile(path));
  player.play();
  timeout.start();
  loop.exec();
  player.stop();

  if (media_error || !sink.videoSize().isValid() || player.duration() <= 0) {
    error =
        QObject::tr("The encoded video could not be reopened for validation.");
    return false;
  }
  if (sink.videoSize() != expected_size) {
    error = QObject::tr(
        "The encoded video dimensions do not match the selected preset.");
    return false;
  }
  const qint64 expected_duration_ms =
      static_cast<qint64>(std::llround(expected_duration_seconds * 1000.0));
  if (std::abs(player.duration() - expected_duration_ms) > 1000) {
    error = QObject::tr("The encoded video duration is incomplete.");
    return false;
  }
  return true;
}

} // namespace

namespace cosmo::video {

VideoExportService::VideoExportService(QObject *parent) : QObject(parent) {
  initialize_video_export_resources();
}

VideoExportService::~VideoExportService() {
  if (m_recorder &&
      m_recorder->recorderState() != QMediaRecorder::StoppedState) {
    m_recorder->stop();
  }
  remove_temporary_output();
}

bool VideoExportService::h264_mp4_available() {
  if (qEnvironmentVariable("QT_MEDIA_BACKEND")
          .compare(QStringLiteral("ffmpeg"), Qt::CaseInsensitive) != 0) {
    return false;
  }
  QMediaFormat format;
  format.setFileFormat(QMediaFormat::MPEG4);
  format.setVideoCodec(QMediaFormat::VideoCodec::H264);
  return format.isSupported(QMediaFormat::Encode);
}

void VideoExportService::start(const VideoExportRequest &request) {
  if (m_busy) {
    emit failed(tr("Another replay video export is already running."));
    return;
  }
  if (!request.session || request.session->samples.empty() ||
      request.analysis.end_index <= request.analysis.start_index) {
    emit failed(
        tr("The selected flight range does not contain enough samples."));
    return;
  }
  if (!h264_mp4_available()) {
    emit failed(tr("This installation does not provide H.264 MP4 encoding "
                   "through the Qt FFmpeg backend."));
    return;
  }

  m_request = request;
  m_busy = true;
  m_cancel_requested = false;
  m_end_frame_pending = false;
  m_end_frame_sent = false;
  m_frame_index = 0;
  m_timeline = std::make_unique<VideoTimeline>(
      m_request.analysis, m_request.preset.frames_per_second);
  emit phase_changed(tr("Preparing cinematic scene…"));

  QString error;
  if (!prepare_output(error) || !prepare_scene(error)) {
    fail_export(error);
    return;
  }

  QVideoFrameFormat frame_format(m_request.preset.resolution,
                                 QVideoFrameFormat::Format_RGBA8888);
  frame_format.setStreamFrameRate(m_request.preset.frames_per_second);
  m_video_input = std::make_unique<QVideoFrameInput>(frame_format);
  m_recorder = std::make_unique<QMediaRecorder>();
  m_capture_session = std::make_unique<QMediaCaptureSession>();

  QMediaFormat media_format;
  media_format.setFileFormat(QMediaFormat::MPEG4);
  media_format.setVideoCodec(QMediaFormat::VideoCodec::H264);
  m_recorder->setMediaFormat(media_format);
  m_recorder->setOutputLocation(QUrl::fromLocalFile(m_temporary_output_path));
  m_recorder->setEncodingMode(QMediaRecorder::AverageBitRateEncoding);
  m_recorder->setVideoBitRate(m_request.preset.video_bit_rate);
  m_recorder->setVideoFrameRate(m_request.preset.frames_per_second);
  m_recorder->setVideoResolution(m_request.preset.resolution);
  m_recorder->setAutoStop(true);

  connect(m_video_input.get(), &QVideoFrameInput::readyToSendVideoFrame, this,
          &VideoExportService::submit_pending_frame);
  connect(m_recorder.get(), &QMediaRecorder::recorderStateChanged, this,
          &VideoExportService::on_recorder_state_changed);
  connect(m_recorder.get(), &QMediaRecorder::errorOccurred, this,
          [this](QMediaRecorder::Error, const QString &message) {
            const QString failure =
                message.isEmpty() ? tr("The video encoder failed.") : message;
            QMetaObject::invokeMethod(
                this,
                [this, failure]() {
                  if (m_busy) {
                    fail_export(failure);
                  }
                },
                Qt::QueuedConnection);
          });

  // Install the generated-frame input before the recorder. Some platform
  // backends snapshot the available capture inputs when the recorder is set.
  m_capture_session->setVideoFrameInput(m_video_input.get());
  m_capture_session->setRecorder(m_recorder.get());

  emit progress_changed(0, m_timeline->frame_count());
  emit phase_changed(tr("Rendering and encoding frames…"));
  m_recorder->record();
}

void VideoExportService::cancel() {
  if (!m_busy) {
    return;
  }
  m_cancel_requested = true;
  emit phase_changed(tr("Cancelling export…"));
  if (m_recorder) {
    m_recorder->stop();
  } else {
    reset_export_objects();
    remove_temporary_output();
    m_busy = false;
    emit cancelled();
  }
}

TerrainPackage VideoExportService::effective_terrain() const {
  if (m_request.terrain_mode == TerrainMode::maptiler &&
      !m_request.terrain.texture.isNull() &&
      !m_request.terrain.height_map.isNull()) {
    return m_request.terrain;
  }
  TerrainPackage terrain;
  terrain.texture = private_texture();
  terrain.height_map = private_height_map();
  terrain.height_span_m = 45.0;
  terrain.real_terrain = false;
  return terrain;
}

bool VideoExportService::prepare_output(QString &error) {
  const QFileInfo output_info(m_request.output_path);
  if (m_request.output_path.isEmpty() || output_info.fileName().isEmpty()) {
    error = tr("Choose a valid MP4 output path.");
    return false;
  }
  if (output_info.exists() && !m_request.overwrite_existing) {
    error = tr("The output file already exists.");
    return false;
  }
  QDir output_directory(output_info.absolutePath());
  if (!output_directory.exists() ||
      !QFileInfo(output_directory.absolutePath()).isWritable()) {
    error = tr("The output directory is not writable.");
    return false;
  }
  const long long estimated_bytes =
      static_cast<long long>(
          static_cast<double>(m_request.preset.video_bit_rate) / 8.0 *
          m_timeline->total_seconds() * 1.25) +
      64LL * 1024LL * 1024LL;
  QStorageInfo storage(output_directory.absolutePath());
  storage.refresh();
  if (storage.isValid() && storage.isReady() &&
      static_cast<long long>(storage.bytesAvailable()) < estimated_bytes) {
    error = tr("There is not enough free space for this video export.");
    return false;
  }
  const QString temporary_name =
      QStringLiteral(".%1.cosmosoft-%2.mp4")
          .arg(output_info.completeBaseName(),
               QUuid::createUuid().toString(QUuid::Id128));
  m_temporary_output_path = output_directory.filePath(temporary_name);
  return true;
}

bool VideoExportService::prepare_scene(QString &error) {
  m_asset_directory = std::make_unique<QTemporaryDir>();
  if (!m_asset_directory->isValid()) {
    error = tr("A temporary terrain workspace could not be created.");
    return false;
  }
  const TerrainPackage terrain = effective_terrain();
  const QString texture_path =
      m_asset_directory->filePath(QStringLiteral("terrain.png"));
  const QString height_path =
      m_asset_directory->filePath(QStringLiteral("height.png"));
  if (!terrain.texture.save(texture_path, "PNG") ||
      !terrain.height_map.save(height_path, "PNG")) {
    error = tr("Terrain assets could not be prepared.");
    return false;
  }

  build_scene_path();
  m_view = std::make_unique<QQuickView>();
  m_view->setResizeMode(QQuickView::SizeRootObjectToView);
  m_view->resize(m_request.preset.resolution);
  m_view->setSource(QUrl(QStringLiteral("qrc:/video/FlightReplayScene.qml")));
  if (m_view->status() != QQuickView::Ready || !m_view->rootObject()) {
    error = tr("The cinematic scene could not be initialized.");
    return false;
  }
  m_view->create();

  QObject *root = m_view->rootObject();
  root->setProperty("flightTitle", m_request.title.left(60));
  root->setProperty("rocketOrTeam", m_request.rocket_or_team.left(40));
  root->setProperty("terrainTextureUrl",
                    QUrl::fromLocalFile(texture_path).toString());
  root->setProperty("heightMapUrl",
                    QUrl::fromLocalFile(height_path).toString());
  root->setProperty("terrainAttribution", terrain.attribution);
  root->setProperty("terrainWidth", m_scene_terrain_width);
  root->setProperty("terrainDepth", m_scene_terrain_depth);
  root->setProperty("terrainRelief", m_scene_terrain_relief);
  root->setProperty("peakHeight", m_request.analysis.peak_height_m);
  root->setProperty("maxAltitude", m_request.analysis.max_recorded_altitude_m);
  root->setProperty("maxSpeed", m_request.analysis.max_speed_mps.value_or(0.0));
  root->setProperty("hasSpeed", m_request.analysis.max_speed_mps.has_value());
  root->setProperty("flightDurationSeconds",
                    m_request.analysis.duration_seconds);
  root->setProperty("pathPoints", m_scene_path);
  root->setProperty("altitudeProfile", m_altitude_profile);
  root->setProperty("profileMode", m_profile_mode);
  return true;
}

void VideoExportService::build_scene_path() {
  const auto &samples = m_request.session->samples;
  const int begin = m_request.analysis.start_index;
  const int end = m_request.analysis.end_index;
  m_sample_positions.assign(samples.size(), QVector3D());
  const int valid_gps = static_cast<int>(std::count_if(
      samples.begin() + begin, samples.begin() + end, valid_coordinate));
  const bool gps_path =
      valid_gps >= 2 &&
      static_cast<double>(valid_gps) / static_cast<double>(end - begin) >= 0.6;

  double reference_lat = 0.0;
  double reference_lon = 0.0;
  if (gps_path) {
    const auto first = std::find_if(samples.begin() + begin,
                                    samples.begin() + end, valid_coordinate);
    reference_lat = first->coordinates.latitude;
    reference_lon = first->coordinates.longitude;
  }
  const double cos_lat = std::cos(reference_lat * kDegreesToRadians);
  double last_x = 0.0;
  double last_z = 0.0;
  bool have_position = false;
  for (int i = begin; i < end; ++i) {
    const auto &sample = samples[static_cast<std::size_t>(i)];
    double x = last_x;
    double z = last_z;
    if (gps_path && valid_coordinate(sample)) {
      x = wrapped_longitude_delta(sample.coordinates.longitude, reference_lon) *
          kDegreesToRadians * kEarthRadiusM * cos_lat;
      z = -(sample.coordinates.latitude - reference_lat) * kDegreesToRadians *
          kEarthRadiusM;
      last_x = x;
      last_z = z;
      have_position = true;
    } else if (!gps_path) {
      const double denominator = std::max(1, end - begin - 1);
      x = -500.0 + 1000.0 * static_cast<double>(i - begin) / denominator;
    } else if (!have_position) {
      x = 0.0;
      z = 0.0;
    }
    const double y =
        std::max(0.0, sample.altitude - m_request.analysis.baseline_altitude_m);
    m_sample_positions[static_cast<std::size_t>(i)] = QVector3D(x, y, z);
  }

  if (gps_path && m_request.terrain_mode == TerrainMode::private_scene) {
    const QVector3D route_start =
        m_sample_positions[static_cast<std::size_t>(begin)];
    const QVector3D route_end =
        m_sample_positions[static_cast<std::size_t>(end - 1)];
    const double delta_x = route_end.x() - route_start.x();
    const double delta_z = route_end.z() - route_start.z();
    if (std::hypot(delta_x, delta_z) > 1e-6) {
      constexpr double target_heading = -0.7853981633974483;
      const double rotation = target_heading - std::atan2(delta_z, delta_x);
      const double cosine = std::cos(rotation);
      const double sine = std::sin(rotation);
      for (int i = begin; i < end; ++i) {
        QVector3D &position = m_sample_positions[static_cast<std::size_t>(i)];
        const double relative_x = position.x() - route_start.x();
        const double relative_z = position.z() - route_start.z();
        position.setX(
            static_cast<float>(relative_x * cosine - relative_z * sine));
        position.setZ(
            static_cast<float>(relative_x * sine + relative_z * cosine));
      }
    }
  }

  double minimum_x = std::numeric_limits<double>::infinity();
  double maximum_x = -std::numeric_limits<double>::infinity();
  double minimum_z = std::numeric_limits<double>::infinity();
  double maximum_z = -std::numeric_limits<double>::infinity();
  for (int i = begin; i < end; ++i) {
    const QVector3D &position = m_sample_positions[static_cast<std::size_t>(i)];
    const double x = position.x();
    const double z = position.z();
    minimum_x = std::min(minimum_x, x);
    maximum_x = std::max(maximum_x, x);
    minimum_z = std::min(minimum_z, z);
    maximum_z = std::max(maximum_z, z);
  }
  double center_x = (minimum_x + maximum_x) * 0.5;
  double center_z = (minimum_z + maximum_z) * 0.5;
  double span = std::max({1.0, maximum_x - minimum_x, maximum_z - minimum_z,
                          m_request.analysis.peak_height_m});
  double scale = 760.0 / span;
  m_scene_terrain_width = 1200.0;
  m_scene_terrain_depth = 1200.0;
  m_scene_terrain_relief = 45.0;

  const TerrainPackage &real_terrain = m_request.terrain;
  const TerrainBounds &terrain_bounds = real_terrain.bounds;
  const bool aligned_real_terrain =
      gps_path && m_request.terrain_mode == TerrainMode::maptiler &&
      real_terrain.real_terrain && terrain_bounds.east > terrain_bounds.west &&
      terrain_bounds.north > terrain_bounds.south;
  if (aligned_real_terrain) {
    const double center_latitude =
        (terrain_bounds.south + terrain_bounds.north) * 0.5;
    const double center_longitude =
        (terrain_bounds.west + terrain_bounds.east) * 0.5;
    center_x = wrapped_longitude_delta(center_longitude, reference_lon) *
               kDegreesToRadians * kEarthRadiusM * cos_lat;
    center_z =
        -(center_latitude - reference_lat) * kDegreesToRadians * kEarthRadiusM;
    const double terrain_width_m =
        (terrain_bounds.east - terrain_bounds.west) * kDegreesToRadians *
        kEarthRadiusM * std::cos(center_latitude * kDegreesToRadians);
    const double terrain_depth_m =
        (terrain_bounds.north - terrain_bounds.south) * kDegreesToRadians *
        kEarthRadiusM;
    span = std::max({1.0, terrain_width_m, terrain_depth_m,
                     m_request.analysis.peak_height_m});
    scale = 1000.0 / span;
    m_scene_terrain_width = std::max(10.0, terrain_width_m * scale);
    m_scene_terrain_depth = std::max(10.0, terrain_depth_m * scale);
    m_scene_terrain_relief =
        std::clamp(real_terrain.height_span_m * scale, 10.0, 500.0);
  }
  for (int i = begin; i < end; ++i) {
    QVector3D &position = m_sample_positions[static_cast<std::size_t>(i)];
    position = QVector3D(static_cast<float>((position.x() - center_x) * scale),
                         static_cast<float>(position.y() * scale),
                         static_cast<float>((position.z() - center_z) * scale));
  }

  const int point_count = std::min(180, end - begin);
  m_scene_path.clear();
  m_altitude_profile.clear();
  m_path_sample_indices.clear();
  for (int point = 0; point < point_count; ++point) {
    const int sample_index =
        point_count == 1 ? begin
                         : begin + static_cast<int>(std::llround(
                                       static_cast<double>(point) *
                                       static_cast<double>(end - begin - 1) /
                                       static_cast<double>(point_count - 1)));
    const QVector3D position =
        m_sample_positions[static_cast<std::size_t>(sample_index)];
    QVariantMap item;
    item.insert(QStringLiteral("x"), position.x());
    item.insert(QStringLiteral("y"), position.y());
    item.insert(QStringLiteral("z"), position.z());
    m_scene_path.append(item);
    m_altitude_profile.append(
        samples[static_cast<std::size_t>(sample_index)].altitude -
        m_request.analysis.baseline_altitude_m);
    m_path_sample_indices.push_back(sample_index);
  }
  m_profile_mode = !gps_path;
}

void VideoExportService::submit_pending_frame() {
  if (!m_busy || !m_video_input || !m_recorder) {
    return;
  }
  if (m_cancel_requested) {
    m_recorder->stop();
    return;
  }
  if (m_pending_frame.isValid()) {
    if (!m_video_input->sendVideoFrame(m_pending_frame)) {
      return;
    }
    m_pending_frame = QVideoFrame();
    ++m_frame_index;
    emit progress_changed(m_frame_index, m_timeline->frame_count());
  }
  if (m_end_frame_pending) {
    if (!m_video_input->sendVideoFrame(QVideoFrame())) {
      return;
    }
    m_end_frame_pending = false;
    m_end_frame_sent = true;
    emit phase_changed(tr("Finalizing MP4…"));
    return;
  }
  render_next_frame();
}

void VideoExportService::render_next_frame() {
  if (!m_busy || m_cancel_requested || m_pending_frame.isValid() ||
      m_end_frame_sent) {
    return;
  }
  if (m_frame_index >= m_timeline->frame_count()) {
    finish_stream();
    return;
  }
  const double video_seconds =
      static_cast<double>(m_frame_index) /
      static_cast<double>(m_request.preset.frames_per_second);
  update_scene_properties(video_seconds);
  QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  if (!m_busy || m_cancel_requested || !m_view) {
    return;
  }
  QImage image = m_view->grabWindow();
  if (image.isNull()) {
    fail_export(tr("A video frame could not be rendered."));
    return;
  }
  if (image.size() != m_request.preset.resolution) {
    image = image.scaled(m_request.preset.resolution, Qt::IgnoreAspectRatio,
                         Qt::SmoothTransformation);
  }
  image = image.convertToFormat(QImage::Format_RGBA8888);
  QVideoFrame frame(image);
  frame.setStartTime(m_timeline->frame_start_microseconds(m_frame_index));
  frame.setEndTime(m_timeline->frame_start_microseconds(m_frame_index + 1));
  if (!m_video_input->sendVideoFrame(frame)) {
    m_pending_frame = std::move(frame);
    return;
  }
  ++m_frame_index;
  emit progress_changed(m_frame_index, m_timeline->frame_count());
  if (m_frame_index >= m_timeline->frame_count()) {
    finish_stream();
  }
}

void VideoExportService::update_scene_properties(double video_seconds) {
  QObject *root = m_view->rootObject();
  if (!root) {
    return;
  }
  const double source_seconds = m_timeline->source_seconds_at(video_seconds);
  const auto &times = m_request.analysis.display_seconds;
  const auto first = times.begin() + m_request.analysis.start_index;
  const auto last = times.begin() + m_request.analysis.end_index;
  const auto upper = std::lower_bound(first, last, source_seconds);
  int high = std::clamp(static_cast<int>(upper - times.begin()),
                        m_request.analysis.start_index,
                        m_request.analysis.end_index - 1);
  int low = std::max(m_request.analysis.start_index, high - 1);
  double fraction = 0.0;
  if (high > low) {
    const double denominator = times[static_cast<std::size_t>(high)] -
                               times[static_cast<std::size_t>(low)];
    if (denominator > 0.0) {
      fraction = std::clamp(
          (source_seconds - times[static_cast<std::size_t>(low)]) / denominator,
          0.0, 1.0);
    }
  }
  const auto &samples = m_request.session->samples;
  const double altitude = samples[static_cast<std::size_t>(low)].altitude +
                          (samples[static_cast<std::size_t>(high)].altitude -
                           samples[static_cast<std::size_t>(low)].altitude) *
                              fraction;
  const double low_speed = finite_speed(m_request.analysis.speed_mps, low);
  const double high_speed = finite_speed(m_request.analysis.speed_mps, high);
  const double speed = low_speed + (high_speed - low_speed) * fraction;
  const QVector3D position =
      m_sample_positions[static_cast<std::size_t>(low)] +
      (m_sample_positions[static_cast<std::size_t>(high)] -
       m_sample_positions[static_cast<std::size_t>(low)]) *
          static_cast<float>(fraction);
  const int visible =
      static_cast<int>(std::upper_bound(m_path_sample_indices.begin(),
                                        m_path_sample_indices.end(), high) -
                       m_path_sample_indices.begin());
  const double source_start =
      times[static_cast<std::size_t>(m_request.analysis.start_index)];
  const double source_end =
      times[static_cast<std::size_t>(m_request.analysis.end_index - 1)];
  const double replay_progress =
      source_end > source_start ? std::clamp((source_seconds - source_start) /
                                                 (source_end - source_start),
                                             0.0, 1.0)
                                : 1.0;
  const int phase = video_seconds < m_timeline->intro_seconds()
                        ? 0
                        : (video_seconds < m_timeline->intro_seconds() +
                                               m_timeline->replay_seconds()
                               ? 1
                               : 2);
  const double camera_progress =
      phase == 1 ? std::clamp((video_seconds - m_timeline->intro_seconds()) /
                                  m_timeline->replay_seconds(),
                              0.0, 1.0)
                 : (phase == 2 ? 1.0 : 0.0);

  root->setProperty("visiblePointCount", visible);
  root->setProperty("currentPosition", QVariant::fromValue(position));
  root->setProperty("currentAltitude", altitude);
  root->setProperty("currentSpeed", speed);
  root->setProperty("elapsedFlightSeconds",
                    std::max(0.0, source_seconds - source_start));
  root->setProperty("replayProgress", replay_progress);
  root->setProperty("cameraProgress", camera_progress);
  root->setProperty("phase", phase);
}

void VideoExportService::finish_stream() {
  if (m_end_frame_pending || m_end_frame_sent) {
    return;
  }
  m_end_frame_pending = true;
  submit_pending_frame();
}

void VideoExportService::on_recorder_state_changed() {
  if (!m_recorder) {
    return;
  }
  if (m_recorder->recorderState() == QMediaRecorder::RecordingState) {
    submit_pending_frame();
    return;
  }
  if (m_recorder->recorderState() != QMediaRecorder::StoppedState || !m_busy) {
    return;
  }
  if (m_cancel_requested) {
    reset_export_objects();
    remove_temporary_output();
    m_busy = false;
    emit cancelled();
  } else if (m_end_frame_sent) {
    finalize_output();
  }
}

void VideoExportService::finalize_output() {
  emit phase_changed(tr("Validating video…"));
  const QFileInfo temporary_info(m_temporary_output_path);
  if (!temporary_info.exists() || temporary_info.size() <= 0) {
    fail_export(tr("The encoder did not produce a valid output file."));
    return;
  }
  const QString output_path = m_request.output_path;
  reset_export_objects();
  QString validation_error;
  if (!validate_encoded_video(m_temporary_output_path,
                              m_request.preset.resolution,
                              m_timeline->total_seconds(), validation_error)) {
    fail_export(validation_error);
    return;
  }
  bool committed = false;
  if (QFileInfo::exists(output_path)) {
    QFile source(m_temporary_output_path);
    QSaveFile destination(output_path);
    destination.setDirectWriteFallback(false);
    if (source.open(QIODevice::ReadOnly) &&
        destination.open(QIODevice::WriteOnly)) {
      QByteArray buffer(1024 * 1024, Qt::Uninitialized);
      committed = true;
      while (!source.atEnd()) {
        const qint64 count = source.read(buffer.data(), buffer.size());
        if (count <= 0 ||
            destination.write(buffer.constData(), count) != count) {
          committed = false;
          break;
        }
      }
      if (committed) {
        committed = destination.commit();
      } else {
        destination.cancelWriting();
      }
    }
    source.close();
    if (committed) {
      QFile::remove(m_temporary_output_path);
    }
  } else {
    committed = QFile::rename(m_temporary_output_path, output_path);
  }
  if (!committed) {
    remove_temporary_output();
    m_busy = false;
    emit failed(tr("The completed video could not be atomically moved to its "
                   "destination."));
    return;
  }
  m_temporary_output_path.clear();
  m_busy = false;
  emit progress_changed(m_timeline ? m_timeline->frame_count() : 0,
                        m_timeline ? m_timeline->frame_count() : 0);
  m_timeline.reset();
  emit finished(output_path);
}

void VideoExportService::fail_export(const QString &message) {
  if (!m_busy) {
    return;
  }
  m_busy = false;
  if (m_recorder) {
    m_recorder->disconnect(this);
    if (m_recorder->recorderState() != QMediaRecorder::StoppedState) {
      m_recorder->stop();
    }
  }
  reset_export_objects();
  remove_temporary_output();
  emit failed(message.isEmpty() ? tr("Replay video export failed.") : message);
}

void VideoExportService::reset_export_objects() {
  m_pending_frame = QVideoFrame();
  m_capture_session.reset();
  m_video_input.reset();
  m_recorder.reset();
  m_view.reset();
  m_asset_directory.reset();
  m_sample_positions.clear();
  m_path_sample_indices.clear();
  m_scene_path.clear();
  m_altitude_profile.clear();
}

void VideoExportService::remove_temporary_output() {
  if (!m_temporary_output_path.isEmpty()) {
    QFile::remove(m_temporary_output_path);
    m_temporary_output_path.clear();
  }
}

} // namespace cosmo::video
