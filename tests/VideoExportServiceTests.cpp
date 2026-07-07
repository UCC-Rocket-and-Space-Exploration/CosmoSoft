#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "services/video/FlightAnalyzer.h"
#include "services/video/VideoExportService.h"

#include <QEventLoop>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>
#include <QTimer>

#include <array>

TEST_CASE("VideoExportService loads the bundled cinematic scene",
          "[video][renderer]") {
  cosmo::video::VideoExportService service;
  QQmlEngine engine;
  QQmlComponent component(
      &engine, QUrl(QStringLiteral("qrc:/video/FlightReplayScene.qml")));
  INFO(component.errorString().toStdString());
  REQUIRE(component.status() == QQmlComponent::Ready);
  std::unique_ptr<QObject> scene(component.create());
  REQUIRE(scene != nullptr);
}

TEST_CASE("VideoExportService scene renders every export resolution",
          "[video][renderer]") {
  if (QGuiApplication::screens().isEmpty()) {
    SKIP("No screen is available for QQuickWindow frame capture");
  }
  cosmo::video::VideoExportService resource_owner;
  const std::array<QSize, 4> resolutions{QSize(1280, 720), QSize(1920, 1080),
                                         QSize(720, 1280), QSize(1080, 1920)};
  QQuickView view;
  view.setResizeMode(QQuickView::SizeRootObjectToView);
  view.setSource(QUrl(QStringLiteral("qrc:/video/FlightReplayScene.qml")));
  REQUIRE(view.status() == QQuickView::Ready);
  view.create();
  for (const QSize &resolution : resolutions) {
    view.resize(resolution);
    QEventLoop wait;
    QTimer::singleShot(100, &wait, &QEventLoop::quit);
    wait.exec();
    const QImage frame = view.grabWindow();
    INFO("expected " << resolution.width() << "x" << resolution.height()
                     << ", grabbed " << frame.width() << "x" << frame.height()
                     << " at DPR " << frame.devicePixelRatio());
    REQUIRE_FALSE(frame.isNull());
    REQUIRE(qRound(view.rootObject()->property("width").toReal()) ==
            resolution.width());
    REQUIRE(qRound(view.rootObject()->property("height").toReal()) ==
            resolution.height());
    REQUIRE(static_cast<double>(frame.width()) /
                static_cast<double>(frame.height()) ==
            Catch::Approx(static_cast<double>(resolution.width()) /
                          static_cast<double>(resolution.height()))
                .epsilon(0.01));
  }
}

TEST_CASE("VideoExportService cancellation removes partial output",
          "[video][encoder]") {
  if (QGuiApplication::screens().isEmpty()) {
    SKIP("No screen is available for QQuickWindow frame capture");
  }
  if (!cosmo::video::VideoExportService::h264_mp4_available()) {
    SKIP("Qt FFmpeg H.264 encoding is unavailable in this runtime");
  }

  auto session = std::make_shared<FlightSession>();
  for (int i = 0; i < 20; ++i) {
    FlightSample sample;
    sample.timestamp = i * 100;
    sample.altitude = static_cast<double>(i) * 2.0;
    session->samples.push_back(sample);
  }
  cosmo::video::VideoExportRequest request;
  request.session = session;
  request.analysis = cosmo::video::FlightAnalyzer::analyze(
      *session, cosmo::video::FlightTrimRange{
                    0, static_cast<int>(session->samples.size())});
  request.preset = cosmo::video::VideoExportPreset::make(
      cosmo::video::VideoOrientation::landscape,
      cosmo::video::VideoQuality::draft_720p);
  request.title = QStringLiteral("Cancellation test");

  QTemporaryDir output_directory;
  REQUIRE(output_directory.isValid());
  request.output_path =
      output_directory.filePath(QStringLiteral("cancelled.mp4"));

  cosmo::video::VideoExportService service;
  QEventLoop loop;
  bool was_cancelled = false;
  QString failure;
  QObject::connect(&service,
                   &cosmo::video::VideoExportService::progress_changed,
                   &service, [&service](int completed, int) {
                     if (completed >= 1)
                       service.cancel();
                   });
  QObject::connect(&service, &cosmo::video::VideoExportService::cancelled,
                   &loop, [&]() {
                     was_cancelled = true;
                     loop.quit();
                   });
  QObject::connect(&service, &cosmo::video::VideoExportService::failed, &loop,
                   [&](const QString &message) {
                     failure = message;
                     loop.quit();
                   });
  QTimer::singleShot(30000, &loop, &QEventLoop::quit);
  service.start(request);
  if (service.is_busy())
    loop.exec();

  INFO(failure.toStdString());
  REQUIRE(was_cancelled);
  REQUIRE_FALSE(QFileInfo::exists(request.output_path));
}

TEST_CASE("VideoExportService refuses non-FFmpeg multimedia backends",
          "[video][encoder]") {
  const QByteArray previous = qgetenv("QT_MEDIA_BACKEND");
  qputenv("QT_MEDIA_BACKEND", QByteArrayLiteral("not-ffmpeg"));
  const bool available = cosmo::video::VideoExportService::h264_mp4_available();
  if (previous.isNull()) {
    qunsetenv("QT_MEDIA_BACKEND");
  } else {
    qputenv("QT_MEDIA_BACKEND", previous);
  }
  REQUIRE_FALSE(available);
}
