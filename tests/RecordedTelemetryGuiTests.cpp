#include "gui/FlightDataModel.h"
#include "gui/FlightReplayController.h"
#include "gui/MainWindow.h"
#include "gui/SkinLoader.h"
#include "gui/ThemeManager.h"
#include "gui/pages/DashboardPage.h"
#include "gui/pages/LiveTelemetryPage.h"
#include "gui/pages/SettingsPage.h"
#include "gui/widgets/Map3DWidget.h"
#include "gui/widgets/RecoveryPanel.h"
#include "gui/widgets/RocketAttitudeWidget.h"
#include "gui/widgets/StatTileWidget.h"
#include "gui/widgets/TelemetryChartView.h"
#include "gui/widgets/VisualHorizonWidget.h"
#include "services/import/SampleFileLoader.h"
#include "services/preview/FlightPreviewCache.h"
#include "services/telemetry/LineTelemetryBatchMailbox.h"
#include "services/telemetry/RecordedComms.h"
#include "services/telemetry/TelemetrySession.h"

#include "gui/SettingsKeys.h"
#include "gui/widgets/PreflightChecklistWidget.h"
#include <QAction>
#include <QApplication>
#include <QChartView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QInputDialog>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineSeries>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QQuaternion>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSlider>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QStyleOptionButton>
#include <QStyleOptionComboBox>
#include <QTableWidget>
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

TEST_CASE("Live west longitude correction is saved and fixed for a connection",
          "[gps-correction][mission-ui]") {
  QSettings settings(kSettingsOrg, kSettingsApp);
  settings.remove(kSettingsWestLongitudeCorrection);
  FlightDataModel model;
  {
    LiveTelemetryPage page(&model);
    auto *toggle = page.findChild<QCheckBox *>("westLongitudeCorrection");
    auto *details = page.findChild<QToolButton *>("linkDetailsToggle");
    REQUIRE(toggle);
    REQUIRE(details);
    REQUIRE_FALSE(toggle->isChecked());
    details->setChecked(true);
    toggle->click();
    REQUIRE(toggle->isChecked());
    REQUIRE(settings.value(kSettingsWestLongitudeCorrection).toBool());
    page.setActiveConnection("Receiver", true);
    REQUIRE_FALSE(toggle->isEnabled());
    toggle->click();
    REQUIRE(toggle->isChecked());
    page.setActiveConnection(QString(), false);
    REQUIRE(toggle->isEnabled());
  }
  {
    LiveTelemetryPage restored(&model);
    auto *toggle = restored.findChild<QCheckBox *>("westLongitudeCorrection");
    REQUIRE(toggle);
    REQUIRE(toggle->isChecked());
    const auto captureDir = qEnvironmentVariable("COSMO_TEST_SCREENSHOTS");
    restored.resize(1280, 900);
    restored.show();
    for (const QString &theme :
         {QStringLiteral("dark"), QStringLiteral("light")}) {
      const auto skin = cosmo::SkinLoader::loadBuiltin(":/skins/" + theme);
      REQUIRE(skin);
      cosmo::ThemeManager::instance().setActiveSkin(*skin);
      restored.findChild<QToolButton *>("linkDetailsToggle")->setChecked(true);
      QApplication::processEvents();
      REQUIRE(toggle->isVisible());
      REQUIRE(toggle->width() >= toggle->sizeHint().width());
      if (!captureDir.isEmpty()) {
        REQUIRE(QDir().mkpath(captureDir));
        REQUIRE(restored.grab().save(captureDir + "/gps-correction-" + theme +
                                     ".png"));
      }
    }
  }
  settings.remove(kSettingsWestLongitudeCorrection);
}

TEST_CASE(
    "Checklist profiles save edits and clear manual confirmations per session",
    "[mission-ui][checklist]") {
  struct RestoreProfiles {
    QVariant previous =
        QSettings(kSettingsOrg, kSettingsApp).value(kSettingsChecklistProfiles);
    ~RestoreProfiles() {
      QSettings settings(kSettingsOrg, kSettingsApp);
      if (previous.isValid())
        settings.setValue(kSettingsChecklistProfiles, previous);
      else
        settings.remove(kSettingsChecklistProfiles);
    }
  } restore;
  PreflightProfileEditor editor;
  auto *table = editor.findChild<QTableWidget *>("preflightItemsEditor");
  auto *save = editor.findChild<QPushButton *>("savePreflightProfiles");
  REQUIRE(table);
  REQUIRE(save);
  REQUIRE(table->rowCount() > 0);
  table->item(0, 0)->setText("Inspect fins");
  bool saved = false;
  editor.profilesSaved = [&] { saved = true; };
  save->click();
  REQUIRE(saved);
  PreflightChecklistWidget checklist;
  auto *list = checklist.findChild<QListWidget *>();
  REQUIRE(list);
  REQUIRE(list->count() > 0);
  REQUIRE(list->item(0)->text() == "Inspect fins");
  list->item(0)->setCheckState(Qt::Checked);
  REQUIRE(list->item(0)->checkState() == Qt::Checked);
  checklist.resetChecks();
  REQUIRE(list->item(0)->checkState() == Qt::Unchecked);
  const QString captureDir = qEnvironmentVariable("COSMO_TEST_SCREENSHOTS");
  if (!captureDir.isEmpty()) {
    editor.resize(650, 420);
    editor.show();
    QApplication::processEvents();
    for (const QString &theme :
         {QStringLiteral("dark"), QStringLiteral("light")}) {
      const auto skin = cosmo::SkinLoader::loadBuiltin(":/skins/" + theme);
      REQUIRE(skin);
      cosmo::ThemeManager::instance().setActiveSkin(*skin);
      QApplication::processEvents();
      REQUIRE(editor.grab().save(captureDir + "/preflight-editor-" + theme +
                                 ".png"));
    }
  }
}

