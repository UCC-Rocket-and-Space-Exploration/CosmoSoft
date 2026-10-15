#include "gui/FlightDataModel.h"
#include "gui/FlightReplayController.h"
#include "gui/MainWindow.h"
#include "gui/SkinLoader.h"
#include "gui/ThemeManager.h"
#include "gui/pages/DashboardPage.h"
#include "gui/pages/LiveTelemetryPage.h"
#include "gui/widgets/Map3DWidget.h"
#include "gui/widgets/RocketAttitudeWidget.h"
#include "gui/widgets/StatTileWidget.h"
#include "services/import/SampleFileLoader.h"
#include "services/preview/FlightPreviewCache.h"
#include "services/telemetry/LineTelemetryBatchMailbox.h"
#include "services/telemetry/RecordedComms.h"
#include "services/telemetry/TelemetrySession.h"

#include <QAction>
#include <QApplication>
#include <QChartView>
#include <QCheckBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFontDatabase>
#include <QLabel>
#include <QLineSeries>
#include <QMenu>
#include <QPushButton>
#include <QQuaternion>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSlider>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOptionButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QValueAxis>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineUrlRequestInfo>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineView>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>

namespace {
class BlockNetwork final : public QWebEngineUrlRequestInterceptor {
public:
  void interceptRequest(QWebEngineUrlRequestInfo &request) override {
    if (request.requestUrl().scheme() == "http" ||
        request.requestUrl().scheme() == "https")
      request.block(true);
  }
};
bool pumpUntil(const std::function<bool()> &predicate) {
  QEventLoop loop;
  QTimer poll;
  QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
    if (predicate())
      loop.quit();
  });
  poll.start(10);
  QTimer::singleShot(3000, &loop, &QEventLoop::quit);
  if (!predicate())
    loop.exec();
  return predicate();
}
} // namespace

TEST_CASE(
    "Replay and live pages contain their own embedded attitude instrument",
    "[attitude][widgets]") {
  FlightDataModel model;
  FlightReplayController replay;
  DashboardPage dashboard(&model, &replay);
  LiveTelemetryPage live(&model);
  auto *replayAttitude = dashboard.findChild<QWidget *>("rocketAttitudeWidget");
  auto *liveAttitude = live.findChild<QWidget *>("rocketAttitudeWidget");
  REQUIRE(replayAttitude);
  REQUIRE(liveAttitude);
  REQUIRE_FALSE(replayAttitude->isWindow());
  REQUIRE_FALSE(liveAttitude->isWindow());
  REQUIRE(replayAttitude != liveAttitude);
}

TEST_CASE("Replay attitude is visible only while flight data is loaded",
          "[attitude][widgets]") {
  FlightDataModel model;
  FlightReplayController replay;
  DashboardPage dashboard(&model, &replay);
  auto *attitude = dashboard.findChild<QWidget *>("rocketAttitudeWidget");
  REQUIRE(attitude);
  dashboard.show();
  REQUIRE_FALSE(attitude->isVisible());

  auto session = std::make_shared<FlightSession>();
  session->samples.emplace_back();
  model.setReplayMode(true);
  dashboard.setReplaySession(session, nullptr);
  REQUIRE(attitude->isVisible());
  dashboard.setReplayTrailLength(0);
  REQUIRE(attitude->isVisible());

  dashboard.setReplaySession(nullptr, nullptr);
  REQUIRE_FALSE(attitude->isVisible());
  dashboard.setReplaySession(session, nullptr);
  REQUIRE(attitude->isVisible());
  dashboard.setReplaySession(std::make_shared<FlightSession>(), nullptr);
  REQUIRE_FALSE(attitude->isVisible());
  dashboard.setReplaySession(session, nullptr);
  model.resetSession();
  REQUIRE_FALSE(attitude->isVisible());
}

