#include "gui/FlightDataModel.h"
#include "gui/FlightReplayController.h"
#include "gui/SkinLoader.h"
#include "gui/ThemeManager.h"
#include "gui/pages/DashboardPage.h"
#include "gui/pages/LiveTelemetryPage.h"
#include "gui/widgets/StatTileWidget.h"
#include "services/import/SampleFileLoader.h"
#include "services/telemetry/LineTelemetryBatchMailbox.h"
#include "services/telemetry/RecordedComms.h"
#include "services/telemetry/TelemetrySession.h"

#include <QApplication>
#include <QChartView>
#include <QCheckBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFontDatabase>
#include <QLabel>
#include <QLineSeries>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QValueAxis>
#include <QWebEngineProfile>
#include <QWebEngineUrlRequestInfo>
#include <QWebEngineUrlRequestInterceptor>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

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