TEST_CASE("Saving before close waits for both telemetry and metadata",
          "[mission-ui][save-close]") {
  struct DialogScope {
    bool previous = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    DialogScope() {
      QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
    }
    ~DialogScope() {
      QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, previous);
    }
  } scope;
  QTemporaryDir directory;
  const QString path = directory.filePath("flight.csv");
  bool failMetadata = false;
  SECTION("Successful save closes and retains telemetry with metadata") {}
  SECTION("Metadata failure keeps the recording open and unsaved") {
    REQUIRE(QDir().mkdir(path + ".mission.json"));
    failMetadata = true;
  }
  MainWindow window;
  window.resize(1280, 900);
  window.show();
  REQUIRE(QMetaObject::invokeMethod(&window, "onStartLiveDemo",
                                    Qt::DirectConnection));
  REQUIRE(QMetaObject::invokeMethod(&window, "onToggleFakeTransmission",
                                    Qt::DirectConnection));
  bool choseFile = false;
  QTimer dialogs;
  QObject::connect(&dialogs, &QTimer::timeout, &window, [&] {
    auto *modal = QApplication::activeModalWidget();
    if (auto *warning = qobject_cast<QMessageBox *>(modal))
      warning->done(QMessageBox::Save);
    else if (auto *file = qobject_cast<QFileDialog *>(modal)) {
      file->selectFile(path);
      choseFile = true;
      QMetaObject::invokeMethod(file, "accept", Qt::DirectConnection);
    }
  });
  dialogs.start(10);
  window.close();
  const bool completed = pumpUntil([&] {
    return choseFile &&
           (failMetadata
                ? window.statusBar()->currentMessage().contains("failed")
                : !window.isVisible());
  });
  CAPTURE(choseFile, failMetadata, window.isVisible(),
          window.statusBar()->currentMessage().toStdString());
  REQUIRE(completed);
  dialogs.stop();
  REQUIRE(QFile::exists(path));
  if (failMetadata) {
    REQUIRE(window.isVisible());
    bool warnedAgain = false;
    QTimer::singleShot(0, &window, [&] {
      if (auto *box =
              qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
        warnedAgain = true;
        box->done(QMessageBox::Discard);
      }
    });
    window.close();
    REQUIRE(pumpUntil([&] { return !window.isVisible(); }));
    REQUIRE(warnedAgain);
  } else
    REQUIRE(QFile::exists(path + ".mission.json"));
}

TEST_CASE(
    "Packet diagnostics keep the newest complete row and suppress repetitions",
    "[mission-ui][diagnostics]") {
  FlightDataModel model;
  LiveTelemetryPage live(&model);
  QString message;
  int changes = 0;
  QObject::connect(&live, &LiveTelemetryPage::packetDiagnosticChanged, &live,
                   [&](const QString &text) {
                     message = text;
                     ++changes;
                   });
  cosmo::telemetry::LineTelemetryBatch batch;
  batch.rowDiagnostic =
      cosmo::telemetry::TelemetryRowDiagnostic{2, 1U << 3, 0, true};
  live.updateLinkDiagnostics(batch);
  REQUIRE(message.contains("altitude"));
  REQUIRE(changes == 1);
  batch.rowDiagnostic->sequence = 3;
  live.updateLinkDiagnostics(batch);
  REQUIRE(changes == 1);
  batch.rowDiagnostic =
      cosmo::telemetry::TelemetryRowDiagnostic{1, 1U << 10, 0, true};
  live.updateLinkDiagnostics(batch);
  REQUIRE(message.contains("altitude"));
  REQUIRE(changes == 1);
  batch.rowDiagnostic =
      cosmo::telemetry::TelemetryRowDiagnostic{4, 0, 0, false};
  live.updateLinkDiagnostics(batch);
  REQUIRE(message.isEmpty());
  REQUIRE(changes == 2);
}

TEST_CASE("Changing chart traces preserves saved event annotations",
          "[mission-ui][events]") {
  FlightDataModel model;
  FlightReplayController replay;
  DashboardPage dashboard(&model, &replay);
  auto session = std::make_shared<FlightSession>();
  for (int i = 0; i < 3; ++i) {
    FlightSample sample;
    sample.timestamp = i * 1000;
    session->samples.push_back(sample);
  }
  const auto preview = cosmo::preview::FlightPreviewCache::build(*session);
  model.setReplayMode(true);
  replay.setSession(session, preview);
  dashboard.setReplaySession(session, preview);
  int edits = 0;
  QObject::connect(&dashboard, &DashboardPage::annotationsChanged, &dashboard,
                   [&] { ++edits; });
  const QJsonArray annotations{QJsonObject{{"time", 1.0}, {"name", "Event A"}}};
  dashboard.setEventAnnotations(annotations);
  REQUIRE(edits == 0);
  for (auto *check : dashboard.findChildren<QCheckBox *>("traceCheck"))
    if (check->text() == "Alt")
      check->setChecked(false);
  REQUIRE(edits == 0);
  REQUIRE(dashboard.eventAnnotations() == annotations);
  QAction *manage = nullptr;
  for (auto *action : dashboard.findChildren<QAction *>())
    if (action->text() == "Manage flight events…")
      manage = action;
  REQUIRE(manage);
  QTimer::singleShot(0, &dashboard, [&] {
    auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
    if (!dialog)
      return;
    auto *table = dialog->findChild<QTableWidget *>("flightEventsEditor");
    if (!table)
      return;
    table->item(0, 1)->setText("Renamed");
    table->item(0, 0)->setText("2");
    dialog->findChild<QDialogButtonBox *>()
        ->button(QDialogButtonBox::Save)
        ->click();
  });
  manage->trigger();
  REQUIRE(
      dashboard.eventAnnotations().at(0).toObject().value("name").toString() ==
      "Renamed");
  REQUIRE(
      dashboard.eventAnnotations().at(0).toObject().value("time").toDouble() ==
      2);
  REQUIRE(edits == 1);
}

TEST_CASE("Horizon and rocket are independent embedded instruments",
          "[mission-ui][navball]") {
  FlightDataModel model;
  FlightReplayController replay;
  DashboardPage dashboard(&model, &replay);
  LiveTelemetryPage live(&model);
  auto *horizon = dashboard.findChild<QWidget *>("visualHorizonWidget");
  auto *liveHorizon = live.findChild<QWidget *>("visualHorizonWidget");
  REQUIRE(horizon);
  REQUIRE(liveHorizon);
  REQUIRE(dashboard.findChild<QWidget *>("rocketAttitudeWidget"));
  REQUIRE(horizon->isHidden());
  auto session = std::make_shared<FlightSession>();
  FlightSample sample;
  sample.angularRotation = {30, 0, 45};
  session->samples.push_back(sample);
  auto preview = cosmo::preview::FlightPreviewCache::build(*session);
  model.setReplayMode(true);
  replay.setSession(session, preview);
  dashboard.setReplaySession(session, preview);
  REQUIRE_FALSE(horizon->isHidden());
  dashboard.setReplaySession(nullptr, nullptr);
  REQUIRE(horizon->isHidden());
}