TEST_CASE("Replay map displays recorded attitude without a mounting correction",
          "[attitude][recorded-pose]") {
  auto session = std::make_shared<FlightSession>();
  FlightSample missing;
  missing.angularRotation.x = std::numeric_limits<double>::quiet_NaN();
  FlightSample launch;
  launch.timestamp = 100;
  launch.coordinates = {54, -5};
  launch.angularRotation = {0, 0, 90};
  FlightSample tilted = launch;
  tilted.timestamp = 200;
  tilted.angularRotation = {90, 0, 90};
  tilted.coordinates = {}; // Attitude must still update without a GPS fix.
  session->samples = {missing, launch, tilted, missing};

  Map3DWidget map;
  map.setReplaySession(session, nullptr);
  map.show();
  auto *browser = map.findChild<QWebEngineView *>();
  REQUIRE(browser);
  auto block = std::make_unique<BlockNetwork>();
  block->setParent(browser->page()->profile());
  browser->page()->profile()->setUrlRequestInterceptor(block.release());
  auto *bridge = map.findChild<Map3DBridge *>();
  REQUIRE(bridge);
  bool ready = false;
  QObject::connect(bridge, &Map3DBridge::ready, &map, [&] { ready = true; });
  REQUIRE(pumpUntil([&] { return ready; }));
  QVariantMap current;
  int publishedTrail = -1;
  QObject::connect(bridge, &Map3DBridge::trailLengthChanged, &map,
                   [&](int trail, const QVariantMap &point) {
                     publishedTrail = trail;
                     current = point;
                   });
  const auto seek = [&](int trail) {
    publishedTrail = -1;
    map.setReplayTrailLength(trail);
    REQUIRE(pumpUntil([&] { return publishedTrail == trail; }));
    return current.value("attitude").toList();
  };
  const auto direction = [](const QVariantList &payload,
                            QVector3D axis = {0, 1, 0}) {
    REQUIRE(payload.size() == 4);
    return QQuaternion(payload[3].toFloat(), payload[0].toFloat(),
                       payload[1].toFloat(), payload[2].toFloat())
        .rotatedVector(axis);
  };
  REQUIRE((direction(seek(2)) - QVector3D(-1, 0, 0)).length() < 1e-5F);
  REQUIRE((direction(seek(3), {1, 0, 0}) - QVector3D(0, 0, 1)).length() <
          1e-5F);
  REQUIRE(seek(4).isEmpty());
  REQUIRE((direction(seek(2)) - QVector3D(-1, 0, 0)).length() < 1e-5F);
  REQUIRE(seek(1).isEmpty());
  REQUIRE(seek(0).isEmpty());

  auto replacement = std::make_shared<FlightSession>();
  launch.angularRotation = {90, 0, 0};
  replacement->samples = {launch};
  map.setReplaySession(replacement, nullptr);
  REQUIRE((direction(seek(1)) - QVector3D(0, 0, 1)).length() < 1e-5F);
}

TEST_CASE("Replay empty state and instruments survive loading and clearing",
          "[replay-ui][widgets]") {
  FlightDataModel model;
  FlightReplayController replay;
  DashboardPage dashboard(&model, &replay);
  dashboard.resize(1024, 720);
  dashboard.show();
  QApplication::processEvents();
  auto *empty = dashboard.findChild<QLabel *>("chartEmptyState");
  auto *traces = dashboard.findChild<QWidget *>("tracesPanel");
  auto *attitude = dashboard.findChild<QWidget *>("rocketAttitudeWidget");
  auto *slider = dashboard.findChild<QSlider *>("replayScrubSlider");
  REQUIRE(empty);
  REQUIRE(traces);
  REQUIRE(attitude);
  REQUIRE(slider);
  auto *instruments = attitude->parentWidget();
  QToolButton *toggle = nullptr;
  QPushButton *graph = nullptr;
  QPushButton *map = nullptr;
  for (auto *button : dashboard.findChildren<QToolButton *>())
    if (button->text() == "Instruments")
      toggle = button;
  for (auto *button : dashboard.findChildren<QPushButton *>()) {
    if (button->accessibleName() == "Show telemetry chart")
      graph = button;
    if (button->accessibleName() == "Show flight path map")
      map = button;
  }
  REQUIRE(toggle);
  REQUIRE(graph);
  REQUIRE(map);
  const auto checkEmpty = [&] {
    QApplication::processEvents();
    CHECK(empty->isVisible());
    CHECK_FALSE(instruments->isVisible());
    CHECK_FALSE(toggle->isVisible());
    CHECK_FALSE(slider->isEnabled());
    CHECK(std::abs(empty->mapTo(&dashboard, empty->rect().center()).x() -
                   dashboard.rect().center().x()) <= 2);
  };
  const auto load = [&] {
    auto session = std::make_shared<FlightSession>();
    session->samples.emplace_back();
    model.setReplayMode(true);
    replay.setSession(session, nullptr);
    dashboard.setReplaySession(session, nullptr);
    REQUIRE_FALSE(empty->isVisible());
  };
  QString captureState;
  SECTION("Startup uses the whole content width") {
    captureState = "startup";
    checkEmpty();
  }
  SECTION("Clearing a loaded recording restores the placeholder") {
    captureState = "cleared";
    load();
    dashboard.setReplaySession(nullptr, nullptr);
    checkEmpty();
  }
  SECTION("Reset clears the loaded screen and transport") {
    captureState = "reset";
    load();
    model.resetSession();
    checkEmpty();
  }
  SECTION("Showing instruments after changing views restores traces") {
    captureState = "restored";
    load();
    toggle->setChecked(false);
    map->click();
    for (auto *profile : dashboard.findChildren<QWebEngineProfile *>()) {
      auto block = std::make_unique<BlockNetwork>();
      block->setParent(profile);
      profile->setUrlRequestInterceptor(block.release());
    }
    graph->click();
    toggle->setChecked(true);
    QApplication::processEvents();
    REQUIRE(traces->isVisible());
    REQUIRE(attitude->isVisible());
    for (const int availableWidth : {1440, 920, 1024}) {
      dashboard.resize(availableWidth, 720);
      QApplication::processEvents();
      QApplication::processEvents();
      CAPTURE(availableWidth);
      REQUIRE(dashboard.width() == availableWidth);
      for (auto *button : dashboard.findChildren<QToolButton *>())
        if (button->accessibleName() == "Chart tools" ||
            button->accessibleName() == "Show chart controls") {
          CAPTURE(button->accessibleName().toStdString());
          REQUIRE(button->isVisible());
          REQUIRE(button->parentWidget()->rect().contains(button->geometry()));
        }
      for (auto *button : dashboard.findChildren<QPushButton *>())
        if (button->isVisible() && button->objectName() == "chartToolbarBtn") {
          CAPTURE(button->text().toStdString());
          QStyleOptionButton option;
          option.initFrom(button);
          const auto contents = button->style()->subElementRect(
              QStyle::SE_PushButtonContents, &option, button);
          REQUIRE(contents.width() >=
                  button->fontMetrics().horizontalAdvance(button->text()));
        }
    }
  }
  const QString captureDir = qEnvironmentVariable("COSMO_TEST_SCREENSHOTS");
  if (!captureDir.isEmpty())
    for (const QString &theme :
         {QStringLiteral("dark"), QStringLiteral("light")}) {
      const auto skin = cosmo::SkinLoader::loadBuiltin(":/skins/" + theme);
      REQUIRE(skin);
      cosmo::ThemeManager::instance().setActiveSkin(*skin);
      for (const QSize size : {QSize(1024, 650), QSize(1440, 800)}) {
        dashboard.resize(size);
        QApplication::processEvents();
        REQUIRE(dashboard.grab().save(captureDir + "/replay-" + captureState +
                                      "-" + theme + "-" +
                                      QString::number(size.width()) + ".png"));
      }
    }
}