TEST_CASE("Closing and clearing captured data require an explicit discard",
          "[mission-ui][unsaved]") {
  MainWindow window;
  window.resize(1280, 900);
  window.show();
  REQUIRE(QMetaObject::invokeMethod(&window, "onStartLiveDemo",
                                    Qt::DirectConnection));
  REQUIRE(QMetaObject::invokeMethod(&window, "onToggleFakeTransmission",
                                    Qt::DirectConnection));
  int warnings = 0;
  const auto choose = [&](QMessageBox::StandardButton button) {
    QTimer::singleShot(0, &window, [&, button] {
      if (auto *box =
              qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
        ++warnings;
        box->done(button);
      }
    });
  };
  choose(QMessageBox::Cancel);
  REQUIRE_FALSE(window.close());
  REQUIRE(warnings == 1);
  REQUIRE(window.isVisible());
  choose(QMessageBox::Cancel);
  REQUIRE(QMetaObject::invokeMethod(&window, "onClearFlightData",
                                    Qt::DirectConnection));
  REQUIRE(warnings == 2);
  choose(QMessageBox::Discard);
  window.close();
  REQUIRE(pumpUntil([&] { return !window.isVisible(); }));
  REQUIRE(warnings == 3);
}

TEST_CASE("Flight event markers stay with their recording",
          "[replay-ui][markers]") {
  FlightDataModel model;
  FlightReplayController replay;
  DashboardPage dashboard(&model, &replay);
  dashboard.resize(1280, 900);
  dashboard.show();
  auto session = std::make_shared<FlightSession>();
  for (int i = 0; i < 3; ++i) {
    FlightSample sample;
    sample.timestamp = i * 1000;
    sample.altitude = 100 + i;
    sample.temperature = 20 + i;
    session->samples.push_back(sample);
  }
  auto preview = cosmo::preview::FlightPreviewCache::build(*session);
  model.setReplayMode(true);
  replay.setSession(session, preview);
  dashboard.setReplaySession(session, preview);
  for (auto *check : dashboard.findChildren<QCheckBox *>("traceCheck"))
    check->setChecked(check->text() == "Alt" || check->text() == "Temp");
  QPushButton *compare = nullptr;
  for (auto *button : dashboard.findChildren<QPushButton *>())
    if (button->text() == "Compare selected")
      compare = button;
  REQUIRE(compare);
  compare->click();

  QAction *addMarker = nullptr;
  for (auto *action : dashboard.findChildren<QAction *>())
    if (action->text() == "Add event marker…")
      addMarker = action;
  REQUIRE(addMarker);
  bool enteredName = false;
  QTimer::singleShot(0, &dashboard, [&] {
    if (auto *dialog =
            qobject_cast<QInputDialog *>(QApplication::activeModalWidget())) {
      enteredName = true;
      dialog->setTextValue("Flight A event");
      dialog->accept();
    }
  });
  addMarker->trigger();
  REQUIRE(enteredName);
  const auto markerCount = [&] {
    int count = 0;
    for (auto *view : dashboard.findChildren<QChartView *>())
      for (auto *series : view->chart()->series())
        if (series->name() == "Flight A event")
          ++count;
    return count;
  };
  REQUIRE(markerCount() == 2); // Original trace and its comparison copy.
  dashboard.setReplayTrailLength(1);
  dashboard.setReplaySession(session, preview);
  REQUIRE(markerCount() == 2);
  QPushButton *back = nullptr;
  for (auto *button : dashboard.findChildren<QPushButton *>())
    if (button->text() == "Back")
      back = button;
  REQUIRE(back);
  back->click();
  REQUIRE(markerCount() == 2);
  compare->click();
  REQUIRE(markerCount() == 2);

  SECTION("Clearing a recording removes its markers") {
    dashboard.setReplaySession(nullptr, nullptr);
    REQUIRE(markerCount() == 0);
  }
  SECTION("Replacing a recording removes its markers") {
    auto replacement = std::make_shared<FlightSession>(*session);
    auto replacementPreview =
        cosmo::preview::FlightPreviewCache::build(*replacement);
    replay.setSession(replacement, replacementPreview);
    dashboard.setReplaySession(replacement, replacementPreview);
    REQUIRE(markerCount() == 0);
  }
  SECTION("Reset removes markers from the original and comparison charts") {
    model.resetSession();
    REQUIRE(markerCount() == 0);
  }
}

TEST_CASE("Comparison clears telemetry when replay returns to zero",
          "[replay-ui][comparison]") {
  FlightDataModel model;
  FlightReplayController replay;
  DashboardPage dashboard(&model, &replay);
  dashboard.show();
  auto session = std::make_shared<FlightSession>();
  for (int i = 0; i < 3; ++i) {
    FlightSample sample;
    sample.timestamp = i * 1000;
    sample.altitude = 100 + i;
    sample.temperature = 20 + i;
    session->samples.push_back(sample);
  }
  auto preview = cosmo::preview::FlightPreviewCache::build(*session);
  model.setReplayMode(true);
  replay.setSession(session, preview);
  dashboard.setReplaySession(session, preview);
  for (auto *check : dashboard.findChildren<QCheckBox *>("traceCheck"))
    check->setChecked(check->text() == "Alt" || check->text() == "Temp");
  for (auto *button : dashboard.findChildren<QPushButton *>())
    if (button->text() == "Compare selected")
      button->click();
  QChartView *comparison = nullptr;
  for (auto *view : dashboard.findChildren<QChartView *>())
    if (view->chart()->title().startsWith("Comparison:"))
      comparison = view;
  REQUIRE(comparison);
  const auto pointCount = [&] {
    qsizetype count = 0;
    for (auto *series : comparison->chart()->series())
      if (auto *line = qobject_cast<QLineSeries *>(series))
        count += line->count();
    return count;
  };
  REQUIRE(pointCount() > 0);
  dashboard.setReplayTrailLength(0);
  REQUIRE(pumpUntil([&] { return pointCount() == 0; }));
  dashboard.setReplayTrailLength(2);
  REQUIRE(pumpUntil([&] { return pointCount() > 0; }));
}

TEST_CASE("Live packet age follows receipt time through hiding and disconnect",
          "[live-age][widgets]") {
  qint64 now = 0;
  FlightDataModel model;
  LiveTelemetryPage live(&model, nullptr, [&] { return now; });
  live.show();
  for (auto *profile : live.findChildren<QWebEngineProfile *>()) {
    auto block = std::make_unique<BlockNetwork>();
    block->setParent(profile);
    profile->setUrlRequestInterceptor(block.release());
  }
  QLabel *age = nullptr;
  for (auto *label : live.findChildren<QLabel *>())
    if (label->text().startsWith("Last packet:"))
      age = label;
  REQUIRE(age);
  FlightSample sample;
  sample.timestamp = -100000;
  model.appendLiveBatch({sample});
  REQUIRE(age->text().contains("now"));
  now = 6500;
  live.hide();
  live.show();
  REQUIRE(age->text().contains("6 s ago"));
  auto *timer = live.findChild<QTimer *>("packetAgeTimer");
  REQUIRE(timer);
  REQUIRE(timer->isActive());
  now = 9000;
  REQUIRE(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
  REQUIRE(age->text().contains("9 s ago"));
  live.setActiveConnection({}, false);
  now = 12000;
  REQUIRE(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
  REQUIRE(age->text().contains("12 s ago"));
  live.hide();
  REQUIRE_FALSE(timer->isActive());
  now = 20000;
  model.appendLiveBatch({sample});
  REQUIRE_FALSE(timer->isActive());
  now = 23000;
  live.show();
  REQUIRE(age->text().contains("3 s ago"));
  model.resetSession();
  REQUIRE(age->text().contains("—"));
  REQUIRE_FALSE(timer->isActive());
}

TEST_CASE(
    "Shared hover timelines cannot inspect samples outside the active range",
    "[hover-range][widgets]") {
  auto chart = std::make_unique<QChart>();
  auto line = std::make_unique<QLineSeries>();
  line->setProperty("metricIndex", 0);
  line->append(0, 0);
  line->append(5, 5);
  chart->addSeries(line.release());
  chart->createDefaultAxes();
  TelemetryChartView view(chart.release());
  view.resize(600, 400);
  view.show();
  QApplication::processEvents();
  int inspected = -1;
  view.hoverDetail = [&](double, int index, int) {
    inspected = index;
    return QString::number(index);
  };
  const QVector<double> times{0, 1, 2, 3, 4, 5};
  view.setHoverXValues(times, 2, 4);
  const auto press = [&](int key) {
    QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
    QApplication::sendEvent(&view, &event);
  };
  press(Qt::Key_Right);
  REQUIRE(inspected == 3);
  REQUIRE_FALSE(times.isDetached());
  press(Qt::Key_Right);
  REQUIRE(inspected == 4);
  press(Qt::Key_Right);
  REQUIRE(inspected == 4);
  press(Qt::Key_Left);
  REQUIRE(inspected == 3);
  view.setHoverXValues(times, 1, 2);
  press(Qt::Key_Right);
  REQUIRE(inspected == 2);
  view.setHoverXValues({});
  inspected = -1;
  press(Qt::Key_Right);
  REQUIRE(inspected == -1);
}

TEST_CASE("Replay hover retains original rows and excludes future samples",
          "[hover-range][replay-ui]") {
  FlightDataModel model;
  FlightReplayController replay;
  DashboardPage dashboard(&model, &replay);
  dashboard.resize(1280, 900);
  dashboard.show();
  auto session = std::make_shared<FlightSession>();
  for (int i = 0; i < 5000; ++i) {
    FlightSample sample;
    sample.timestamp = i * 10;
    sample.altitude = i * 0.1;
    session->samples.push_back(sample);
  }
  auto preview = cosmo::preview::FlightPreviewCache::build(*session);
  model.setReplayMode(true);
  replay.setSession(session, preview);
  dashboard.setReplaySession(session, preview);
  TelemetryChartView *view = nullptr;
  QLineSeries *altitude = nullptr;
  for (auto *candidate : dashboard.findChildren<QChartView *>())
    for (auto *series : candidate->chart()->series())
      if (series->property("metricIndex").isValid() &&
          series->property("metricIndex").toInt() == 0) {
        view = dynamic_cast<TelemetryChartView *>(candidate);
        altitude = qobject_cast<QLineSeries *>(series);
      }
  REQUIRE(view);
  REQUIRE(altitude);
  REQUIRE(view->hoverDetail(17.76, 1777, 5000).contains("row 1777 / 5000"));
  REQUIRE(std::abs(view->hoverValue(altitude, 1777) - 177.6) < 1e-8);
  dashboard.setReplayTrailLength(1800);
  REQUIRE(
      pumpUntil([&] { return view->hoverDetail(20, 2001, 5000).isEmpty(); }));
  REQUIRE(view->hoverDetail(17.76, 1777, 5000).contains("row 1777 / 5000"));
  dashboard.setReplayTrailLength(0);
  REQUIRE(pumpUntil([&] { return view->hoverDetail(0, 1, 5000).isEmpty(); }));
}

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

TEST_CASE(
    "Flight map aligns held GPS coordinates without changing measurements",
    "[trajectory]") {
  auto session = std::make_shared<FlightSession>();
  for (int i = 0; i < 5; ++i) {
    FlightSample sample;
    sample.timestamp = i * 250;
    sample.coordinates = {54, i == 4 ? -4.999 : -5};
    sample.altitude = 100 + i * 25;
    session->samples.push_back(sample);
  }
  Map3DWidget map;
  map.setReplaySession(session,
                       cosmo::preview::FlightPreviewCache::build(*session));
  map.show();
  auto *browser = map.findChild<QWebEngineView *>();
  REQUIRE(browser);
  auto block = std::make_unique<BlockNetwork>();
  block->setParent(browser->page()->profile());
  browser->page()->profile()->setUrlRequestInterceptor(block.release());
  auto *bridge = map.findChild<Map3DBridge *>();
  REQUIRE(bridge);
  QVariantMap current;
  QObject::connect(bridge, &Map3DBridge::trailLengthChanged, &map,
                   [&](int, const QVariantMap &point) { current = point; });
  map.setReplayTrailLength(2);
  REQUIRE(pumpUntil([&] { return !current.isEmpty(); }));
  REQUIRE(current.value("lon").toDouble() == -5);
  REQUIRE(current.value("alt").toDouble() == 125);
  REQUIRE(std::abs(current.value("displayLon").toDouble() + 4.99975) < 1e-9);
  REQUIRE(session->samples[1].coordinates.longitude == -5);

  map.onSessionReset();
  QVariantMap live;
  QObject::connect(bridge, &Map3DBridge::liveBatchAdded, &map,
                   [&](const QVariantMap &batch) { live = batch; });
  map.onLiveSamplesReceived({session->samples[0], session->samples[1]});
  REQUIRE(pumpUntil([&] { return !live.isEmpty(); }));
  REQUIRE(live.value("path3d")
              .toList()
              .back()
              .toMap()
              .value("displayLon")
              .toDouble() == -5);
  live.clear();
  map.onLiveSamplesReceived(
      {session->samples[2], session->samples[3], session->samples[4]});
  REQUIRE(pumpUntil([&] { return !live.isEmpty(); }));
  REQUIRE(live.value("replace").toBool());
  const auto aligned = live.value("path3d").toList()[1].toMap();
  REQUIRE(aligned.value("lon").toDouble() == -5);
  REQUIRE(std::abs(aligned.value("displayLon").toDouble() + 4.99975) < 1e-9);
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
  auto *toggle = dashboard.findChild<QAction *>("replayInstrumentSidebar");
  QPushButton *graph = nullptr;
  QPushButton *map = nullptr;
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
    CHECK_FALSE(toggle->isEnabled());
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
        if (!captureDir.isEmpty()) {
          REQUIRE(live.grab().save(captureDir + "/layout-" + theme + "-" +
                                   QString::number(size.width()) + ".png"));
          auto *attitude = live.findChild<QWidget *>("rocketAttitudeWidget");
          auto *horizon = live.findChild<QWidget *>("visualHorizonWidget");
          REQUIRE(attitude);
          REQUIRE(horizon);
          const auto bounds =
              QRect(attitude->mapTo(&live, QPoint()), attitude->size())
                  .united(
                      QRect(horizon->mapTo(&live, QPoint()), horizon->size()));
          REQUIRE(live.grab(bounds).save(captureDir + "/live-pair-" + theme +
                                         "-" + QString::number(size.width()) +
                                         ".png"));
        }
        REQUIRE(scroll->verticalScrollBar()->maximum() == 0);
        REQUIRE(scroll->horizontalScrollBar()->maximum() == 0);
        const auto *recovery = live.findChild<QWidget *>("recoveryPanel");
        REQUIRE(recovery);
        REQUIRE(recovery->isHidden());
        const auto *browser = live.findChild<QWebEngineView *>();
        REQUIRE(browser);
        REQUIRE(browser->height() >= browser->parentWidget()->height());
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
  const auto stateCaptureDir = qEnvironmentVariable("COSMO_TEST_SCREENSHOTS");
  if (!stateCaptureDir.isEmpty()) {
    instrument->setCompact(true);
    for (const QString &theme :
         {QStringLiteral("dark"), QStringLiteral("light")}) {
      const auto skin = cosmo::SkinLoader::loadBuiltin(":/skins/" + theme);
      REQUIRE(skin);
      cosmo::ThemeManager::instance().setActiveSkin(*skin);
      QApplication::processEvents();
      REQUIRE(instrument->grab().save(stateCaptureDir + "/attitude-stale-" +
                                      theme + ".png"));
    }
    instrument->setCompact(false);
  }
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

TEST_CASE(
    "Attitude stays readable through compact mode changes and repeated poses",
    "[attitude][attitude-design]") {
  RocketAttitudeWidget instrument;
  FlightSample sample;
  sample.angularRotation = {30, -179.9, 45};
  instrument.setSample(sample, true);
  instrument.resize(240, 204);
  instrument.show();
  QApplication::processEvents();
  auto *status = instrument.findChild<QLabel *>("attitudeStatus");
  auto *angles = instrument.findChild<QLabel *>("attitudeAngles");
  REQUIRE(status);
  REQUIRE(angles);
  const int fullSize = angles->font().pixelSize();
  instrument.setCompact(true);
  instrument.resize(136, 204);
  QApplication::processEvents();
  REQUIRE(status->isVisible());
  REQUIRE(status->text() == "Replay");
  REQUIRE(angles->text().contains("-179.9"));
  instrument.setCompact(false);
  instrument.resize(240, 204);
  QApplication::processEvents();
  REQUIRE(angles->font().pixelSize() == fullSize);

  QWidget *canvas = nullptr;
  for (auto *child : instrument.findChildren<QWidget *>())
    if (child->accessibleName().startsWith("Rocket orientation"))
      canvas = child;
  REQUIRE(canvas);
  const auto initialPose = canvas->grab().toImage();
  for (int i = 0; i < 3; ++i) {
    instrument.setSample(sample, true);
    QApplication::processEvents();
  }
  REQUIRE(canvas->grab().toImage() == initialPose);
  auto tilted = sample;
  tilted.angularRotation = {90, 0, 0};
  instrument.setSample(tilted, true);
  REQUIRE(canvas->grab().toImage() != initialPose);
  instrument.setSample(sample, true);
  REQUIRE(canvas->grab().toImage() == initialPose);

  const auto captureDir = qEnvironmentVariable("COSMO_TEST_SCREENSHOTS");
  for (const QString &theme :
       {QStringLiteral("dark"), QStringLiteral("light")}) {
    const auto skin = cosmo::SkinLoader::loadBuiltin(":/skins/" + theme);
    REQUIRE(skin);
    cosmo::ThemeManager::instance().setActiveSkin(*skin);
    for (const int width : {136, 240, 320}) {
      instrument.setCompact(width < 240);
      instrument.resize(width, 204);
      for (int pose = 0; pose < 4; ++pose) {
        sample.angularRotation =
            pose == 0   ? AngularRotationCoordinates{0, 0, 0}
            : pose == 1 ? AngularRotationCoordinates{30, -179.9, 45}
            : pose == 2 ? AngularRotationCoordinates{90, 0, 0}
                        : AngularRotationCoordinates{180, 0, 0};
        instrument.setSample(sample, true);
        QApplication::processEvents();
        CAPTURE(theme, width, pose);
        REQUIRE(instrument.width() == width);
        REQUIRE(instrument.height() == 204);
        for (auto *label : instrument.findChildren<QLabel *>())
          REQUIRE(instrument.rect().contains(
              QRect(label->mapTo(&instrument, QPoint()), label->size())));
        if (!captureDir.isEmpty())
          REQUIRE(instrument.grab().save(captureDir + "/attitude-" + theme +
                                         "-" + QString::number(width) + "-" +
                                         QString::number(pose) + ".png"));
      }
    }
    const auto lastPose = canvas->grab().toImage();
    instrument.resetDisplay();
    REQUIRE(canvas->grab().toImage() != lastPose);
    REQUIRE(angles->text().contains("—"));
    REQUIRE(status->text().isEmpty());
    if (!captureDir.isEmpty())
      REQUIRE(instrument.grab().save(captureDir + "/attitude-empty-" + theme +
                                     ".png"));
  }
}

// Run explicitly with a graphics-capable Qt platform; CTest's offscreen target
// disables GL.
TEST_CASE(
    "3D replay displays recording angles with an interpolated path and an "
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
  REQUIRE(info.contains("Estimated path"));
  REQUIRE_FALSE(info.contains("Attitude unavailable"));
  REQUIRE(evaluate(QStringLiteral(
              "String(document.querySelectorAll('#view3d canvas').length)")) ==
          "1");
  REQUIRE(evaluate(QStringLiteral(
              "String(document.getElementById('path3dOptions') === null)")) ==
          "true");
  evaluate(QStringLiteral("document.getElementById('btnFit').click();"));
  REQUIRE(
      evaluate(QStringLiteral("document.getElementById('btnFollow').click(); "
                              "document.getElementById('btnFollow')."
                              "getAttribute('aria-pressed')")) == "true");
  REQUIRE(evaluate(QStringLiteral(
              "document.querySelector('#view3d canvas').dispatchEvent("
              "new WheelEvent('wheel', {deltaY:100, clientX:400, clientY:260, "
              "cancelable:true})); "
              "document.getElementById('btnFollow').getAttribute('aria-pressed'"
              ")")) == "true");
  REQUIRE(evaluate(QStringLiteral(
              "String(document.getElementById('btnFollow').scrollWidth <= "
              "document.getElementById('btnFollow').clientWidth)")) == "true");
  const QString captureDir = qEnvironmentVariable("COSMO_TEST_SCREENSHOTS");
  if (!captureDir.isEmpty()) {
    for (const QString &theme :
         {QStringLiteral("dark"), QStringLiteral("light")}) {
      const auto skin =
          cosmo::SkinLoader::loadBuiltin(QStringLiteral(":/skins/") + theme);
      REQUIRE(skin.has_value());
      cosmo::ThemeManager::instance().setActiveSkin(*skin);
      evaluate(
          QStringLiteral("window.followRocket(false);window.fitCamera();"));
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
  SettingsPage settings;
  auto *button =
      settings.findChild<QPushButton *>(QStringLiteral("recordedImuInput"));
  REQUIRE(button);
  bool requested = false;
  QObject::connect(&settings, &SettingsPage::recordedInputRequested, &settings,
                   [&] { requested = true; });
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
  application.setQuitOnLastWindowClosed(false);
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

TEST_CASE("Recovery and horizon remain compact through theme changes",
          "[mission-ui][recovery][navball]") {
  RecoveryPanel recovery;
  VisualHorizonWidget horizon;
  const QJsonObject record{
      {"lat", 51.9},
      {"lon", -8.5},
      {"receivedUtcMs", 1000.0},
      {"earlier", true},
      {"source", "Serial"},
      {"session", "visual-check"},
      {"reference", QJsonObject{{"lat", 51.899},
                                {"lon", -8.501},
                                {"name", "launch point"}}}};
  recovery.setRecord(record);
  recovery.modeAction()->setChecked(true);
  recovery.resize(420, recovery.sizeHint().height());
  auto *position = recovery.findChild<QLabel *>("lastKnownPosition");
  REQUIRE(position);
  REQUIRE(position->text().contains("Earlier session"));
  REQUIRE(position->text().contains("launch point"));
  const QString captureDir = qEnvironmentVariable("COSMO_TEST_SCREENSHOTS");
  for (const QString &theme :
       {QStringLiteral("dark"), QStringLiteral("light")}) {
    const auto skin = cosmo::SkinLoader::loadBuiltin(":/skins/" + theme);
    REQUIRE(skin.has_value());
    cosmo::ThemeManager::instance().setActiveSkin(*skin);
    recovery.show();
    horizon.resize(150, 204);
    horizon.show();
    FlightSample pose;
    for (int angle : {0, 45, 90, 180}) {
      pose.angularRotation = {static_cast<double>(angle), 0, 0};
      horizon.setSample(pose, true);
      QApplication::processEvents();
      if (angle == 45) {
        const auto rendered = horizon.grab().toImage();
        const auto scale = rendered.devicePixelRatio();
        const auto sky =
            rendered.pixelColor(qRound(75 * scale), qRound(87 * scale));
        REQUIRE(sky.blue() > sky.red());
      }
      if (!captureDir.isEmpty())
        REQUIRE(horizon.grab().save(captureDir + "/horizon-" + theme + "-" +
                                    QString::number(angle) + ".png"));
    }
    if (!captureDir.isEmpty())
      REQUIRE(recovery.grab().save(captureDir + "/recovery-" + theme + ".png"));
  }
}

TEST_CASE("Top mission controls follow the active page without duplicates",
          "[mission-ui][mission-controls]") {
  MainWindow window;
  window.resize(1024, 650);
  window.show();
  auto *menu = window.findChild<QMenu *>("instrumentsMenu");
  REQUIRE(menu);
  const auto menus = window.menuBar()->actions();
  const int viewIndex =
      menus.indexOf(window.findChild<QMenu *>("viewMenu")->menuAction());
  REQUIRE(menus.indexOf(menu->menuAction()) == viewIndex + 1);
  auto *recovery = window.findChild<QToolButton *>("missionRecovery");
  auto *open = window.findChild<QPushButton *>("openFlightLog");
  REQUIRE(recovery);
  REQUIRE(open);
  REQUIRE(recovery->parentWidget() == open->parentWidget());
  REQUIRE(recovery->geometry().right() < open->geometry().left());
  auto *dashboard = window.findChild<DashboardPage *>();
  auto *live = window.findChild<LiveTelemetryPage *>();
  auto *pages = qobject_cast<QStackedWidget *>(dashboard->parentWidget());
  REQUIRE(pages);
  REQUIRE_FALSE(recovery->isEnabled());
  auto session = std::make_shared<FlightSession>();
  FlightSample sample;
  sample.angularRotation = {30, 0, 45};
  sample.coordinates = {51.9, -8.5};
  session->samples.push_back(sample);
  dashboard->setReplaySession(
      session, cosmo::preview::FlightPreviewCache::build(*session));
  REQUIRE(recovery->isEnabled());
  const auto actionNamed = [&](const QString &text) -> QAction * {
    for (auto *action : menu->actions())
      if (action->text() == text)
        return action;
    return nullptr;
  };
  auto *replayHorizon = actionNamed("Visual horizon");
  REQUIRE(replayHorizon);
  replayHorizon->trigger();
  REQUIRE(dashboard->findChild<QWidget *>("visualHorizonWidget")->isHidden());
  recovery->click();
  auto *replayDetails = dashboard->findChild<QWidget *>("recoveryPanel");
  REQUIRE(replayDetails->isVisible());
  REQUIRE(recovery->isChecked());
  pages->setCurrentWidget(live);
  auto *liveHorizon = actionNamed("Visual horizon");
  REQUIRE(liveHorizon);
  REQUIRE(liveHorizon != replayHorizon);
  REQUIRE(recovery->actions().size() == 1);
  REQUIRE(liveHorizon->isChecked());
  REQUIRE_FALSE(recovery->isChecked());
  recovery->click();
  REQUIRE(live->findChild<QWidget *>("recoveryPanel")->isVisible());
  pages->setCurrentWidget(dashboard);
  REQUIRE(recovery->isChecked());
  REQUIRE_FALSE(replayHorizon->isChecked());
  for (auto *map : window.findChildren<Map3DWidget *>())
    REQUIRE(map->findChildren<QToolButton *>("recoveryMode").isEmpty());
  for (auto *button : live->findChildren<QToolButton *>())
    REQUIRE(button->text() != "Instruments");
  for (auto *button : dashboard->findChildren<QToolButton *>())
    REQUIRE(button->text() != "Instruments");
  const QString captureDir = qEnvironmentVariable("COSMO_TEST_SCREENSHOTS");
  for (const QString &theme :
       {QStringLiteral("dark"), QStringLiteral("light")}) {
    const auto skin = cosmo::SkinLoader::loadBuiltin(":/skins/" + theme);
    REQUIRE(skin);
    cosmo::ThemeManager::instance().setActiveSkin(*skin);
    for (const auto size :
         {QSize(920, 650), QSize(1024, 650), QSize(1440, 800)}) {
      window.resize(size);
      for (auto *page :
           {static_cast<QWidget *>(dashboard), static_cast<QWidget *>(live)}) {
        pages->setCurrentWidget(page);
        if (recovery->isChecked())
          recovery->click();
        QApplication::processEvents();
        CAPTURE(theme, size.width(), page == live);
        REQUIRE(window.width() == size.width());
        REQUIRE(recovery->geometry().right() < open->geometry().left());
        REQUIRE(
            recovery->parentWidget()->rect().contains(recovery->geometry()));
        REQUIRE(recovery->parentWidget()->rect().contains(open->geometry()));
        if (!captureDir.isEmpty())
          REQUIRE(window.grab().save(captureDir + "/mission-controls-" + theme +
                                     (page == live ? "-live-" : "-replay-") +
                                     QString::number(size.width()) + ".png"));
      }
    }
  }
  pages->setCurrentIndex(2);
  REQUIRE_FALSE(recovery->isVisible());
  REQUIRE_FALSE(menu->menuAction()->isVisible());
}

TEST_CASE("Replay instruments follow Graph and Map layouts without recreating "
          "widgets",
          "[mission-ui][instrument-layout]") {
  FlightDataModel model;
  FlightReplayController replay;
  DashboardPage dashboard(&model, &replay);
  LiveTelemetryPage live(&model);
  auto session = std::make_shared<FlightSession>();
  FlightSample sample;
  sample.angularRotation = {30, 0, 45};
  sample.coordinates = {51.9, -8.5};
  session->samples.push_back(sample);
  sample.timestamp = 1000;
  session->samples.push_back(sample);
  auto preview = cosmo::preview::FlightPreviewCache::build(*session);
  model.setReplayMode(true);
  replay.setSession(session, preview);
  dashboard.setReplaySession(session, preview);
  auto *horizon = dashboard.findChild<QWidget *>("visualHorizonWidget");
  auto *attitude = dashboard.findChild<QWidget *>("rocketAttitudeWidget");
  REQUIRE(horizon);
  REQUIRE(attitude);
  QPushButton *graph = nullptr, *map = nullptr;
  for (auto *button : dashboard.findChildren<QPushButton *>()) {
    if (button->accessibleName() == "Show telemetry chart")
      graph = button;
    if (button->accessibleName() == "Show flight path map")
      map = button;
  }
  REQUIRE(graph);
  REQUIRE(map);
  bool mapNetworkBlocked = false;
  const auto captureDir = qEnvironmentVariable("COSMO_TEST_SCREENSHOTS");
  for (const QString &theme :
       {QStringLiteral("dark"), QStringLiteral("light")}) {
    const auto skin = cosmo::SkinLoader::loadBuiltin(":/skins/" + theme);
    REQUIRE(skin);
    cosmo::ThemeManager::instance().setActiveSkin(*skin);
    for (const auto size :
         {QSize(920, 650), QSize(1024, 650), QSize(1440, 800)}) {
      dashboard.resize(size);
      dashboard.show();
      for (bool mapMode : {false, true, false}) {
        (mapMode ? map : graph)->click();
        if (!mapNetworkBlocked)
          if (auto *browser = dashboard.findChild<QWebEngineView *>()) {
            auto block = std::make_unique<BlockNetwork>();
            block->setParent(browser->page()->profile());
            browser->page()->profile()->setUrlRequestInterceptor(
                block.release());
            mapNetworkBlocked = true;
          }
        QApplication::processEvents();
        CAPTURE(theme, size.width(), mapMode);
        REQUIRE(dashboard.width() == size.width());
        REQUIRE(dashboard.height() == size.height());
        if (mapMode) {
          REQUIRE(horizon->geometry().bottom() < attitude->geometry().top());
          REQUIRE(horizon->geometry().left() == attitude->geometry().left());
        } else {
          REQUIRE(horizon->geometry().right() < attitude->geometry().left());
          REQUIRE(horizon->geometry().top() == attitude->geometry().top());
        }
        for (auto *instrument : {horizon, attitude})
          REQUIRE(dashboard.rect().contains(QRect(
              instrument->mapTo(&dashboard, QPoint()), instrument->size())));
        if (!captureDir.isEmpty()) {
          REQUIRE(dashboard.grab().save(captureDir + "/instruments-" + theme +
                                        (mapMode ? "-map-" : "-graph-") +
                                        QString::number(size.width()) +
                                        ".png"));
          const auto bounds =
              QRect(attitude->mapTo(&dashboard, QPoint()), attitude->size())
                  .united(QRect(horizon->mapTo(&dashboard, QPoint()),
                                horizon->size()));
          REQUIRE(dashboard.grab(bounds).save(
              captureDir + "/replay-pair-" + theme +
              (mapMode ? "-map-" : "-graph-") + QString::number(size.width()) +
              ".png"));
        }
      }
      dashboard.hide();
    }
  }
  dashboard.setReplaySession(nullptr, nullptr);
  REQUIRE(horizon->isHidden());
  REQUIRE(attitude->isHidden());
  live.resize(920, 650);
  live.show();
  QApplication::processEvents();
  auto *liveHorizon = live.findChild<QWidget *>("visualHorizonWidget");
  auto *liveAttitude = live.findChild<QWidget *>("rocketAttitudeWidget");
  REQUIRE(liveHorizon->geometry().right() < liveAttitude->geometry().left());
  REQUIRE(liveHorizon->geometry().top() == liveAttitude->geometry().top());
}

TEST_CASE("Theme arrows are compact open chevrons in every direction",
          "[mission-ui][chevrons]") {
  for (const QString &theme :
       {QStringLiteral("dark"), QStringLiteral("light")}) {
    const auto skin = cosmo::SkinLoader::loadBuiltin(":/skins/" + theme);
    REQUIRE(skin);
    cosmo::ThemeManager::instance().setActiveSkin(*skin);
    for (auto direction :
         {QStyle::PE_IndicatorArrowDown, QStyle::PE_IndicatorArrowUp,
          QStyle::PE_IndicatorArrowRight, QStyle::PE_IndicatorArrowLeft}) {
      QImage image(48, 48, QImage::Format_ARGB32_Premultiplied);
      image.fill(Qt::transparent);
      QStyleOption option;
      option.rect = image.rect();
      option.state = QStyle::State_Enabled;
      option.palette = QApplication::palette();
      QPainter painter(&image);
      QApplication::style()->drawPrimitive(direction, &option, &painter);
      painter.end();
      CAPTURE(theme, direction);
      // Allow antialiasing at the inside edge, while rejecting filled arrows.
      REQUIRE(image.pixelColor(24, 24).alpha() < 64);
      QRect painted;
      for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
          if (image.pixelColor(x, y).alpha() > 20)
            painted = painted.united(QRect(x, y, 1, 1));
      REQUIRE_FALSE(painted.isEmpty());
      REQUIRE(painted.width() <= 12);
      REQUIRE(painted.height() <= 12);
    }
    QComboBox selector;
    selector.addItem("Receiver");
    selector.setFixedSize(220, 32);
    selector.show();
    QApplication::processEvents();
    QStyleOptionComboBox comboOption;
    comboOption.initFrom(&selector);
    const QRect arrow = selector.style()->subControlRect(
        QStyle::CC_ComboBox, &comboOption, QStyle::SC_ComboBoxArrow, &selector);
    const auto rendered = selector.grab().toImage();
    const auto scale = rendered.devicePixelRatio();
    const auto background = rendered.pixelColor(
        qRound((arrow.left() + 2) * scale), qRound(arrow.center().y() * scale));
    int foregroundPixels = 0;
    const auto inside = arrow.adjusted(3, 3, -3, -3);
    for (int y = inside.top(); y <= inside.bottom(); ++y)
      for (int x = inside.left(); x <= inside.right(); ++x) {
        const auto color =
            rendered.pixelColor(qRound(x * scale), qRound(y * scale));
        if (std::abs(color.red() - background.red()) +
                std::abs(color.green() - background.green()) +
                std::abs(color.blue() - background.blue()) >
            120)
          ++foregroundPixels;
      }
    REQUIRE(foregroundPixels > 5);
    const auto captureDir = qEnvironmentVariable("COSMO_TEST_SCREENSHOTS");
    if (!captureDir.isEmpty()) {
      FlightDataModel model;
      LiveTelemetryPage live(&model);
      live.setAvailablePorts({"USB telemetry receiver"});
      live.resize(1024, 800);
      live.show();
      if (auto *browser = live.findChild<QWebEngineView *>()) {
        auto block = std::make_unique<BlockNetwork>();
        block->setParent(browser->page()->profile());
        browser->page()->profile()->setUrlRequestInterceptor(block.release());
      }
      for (bool expanded : {false, true}) {
        for (auto *button : live.findChildren<QToolButton *>())
          if (button->text() == "Link details" || button->text() == "Preflight")
            button->setChecked(expanded);
        QApplication::processEvents();
        REQUIRE(
            live.grab().save(captureDir + "/chevrons-" + theme +
                             (expanded ? "-expanded.png" : "-collapsed.png")));
      }
    }
  }
}