TEST_CASE("Embedded attitude follows replay seeks and separate live batches",
          "[attitude][widgets]") {
  FlightDataModel model;
  FlightReplayController replay;
  DashboardPage dashboard(&model, &replay);
  LiveTelemetryPage live(&model);
  auto *replayPanel = dashboard.findChild<QWidget *>("rocketAttitudeWidget");
  auto *livePanel = live.findChild<QWidget *>("rocketAttitudeWidget");
  REQUIRE(replayPanel);
  REQUIRE(livePanel);
  auto *replayAngles = replayPanel->findChild<QLabel *>("attitudeAngles");
  auto *liveAngles = livePanel->findChild<QLabel *>("attitudeAngles");
  REQUIRE(replayAngles);
  REQUIRE(liveAngles);
  auto session = std::make_shared<FlightSession>();
  FlightSample first;
  first.timestamp = 0;
  first.angularRotation = {30, 0, 45};
  FlightSample second = first;
  second.timestamp = 100;
  second.angularRotation = {-20, 90, 0};
  session->samples = {first, second};
  auto preview = cosmo::preview::FlightPreviewCache::build(*session);
  model.setReplayMode(true);
  replay.setSession(session, preview);
  dashboard.setReplaySession(session, preview);
  dashboard.setReplayTrailLength(2);
  dashboard.show();
  auto *traces = dashboard.findChild<QWidget *>("tracesPanel");
  REQUIRE(traces);
  REQUIRE(traces->isVisible());
  dashboard.hide();
  REQUIRE(replayAngles->text().contains("-20.0"));
  dashboard.setReplayTrailLength(1);
  REQUIRE(replayAngles->text().contains("30.0"));
  REQUIRE_FALSE(liveAngles->text().contains("30.0"));
  dashboard.setReplayTrailLength(0);
  REQUIRE_FALSE(replayAngles->text().contains("30.0"));
  model.setReplayMode(false);
  model.appendLiveBatch({second});
  REQUIRE(liveAngles->text().contains("-20.0"));
  first.angularRotation.x = std::numeric_limits<double>::quiet_NaN();
  model.appendLiveBatch({first});
  REQUIRE_FALSE(liveAngles->text().contains("-20.0"));
  REQUIRE(livePanel->findChild<QLabel *>("attitudeStatus")->text().isEmpty());
  REQUIRE(liveAngles->text().contains("—"));
  model.resetSession();
  REQUIRE_FALSE(replayAngles->text().contains("-20.0"));
  const QString captureDir = qEnvironmentVariable("COSMO_TEST_SCREENSHOTS");
  if (!captureDir.isEmpty()) {
    for (const QString &theme :
         {QStringLiteral("dark"), QStringLiteral("light")}) {
      const auto skin =
          cosmo::SkinLoader::loadBuiltin(QStringLiteral(":/skins/") + theme);
      REQUIRE(skin.has_value());
      cosmo::ThemeManager::instance().setActiveSkin(*skin);
      for (const QSize size : {QSize(1440, 900), QSize(1024, 768)}) {
        model.setReplayMode(true);
        dashboard.setReplaySession(session, preview);
        dashboard.setReplayTrailLength(1);
        dashboard.resize(size);
        dashboard.show();
        QApplication::processEvents();
        REQUIRE(dashboard.grab().save(captureDir + "/replay-" + theme + "-" +
                                      QString::number(size.width()) + ".png"));
        dashboard.hide();
        model.setReplayMode(false);
        model.appendLiveBatch({second});
        live.resize(size);
        live.show();
        for (auto *profile : live.findChildren<QWebEngineProfile *>()) {
          auto block = std::make_unique<BlockNetwork>();
          block->setParent(profile);
          profile->setUrlRequestInterceptor(block.release());
        }
        if (auto *browser = live.findChild<QWebEngineView *>()) {
          auto ready = std::make_shared<bool>(false);
          QTimer pollPage;
          QObject::connect(&pollPage, &QTimer::timeout, &live,
                           [browser, ready] {
                             browser->page()->runJavaScript(
                                 "typeof window.loadSession === 'function'",
                                 [ready](const QVariant &value) {
                                   *ready = value.toBool();
                                 });
                           });
          pollPage.start(20);
          REQUIRE(pumpUntil([&] { return *ready; }));
        }
        QApplication::processEvents();
        REQUIRE(live.grab().save(captureDir + "/live-" + theme + "-" +
                                 QString::number(size.width()) + ".png"));
        live.hide();
      }
    }
  }
}

TEST_CASE(
    "Live telemetry fits desktop viewports after resizing and theme changes",
    "[attitude][layout]") {
  FlightDataModel model;
  LiveTelemetryPage live(&model);
  auto *scroll = live.findChild<QScrollArea *>("liveTelemetryScroll");
  REQUIRE(scroll);
  FlightSample sample;
  sample.timestamp = 1000;
  sample.altitude = 1234.5;
  sample.temperature = 23;
  sample.pressure = 101325;
  sample.batteryVoltage = 12;
  sample.rssi = -100;
  sample.angularRotation = {30, -20, 45};
  FlightSample next = sample;
  next.timestamp = 1100;
  next.altitude += 12.34;
  model.appendLiveBatch({sample, next});
  live.setActiveConnection("Rocket telemetry receiver (layout test)", true);
  live.show();
  for (auto *profile : live.findChildren<QWebEngineProfile *>()) {
    auto block = std::make_unique<BlockNetwork>();
    block->setParent(profile);
    profile->setUrlRequestInterceptor(block.release());
  }
  for (const QString &theme :
       {QStringLiteral("dark"), QStringLiteral("light")}) {
    const auto skin = cosmo::SkinLoader::loadBuiltin(":/skins/" + theme);
    REQUIRE(skin.has_value());
    cosmo::ThemeManager::instance().setActiveSkin(*skin);
    for (const QSize size : {QSize(1024, 650), QSize(1440, 800),
                             QSize(640, 650), QSize(920, 650)}) {
      live.resize(size);
      QApplication::processEvents();
      QApplication::processEvents();
      CAPTURE(theme, size.width(), size.height());
      if (size.width() >= 920) {
        const QString captureDir =
            qEnvironmentVariable("COSMO_TEST_SCREENSHOTS");
        if (!captureDir.isEmpty())
          REQUIRE(live.grab().save(captureDir + "/layout-" + theme + "-" +
                                   QString::number(size.width()) + ".png"));
        REQUIRE(scroll->verticalScrollBar()->maximum() == 0);
        REQUIRE(scroll->horizontalScrollBar()->maximum() == 0);
        for (auto *tile : live.findChildren<StatTileWidget *>())
          for (auto *label : tile->findChildren<QLabel *>()) {
            CAPTURE(label->text().toStdString());
            REQUIRE(label->width() >= label->minimumSizeHint().width());
          }
        for (const QString &name : {QStringLiteral("rocketAttitudeWidget"),
                                    QStringLiteral("liveSidePanel"),
                                    QStringLiteral("liveIgnitorPanel")}) {
          auto *panel = live.findChild<QWidget *>(name);
          REQUIRE(panel);
          REQUIRE(scroll->viewport()->rect().contains(QRect(
              panel->mapTo(scroll->viewport(), QPoint()), panel->size())));
        }
      }
    }
  }
}

// Desktop smoke test; scans metadata only and never opens a serial device.
TEST_CASE("Mission toolbar can be restored from View after it is hidden",
          "[.][toolbar-ui]") {
  MainWindow window;
  window.resize(1280, 900);
  window.show();
  auto *toolbar = window.findChild<QToolBar *>("missionToolbar");
  auto *viewMenu = window.findChild<QMenu *>("viewMenu");
  REQUIRE(toolbar);
  REQUIRE(viewMenu);
  auto *toggle = toolbar->toggleViewAction();
  REQUIRE(viewMenu->actions().contains(toggle));
  REQUIRE(toggle->shortcut() == QKeySequence("Ctrl+Shift+M"));
  toolbar->hide();
  REQUIRE_FALSE(toolbar->isVisible());
  toggle->trigger();
  REQUIRE(toolbar->isVisible());
  toggle->trigger();
  REQUIRE_FALSE(toolbar->isVisible());
  toggle->trigger();
  REQUIRE(toolbar->isVisible());
}

TEST_CASE("Embedded attitude is compact, theme aware, and marks stopped live "
          "data stale",
          "[attitude][widgets]") {
  QWidget host;
  auto layout = std::make_unique<QVBoxLayout>(&host);
  auto panel = std::make_unique<RocketAttitudeWidget>(&host);
  auto *instrument = panel.get();
  layout->addWidget(panel.release());
  host.resize(260, 280);
  host.show();
  auto *status = instrument->findChild<QLabel *>("attitudeStatus");
  auto *timer = instrument->findChild<QTimer *>();
  REQUIRE(status);
  REQUIRE(timer);
  FlightSample sample;
  sample.angularRotation = {30, 0, 45};
  instrument->setSample(sample);
  REQUIRE(status->text().startsWith("Live"));
  timer->stop();
  REQUIRE(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
  REQUIRE(timer->isActive());
  REQUIRE(pumpUntil([&] { return status->text().startsWith("Stale"); }));
  instrument->setSample(sample, true);
  REQUIRE(status->text().startsWith("Replay"));
  QString previousStyle;
  for (const QString &theme : {QStringLiteral("dark"), QStringLiteral("light"),
                               QStringLiteral("dark")}) {
    const auto skin =
        cosmo::SkinLoader::loadBuiltin(QStringLiteral(":/skins/") + theme);
    REQUIRE(skin.has_value());
    cosmo::ThemeManager::instance().setActiveSkin(*skin);
    REQUIRE(instrument->styleSheet() != previousStyle);
    previousStyle = instrument->styleSheet();
    QApplication::processEvents();
    REQUIRE(instrument->height() <= 280);
    for (auto *label : instrument->findChildren<QLabel *>())
      REQUIRE(instrument->rect().contains(label->geometry()));
    const QString captureDir = qEnvironmentVariable("COSMO_TEST_SCREENSHOTS");
    if (!captureDir.isEmpty())
      REQUIRE(host.grab().save(captureDir + "/attitude-" + theme + ".png"));
  }
}

// Run explicitly with a graphics-capable Qt platform; CTest's offscreen target
// disables GL.
TEST_CASE("3D replay displays recording angles with the original path and an "
          "orange rocket",
          "[.][attitude-webgl]") {
  auto session = std::make_shared<FlightSession>();
  REQUIRE_FALSE(SampleFileLoader::loadTheseusCsv(
                    std::string(COSMO_SOURCE_DIR) +
                        "/sample_data/flight_2026-05-30_17-34-27.csv",
                    *session)
                    .has_value());
  Map3DWidget map;
  map.resize(800, 520);
  bool positioned = false;
  QObject::connect(&map, &Map3DWidget::positionStatsChanged, &map,
                   [&](double, double, double, double, double, double, int,
                       int valid, double, double) { positioned = valid > 0; });
  map.setReplaySession(session,
                       cosmo::preview::FlightPreviewCache::build(*session));
  map.show();
  auto *browser = map.findChild<QWebEngineView *>();
  REQUIRE(browser);
  auto block = std::make_unique<BlockNetwork>();
  block->setParent(browser->page()->profile());
  browser->page()->profile()->setUrlRequestInterceptor(block.release());
  REQUIRE(pumpUntil([&] { return positioned; }));
  const auto evaluate = [&](const QString &script) {
    auto result = std::make_shared<std::pair<bool, QString>>();
    browser->page()->runJavaScript(script, [result](const QVariant &value) {
      result->second = value.toString();
      result->first = true;
    });
    REQUIRE(pumpUntil([&] { return result->first; }));
    return result->second;
  };
  const auto info = evaluate(
      QStringLiteral("document.getElementById('btn3D').click(); "
                     "document.getElementById('info3d').textContent;"));
  REQUIRE(info.contains("3x vert"));
  REQUIRE_FALSE(info.contains("Attitude unavailable"));
  REQUIRE(evaluate(QStringLiteral(
              "String(document.querySelectorAll('#view3d canvas').length)")) ==
          "1");
  REQUIRE(evaluate(QStringLiteral(
              "String(document.getElementById('path3dOptions') === null)")) ==
          "true");
  evaluate(QStringLiteral("document.getElementById('btnFit').click();"));
  const QString captureDir = qEnvironmentVariable("COSMO_TEST_SCREENSHOTS");
  if (!captureDir.isEmpty()) {
    for (const QString &theme :
         {QStringLiteral("dark"), QStringLiteral("light")}) {
      const auto skin =
          cosmo::SkinLoader::loadBuiltin(QStringLiteral(":/skins/") + theme);
      REQUIRE(skin.has_value());
      cosmo::ThemeManager::instance().setActiveSkin(*skin);
      bool rendered = false;
      QTimer::singleShot(200, &map, [&] { rendered = true; });
      REQUIRE(pumpUntil([&] { return rendered; }));
      REQUIRE(map.grab().save(captureDir + "/map-" + theme + ".png"));
    }
  }
}

TEST_CASE("Recorded telemetry pages update in both themes and survive page "
          "switches and stop",
          "[imu][widgets]") {
  using namespace cosmo::telemetry;
  FlightDataModel model;
  FlightReplayController replay;
  QStackedWidget pages;
  auto live = std::make_unique<LiveTelemetryPage>(&model);
  auto dashboard = std::make_unique<DashboardPage>(&model, &replay);
  pages.addWidget(live.get());
  pages.addWidget(dashboard.get());
  pages.resize(1440, 950);
  pages.show();
  for (auto *profile : qApp->findChildren<QWebEngineProfile *>()) {
    auto blocker = std::make_unique<BlockNetwork>();
    blocker->setParent(profile);
    profile->setUrlRequestInterceptor(blocker.release());
  }
  auto *button =
      live->findChild<QPushButton *>(QStringLiteral("recordedImuInput"));
  REQUIRE(button);
  bool requested = false;
  QObject::connect(live.get(), &LiveTelemetryPage::recordedImuRequested,
                   live.get(), [&] { requested = true; });
  button->click();
  REQUIRE(requested);

  LineTelemetryBatchMailbox mailbox;
  int received = 0;
  const auto drain = [&] {
    for (const auto &batch : mailbox.take(1).batches) {
      FlightSampleBatch samples;
      for (const auto &sample : batch.samples)
        samples.append(sample);
      received += samples.size();
      model.addBytesReceived(static_cast<qint64>(batch.receivedBytes));
      model.appendLiveBatch(samples);
    }
  };
  TelemetrySession session(
      std::make_shared<RecordedComms>(std::string(COSMO_SOURCE_DIR) +
                                      "/sample_data/recorded_imu.telem"),
      [&](LineTelemetryBatch batch) {
        if (mailbox.push(1, std::move(batch)))
          QMetaObject::invokeMethod(&model, drain, Qt::QueuedConnection);
      },
      [](const std::string &) {});
  live->setActiveConnection(QStringLiteral("Recorded IMU input"), true);
  REQUIRE(session.start());
  REQUIRE(pumpUntil([&] { return received >= 2; }));
  REQUIRE(std::isfinite(model.latestSample().temperature));
  REQUIRE(model.totalBytesReceived() > 0);
  REQUIRE(std::isnan(model.latestSample().altitude));
  REQUIRE(std::isnan(model.latestSample().coordinates.latitude));

  const auto chartSeries = [&] {
    QList<QLineSeries *> lines;
    for (auto *view : dashboard->findChildren<QChartView *>()) {
      for (auto *series : view->chart()->series())
        if (auto *line = qobject_cast<QLineSeries *>(series))
          lines.append(line);
    }
    return lines;
  };
  QString previousStyle;
  for (const QString &theme : {QStringLiteral("dark"), QStringLiteral("light"),
                               QStringLiteral("dark")}) {
    const auto skin =
        cosmo::SkinLoader::loadBuiltin(QStringLiteral(":/skins/") + theme);
    REQUIRE(skin.has_value());
    cosmo::ThemeManager::instance().setActiveSkin(*skin);
    REQUIRE(live->styleSheet() != previousStyle);
    previousStyle = live->styleSheet();
    pages.setCurrentWidget(live.get());
    QApplication::processEvents();
    int missingTiles = 0;
    for (auto *tile : live->findChildren<StatTileWidget *>()) {
      for (auto *label : tile->findChildren<QLabel *>()) {
        if (label->property("kind") == "statValue" &&
            label->text() == QStringLiteral("—"))
          ++missingTiles;
      }
    }
    REQUIRE(missingTiles >=
            4); // altitude, vertical speed, pressure and battery
    REQUIRE(button->isEnabled());
    button->setFocus();
    const QString captureDir = qEnvironmentVariable("COSMO_TEST_SCREENSHOTS");
    if (!captureDir.isEmpty())
      REQUIRE(
          pages.grab().save(captureDir + "/recorded-imu-" + theme + ".png"));
    pages.setCurrentWidget(dashboard.get());
    for (auto *check : dashboard->findChildren<QCheckBox *>()) {
      if (check->objectName() == QStringLiteral("traceCheck"))
        check->setChecked(true);
    }
    REQUIRE(pumpUntil([&] {
      for (auto *series : chartSeries()) {
        for (const auto &point : series->points()) {
          if (point.y() >= 21 && point.y() < 23)
            return true;
        }
      }
      return false;
    }));
    for (auto *series : chartSeries()) {
      for (const auto &point : series->points())
        REQUIRE(std::isfinite(point.y()));
    }
  }
  session.stop();
  drain();
  const int stoppedCount = received;
  QApplication::processEvents();
  REQUIRE(received == stoppedCount);
  REQUIRE_FALSE(session.running());
  // Exercise destruction with another actively paced reader.
  REQUIRE(session.start());
  pages.close();
  for (auto *profile : qApp->findChildren<QWebEngineProfile *>()) {
    profile->setUrlRequestInterceptor(nullptr);
  }
}

TEST_CASE("TELEM imports render and replay while the GUI processes events",
          "[import][widgets]") {
  FlightDataModel model;
  FlightReplayController replay;
  DashboardPage dashboard(&model, &replay);
  dashboard.resize(1440, 950);
  dashboard.show();
  for (const auto *name : {"recorded_imu.telem", "altos_sample_data.telem",
                           "altos_sample_data2.telem"}) {
    auto session = std::make_shared<FlightSession>();
    FrameDecoderVault vault;
    const auto result = SampleFileLoader::loadTelemFile(
        std::string(COSMO_SOURCE_DIR) + "/sample_data/" + name, *session, vault,
        cosmo::CancellationCheck{});
    REQUIRE(result.succeeded());
    auto preview = cosmo::preview::FlightPreviewCache::build(*session);
    REQUIRE(preview);
    model.setReplayMode(true);
    replay.setSession(session, preview);
    dashboard.setReplaySession(session, preview);
    for (auto *check : dashboard.findChildren<QCheckBox *>()) {
      if (check->objectName() == QStringLiteral("traceCheck"))
        check->setChecked(true);
    }
    replay.setPosition(static_cast<int>(session->samples.size()));
    bool tick = false;
    QTimer::singleShot(100, &dashboard, [&] { tick = true; });
    REQUIRE(pumpUntil([&] { return tick; }));
    bool plotted = false;
    for (auto *view : dashboard.findChildren<QChartView *>()) {
      for (auto *abstractAxis : view->chart()->axes()) {
        if (auto *axis = qobject_cast<QValueAxis *>(abstractAxis)) {
          REQUIRE(axis->tickType() == QValueAxis::TicksFixed);
          REQUIRE(axis->tickCount() <= 9);
        }
      }
      for (auto *series : view->chart()->series()) {
        if (auto *line = qobject_cast<QLineSeries *>(series)) {
          plotted |= line->count() > 0;
          for (const auto &point : line->points()) {
            REQUIRE(std::isfinite(point.x()));
            REQUIRE(std::isfinite(point.y()));
          }
        }
      }
    }
    REQUIRE(plotted);
  }
}

int main(int argc, char **argv) {
  QApplication application(argc, argv);
  QTemporaryDir settings;
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                     settings.path());
  Q_INIT_RESOURCE(resources);
  const auto id = QFontDatabase::addApplicationFont(
      QStringLiteral(":/fonts/RedHatMono-Regular.ttf"));
  if (id >= 0)
    application.setFont(
        QFont(QFontDatabase::applicationFontFamilies(id).first(), 12));
  BlockNetwork block;
  QWebEngineProfile::defaultProfile()->setUrlRequestInterceptor(&block);
  const int result = Catch::Session().run(argc, argv);
  QWebEngineProfile::defaultProfile()->setUrlRequestInterceptor(nullptr);
  return result;
}
