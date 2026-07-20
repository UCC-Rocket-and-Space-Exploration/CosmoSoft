#include "gui/MainWindow.h"

#include "domain/FlightSample.h"
#include "domain/FlightSession.h"
#include "gateway/comms/CommsFactory.h"
#include "gateway/comms/ISerialPortScanner.h"
#include "gateway/comms/SerialPortScannerFactory.h"
#include "gateway/comms/SerialWorker.h"
#include "gui/AboutDialog.h"
#include "gui/FlightDataModel.h"
#include "gui/FlightReplayController.h"
#include "gui/LayoutHelpers.h"
#include "gui/SettingsKeys.h"
#include "gui/Theme.h"
#include "gui/ThemeManager.h"
#include "gui/pages/DashboardPage.h"
#include "gui/pages/EventLogPage.h"
#include "gui/pages/LiveTelemetryPage.h"
#include "gui/pages/SettingsPage.h"
#include "services/import/SampleFileLoader.h"
#include "services/flight/FakeFlightLink.h"
#include "services/persistence/FlightLogManager.h"
#include "services/telemetry/Framer.h"
#include "services/telemetry/LineTelemetryBatchMailbox.h"
#include "services/telemetry/LineTelemetryDecodeWorker.h"
#include "services/telemetry/Parser.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QFont>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QResizeEvent>
#include <QSettings>
#include <QStringList>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMetaObject>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>

#include <QtConcurrent/QtConcurrentRun>

#include <atomic>
#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace Qt::StringLiterals;

namespace {

struct FlightLogLoadResult {
    std::optional<std::string> error;
    std::shared_ptr<const FlightSession> session;
    std::shared_ptr<const cosmo::preview::FlightPreviewCache> preview;
    bool canceled = false;
};

struct SerialPortScanResult {
    std::optional<std::string> error;
    std::vector<std::string> ports;
    bool canceled = false;
};

[[nodiscard]] bool cancellationRequested(const std::shared_ptr<std::atomic_bool> &flag) {
    return flag && flag->load(std::memory_order_relaxed);
}

[[nodiscard]] FlightLogLoadResult loadFlightLogAtPath(
    const QString &path,
    const std::shared_ptr<std::atomic_bool> &cancelFlag) {
    FlightLogLoadResult r;
    if (cancellationRequested(cancelFlag)) {
        r.canceled = true;
        return r;
    }

    try {
        const cosmo::CancellationCheck cancellationCheck = [cancelFlag] {
            return cancellationRequested(cancelFlag);
        };
        FlightSession session;
        SampleFileLoader::LoadResult loadResult;
        if (path.endsWith(u".telem", Qt::CaseInsensitive)) {
            Framer framer;
            Parser parser;
            loadResult = SampleFileLoader::loadTelemFile(
                path.toStdString(), session, framer, parser, cancellationCheck);
        } else if (path.endsWith(u".xlsx", Qt::CaseInsensitive)) {
            loadResult = SampleFileLoader::loadXlsx(
                path.toStdString(), session, cancellationCheck);
        } else {
            loadResult = SampleFileLoader::loadTheseusCsv(
                path.toStdString(), session, cancellationCheck);
        }
        r.error = std::move(loadResult.error);
        if (loadResult.canceled || cancellationRequested(cancelFlag)) {
            r.canceled = true;
            return r;
        }
        if (!r.error) {
            auto loadedSession = std::make_shared<FlightSession>(std::move(session));
            r.preview = cosmo::preview::FlightPreviewCache::build(
                *loadedSession, cancellationCheck);
            if (!r.preview || cancellationRequested(cancelFlag)) {
                r.preview.reset();
                r.canceled = true;
                return r;
            }
            r.session = std::move(loadedSession);
        }
    } catch (const std::exception &e) {
        r.error = std::string("Unexpected error while loading flight log: ") + e.what();
    } catch (const std::string &message) {
        r.error = message;
    } catch (const char *message) {
        r.error = message ? message : "Unexpected error while loading flight log.";
    }
    return r;
}

[[nodiscard]] SerialPortScanResult scanSerialPorts(
    const std::shared_ptr<std::atomic_bool> &cancelFlag) {
    SerialPortScanResult result;
    if (cancellationRequested(cancelFlag)) {
        result.canceled = true;
        return result;
    }

    try {
        auto scanner = SerialPortScannerFactory::createSerialPortScanner();
        if (scanner) {
            result.ports = scanner->enumeratePorts();
        }
    } catch (const std::exception &e) {
        result.error = e.what();
    } catch (const std::string &message) {
        result.error = message;
    } catch (const char *message) {
        result.error = message ? message : "Serial device scan failed.";
    }

    result.canceled = cancellationRequested(cancelFlag);
    if (result.canceled) {
        result.ports.clear();
    }
    return result;
}

[[nodiscard]] bool exportFlightSessionAtPath(
    const QString &path,
    const std::shared_ptr<const FlightSession> &session) {
    if (!session) {
        return false;
    }
    try {
        FlightLogManager manager;
        manager.setOutputPath(path.toStdString());
        return manager.exportSessionToCsv(*session);
    } catch (const std::exception &) {
        return false;
    }
}

QPushButton* createActionButton(QWidget *parent, const QString &tooltip, const QString &iconName) {
    auto *btn = new QPushButton(parent);
    btn->setProperty("kind", "actionButton");
    btn->setToolTip(tooltip);
    btn->setAccessibleName(tooltip);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFocusPolicy(Qt::TabFocus);

    // Use Qt standard icons as placeholders
    QStyle::StandardPixmap iconType = QStyle::SP_FileIcon;
    if (iconName == u"folder-open"_s) {
        iconType = QStyle::SP_DirOpenIcon;
    } else if (iconName == u"trash"_s) {
        iconType = QStyle::SP_TrashIcon;
    } else if (iconName == u"export"_s) {
        iconType = QStyle::SP_DialogSaveButton;
    } else if (iconName == u"play"_s) {
        iconType = QStyle::SP_MediaPlay;
    } else if (iconName == u"stop"_s) {
        iconType = QStyle::SP_MediaStop;
    }

    QIcon icon = btn->style()->standardIcon(iconType);
    btn->setIcon(icon);
    btn->setIconSize(QSize(20, 20));

    return btn;
}

[[nodiscard]] double relativeLuminance(const QColor &color) {
    const auto linearChannel = [](const double channel) {
        return channel <= 0.04045
            ? channel / 12.92
            : std::pow((channel + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linearChannel(color.redF())
        + 0.7152 * linearChannel(color.greenF())
        + 0.0722 * linearChannel(color.blueF());
}

[[nodiscard]] QString settingsIconResource(const QString &backgroundColor) {
    const QColor background(backgroundColor);
    if (!background.isValid()) {
        return u":/icons/settings_button.png"_s;
    }

    // Choose the black or white asset according to whichever has the stronger
    // WCAG contrast against the current palette surface.
    constexpr double kEqualContrastLuminance = 0.179;
    return relativeLuminance(background) > kEqualContrastLuminance
        ? u":/icons/settings_button_black.png"_s
        : u":/icons/settings_button.png"_s;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_flightModel(std::make_unique<FlightDataModel>(this)),
      m_replay(std::make_unique<FlightReplayController>(this)),
      m_logManager(std::make_unique<FlightLogManager>()),
      m_liveBatchMailbox(
          std::make_unique<cosmo::telemetry::LineTelemetryBatchMailbox>()) {
    setWindowTitle(u"CosmoSoft"_s);
    setWindowIcon(QIcon(u":/images/Logo_rounded.png"_s));
    m_uiSoundsEnabled = QSettings(kSettingsOrg, kSettingsApp)
                            .value(kSettingsSoundsEnabled, true)
                            .toBool();

    setupActions();
    setupMenuBar();
    setupToolbar();
    setupPages();

    m_fakeTransmissionTimer = new QTimer(this);
    m_fakeTransmissionTimer->setInterval(250);
    connect(m_fakeTransmissionTimer, &QTimer::timeout, this, &MainWindow::pushFakeTransmissionSample);

    connect(m_replay.get(), &FlightReplayController::positionChanged, this, &MainWindow::onReplayPositionChanged);

    m_replayTelemetryCoalesceTimer = new QTimer(this);
    m_replayTelemetryCoalesceTimer->setSingleShot(true);
    m_replayTelemetryCoalesceTimer->setInterval(50);
    connect(m_replayTelemetryCoalesceTimer, &QTimer::timeout, this, &MainWindow::applyPendingReplayTelemetryStrip);

    const auto flushReplayTelemetryStrip = [this]() {
        if (m_replayTelemetryCoalesceTimer) {
            m_replayTelemetryCoalesceTimer->stop();
        }
        if (!m_flightDataPage || !m_flightDataPage->isVisible()) {
            return;
        }
        const int idx = m_replay ? m_replay->index() : 0;
        if (idx <= 0) {
            m_flightModel->setDisplayedSample(FlightSample{});
        } else {
            applyReplayTelemetrySample(idx);
        }
    };
    connect(m_replay.get(), &FlightReplayController::playbackPaused, this, flushReplayTelemetryStrip);
    connect(m_replay.get(), &FlightReplayController::playbackStopped, this, flushReplayTelemetryStrip);
    connect(m_replay.get(), &FlightReplayController::playbackFinished, this, flushReplayTelemetryStrip);

    connect(&cosmo::ThemeManager::instance(), &cosmo::ThemeManager::themeChanged,
            this, &MainWindow::onThemeChanged);

    statusBar()->showMessage(u"DO NOT FORGET TO CONNECT WIFI AND CABLE TO ROCKET."_s);

    if (const auto geom = QSettings(kSettingsOrg, kSettingsApp).value(kSettingsWindowMainGeo).toByteArray(); !geom.isEmpty())
        restoreGeometry(geom);
}

MainWindow::~MainWindow() {
    cancelFlightLogLoad(false);
    ++m_exportGeneration;
    ++m_scanGeneration;
    if (m_scanCancelFlag) {
        m_scanCancelFlag->store(true, std::memory_order_relaxed);
        m_scanCancelFlag.reset();
    }
    QSettings(kSettingsOrg, kSettingsApp).setValue(kSettingsWindowMainGeo, saveGeometry());
    stopFakeTransmission(false);
    stopSerial();
}

void MainWindow::showStatusMessage(const QString &message, int timeout) {
    if (auto *sb = statusBar()) {
        sb->showMessage(message, timeout);
    }
}

void MainWindow::onParserError(const QString &message) {
    showStatusMessage(message, 5000);
    appendToLog(true, message);
}

void MainWindow::appendToLog(bool isError, const QString &text) {
    if (m_eventLogPage) {
        if (isError) {
            m_eventLogPage->appendError(text);
        } else {
            m_eventLogPage->appendEntry(text);
        }
    }
    if (isError) {
        playErrorFeedback();
    }
}

void MainWindow::playErrorFeedback() {
    if (!m_uiSoundsEnabled) {
        return;
    }

    constexpr auto kMinimumAlertInterval = std::chrono::milliseconds(1500);
    const auto now = std::chrono::steady_clock::now();
    if (m_hasPlayedErrorFeedback
        && now - m_lastErrorFeedback < kMinimumAlertInterval) {
        return;
    }

    QApplication::beep();
    m_lastErrorFeedback = now;
    m_hasPlayedErrorFeedback = true;
}

void MainWindow::setupActions() {
    m_openSettingsAction = new QAction(u"Preferences…"_s, this);
    m_openSettingsAction->setToolTip(u"Open application preferences."_s);
    m_openSettingsAction->setMenuRole(QAction::PreferencesRole);
    m_openSettingsAction->setShortcut(QKeySequence::Preferences);
    m_openSettingsAction->setShortcutContext(Qt::ApplicationShortcut);
    m_openSettingsAction->setCheckable(true);
    updateSettingsIcon();

    connect(m_openSettingsAction, &QAction::triggered, this, [this]() { openSettingsWindow(); });

    auto *pageGroup = new QActionGroup(this);
    pageGroup->setExclusive(true);

    m_dashboardAction = new QAction(u"Dashboard"_s, this);
    m_dashboardAction->setToolTip(u"Show the flight replay dashboard."_s);
    m_dashboardAction->setShortcut(QKeySequence(u"Ctrl+1"_s));
    m_dashboardAction->setCheckable(true);
    m_dashboardAction->setChecked(true);
    pageGroup->addAction(m_dashboardAction);

    m_liveTelemetryAction = new QAction(u"Live Telemetry"_s, this);
    m_liveTelemetryAction->setToolTip(u"Show live telemetry."_s);
    m_liveTelemetryAction->setShortcut(QKeySequence(u"Ctrl+2"_s));
    m_liveTelemetryAction->setCheckable(true);
    pageGroup->addAction(m_liveTelemetryAction);

    m_eventLogAction = new QAction(u"Event Log"_s, this);
    m_eventLogAction->setToolTip(u"Show session events and errors."_s);
    m_eventLogAction->setShortcut(QKeySequence(u"Ctrl+3"_s));
    m_eventLogAction->setCheckable(true);
    pageGroup->addAction(m_eventLogAction);
}

void MainWindow::setupMenuBar() {
    auto *mb = menuBar();

    auto *fileMenu = mb->addMenu(u"&File"_s);
    fileMenu->addAction(u"&Open log…"_s, QKeySequence::Open, this, &MainWindow::onOpenReplayFile);
    m_recentFilesMenu = fileMenu->addMenu(u"Open &Recent"_s);
    rebuildRecentFilesMenu();
    fileMenu->addSeparator();
    fileMenu->addAction(u"&Export session…"_s, QKeySequence(u"Ctrl+Shift+E"_s), this, &MainWindow::onExportSession);
    fileMenu->addSeparator();
    fileMenu->addAction(u"&Clear flight"_s, this, &MainWindow::onClearFlightData);
    fileMenu->addSeparator();
    fileMenu->addAction(u"&Quit"_s, QKeySequence::Quit, qApp, &QApplication::quit);

    auto *viewMenu = mb->addMenu(u"&View"_s);
    viewMenu->addAction(m_dashboardAction);
    viewMenu->addAction(m_liveTelemetryAction);
    viewMenu->addAction(m_eventLogAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_openSettingsAction);

    auto *helpMenu = mb->addMenu(u"&Help"_s);
    helpMenu->addAction(u"&About CosmoSoft…"_s, this, &MainWindow::onShowAbout);
}

void MainWindow::addRecentFile(const QString &path) {
    QSettings s(kSettingsOrg, kSettingsApp);
    QStringList recent = s.value(kSettingsRecentFiles).toStringList();
    recent.removeAll(path);
    recent.prepend(path);
    constexpr int kMaxRecentFiles = 10;
    while (recent.size() > kMaxRecentFiles) {
        recent.removeLast();
    }
    s.setValue(kSettingsRecentFiles, recent);
    rebuildRecentFilesMenu();
}

void MainWindow::rebuildRecentFilesMenu() {
    if (!m_recentFilesMenu) {
        return;
    }
    m_recentFilesMenu->clear();
    QSettings s(kSettingsOrg, kSettingsApp);
    const QStringList recent = s.value(kSettingsRecentFiles).toStringList();
    if (recent.isEmpty()) {
        m_recentFilesMenu->addAction(u"(no recent files)"_s)->setEnabled(false);
        return;
    }
    for (const QString &filePath : recent) {
        const QString display = QFileInfo(filePath).fileName();
        m_recentFilesMenu->addAction(display, this, [this, filePath]() {
            if (!QFileInfo::exists(filePath)) {
                const QString message = QStringLiteral("File not found: %1").arg(filePath);
                showStatusMessage(message, 4000);
                appendToLog(true, message);
                return;
            }
            QSettings rs(kSettingsOrg, kSettingsApp);
            rs.setValue(kSettingsReplayDir, QFileInfo(filePath).absolutePath());
            stopFakeTransmission(false);
            stopSerial();
            loadFlightLogAsync(filePath);
        });
    }
    m_recentFilesMenu->addSeparator();
    m_recentFilesMenu->addAction(u"Clear Recent"_s, this, [this]() {
        QSettings cs(kSettingsOrg, kSettingsApp);
        cs.remove(kSettingsRecentFiles);
        rebuildRecentFilesMenu();
    });
}

QString MainWindow::buildToolbarStyleSheet() {
    const auto bgPanel = Theme::kBgPanel();
    const auto textPri = Theme::kTextPrimary();
    const auto textMid = Theme::kTextMid();
    const auto btnBg = Theme::kBgButton();
    const auto btnHov = Theme::kBtnHover();
    const auto borderDef = Theme::kBorderDefault();
    const auto borderLight = Theme::kBorderLight();
    const auto accent = Theme::kAccentLink();
    const auto textDim = Theme::kTextDim();

    return QString(uR"(
        QToolBar#missionToolbar {
            background: %1;
            padding: 10px 10px;
            border: none;
        }
        QWidget#toolbarContent {
            background: transparent;
            margin: 0;
        }
        QWidget#brandBlock QLabel#brandLabel {
            font-size: 26px;
            font-weight: 500;
            font-family: %2;
            letter-spacing: 0.05em;
            color: %3;
        }
        QWidget#brandBlock QLabel#missionMeta {
            font-size: 14px;
            color: %4;
            font-family: %5;
        }
        QLabel#missionPageTitle {
            font-size: 15px;
            font-weight: 600;
            color: %4;
            letter-spacing: 0.08em;
            font-family: %5;
        }
        QToolButton[kind="navButton"] {
            font-size: %6px;
            min-width: 150px;
            padding: 5px 8px;
            border: 2px solid %7;
            border-radius: 0;
            background-color: %8;
            color: %3;
            letter-spacing: 1px;
            font-family: %5;
        }
        QToolButton[kind="navButton"]:hover {
            background-color: %9;
        }
    )"_s)
        .arg(bgPanel)               // %1
        .arg(Theme::kFontDisplay)   // %2
        .arg(textPri)               // %3
        .arg(textMid)               // %4
        .arg(Theme::kFontMono)      // %5
        .arg(Theme::kFontSizeBase)  // %6
        .arg(borderDef)             // %7
        .arg(btnBg)                 // %8
        .arg(btnHov)                // %9
    + QString(uR"(
        QToolButton[kind="navButton"]:checked {
            background-color: %1;
            color: %2;
            border-color: %3;
        }
        QToolButton[kind="navButton"]:disabled {
            color: %4;
            border-color: %5;
            background-color: transparent;
        }
        QToolButton[kind="iconButton"] {
            min-width: 30px;
            min-height: 30px;
            border: none;
            background-color: transparent;
        }
        QToolButton[kind="iconButton"]:hover {
            background-color: %6;
        }
        QToolButton[kind="iconButton"]:checked {
            background-color: %7;
        }
        QToolBar#missionToolbar[compact="true"] QToolButton[kind="navButton"] {
            min-width: 76px;
            padding-left: 6px;
            padding-right: 6px;
        }
        QToolBar#missionToolbar[narrow="true"] QToolButton[kind="navButton"] {
            min-width: 62px;
        }
    )"_s)
        .arg(accent)                // %1
        .arg(Theme::kBgBase())      // %2
        .arg(accent)                // %3
        .arg(textDim)               // %4
        .arg(borderLight)           // %5
        .arg(btnHov)                // %6
        .arg(btnBg)                 // %7
    + QString(uR"(
        QToolButton[kind="navButton"]:focus,
        QToolButton[kind="iconButton"]:focus {
            border: 2px solid %1;
        }
    )"_s)
        .arg(Theme::kFocusRing());
}

QString MainWindow::buildActionBarStyleSheet() {
    return QString(uR"(
        QWidget#connectionStrip {
            background: %1;
            color: %2;
            border-bottom: 1px solid %3;
            padding: 8px 16px;
        }
        QLabel#connectionStripContext {
            font-size: %4px;
            color: %5;
            font-family: %6;
            font-weight: 500;
        }
        QPushButton[kind="actionButton"] {
            min-width: 32px;
            min-height: 32px;
            max-width: 32px;
            max-height: 32px;
            border: none;
            border-radius: 4px;
            background-color: transparent;
            padding: 4px;
        }
        QPushButton[kind="actionButton"]:hover {
            background-color: %7;
        }
        QPushButton[kind="actionButton"]:pressed {
            background-color: %8;
        }
        QPushButton[kind="actionButton"]:focus {
            border: 2px solid %9;
        }
    )"_s)
        .arg(Theme::kBgBase())          // %1 - lighter than toolbar
        .arg(Theme::kTextPrimary())     // %2
        .arg(Theme::kBorderSubtle())    // %3
        .arg(Theme::kFontSizeBase)      // %4
        .arg(Theme::kTextMid())         // %5
        .arg(Theme::kFontMono)          // %6
        .arg(Theme::kBtnHover())        // %7
        .arg(Theme::kBtnPressed())      // %8
        .arg(Theme::kFocusRing());      // %9
}

void MainWindow::setupToolbar() {
    auto *toolbar = new QToolBar(u"Mission Toolbar"_s, this);
    m_missionToolbar = toolbar;
    toolbar->setObjectName(u"missionToolbar"_s);
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    toolbar->setAllowedAreas(Qt::TopToolBarArea);
    toolbar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    toolbar->setStyleSheet(buildToolbarStyleSheet());
    addToolBar(Qt::TopToolBarArea, toolbar);

    m_brandShadow = new QGraphicsDropShadowEffect(this);
    m_brandShadow->setBlurRadius(5);
    m_brandShadow->setOffset(1, 1);
    updateBrandShadowColor();

    auto *content = new QWidget(toolbar);
    m_toolbarContent = content;
    content->setObjectName(u"toolbarContent"_s);
    content->setMinimumWidth(0);
    content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *contentLayout = new QHBoxLayout(content);
    LayoutHelpers::setZeroMargins(contentLayout);
    contentLayout->setSpacing(Theme::kSpaceXl);

    auto *brandBlock = new QWidget(content);
    m_brandBlock = brandBlock;
    brandBlock->setObjectName(u"brandBlock"_s);
    brandBlock->setGraphicsEffect(m_brandShadow);
    auto *brandLayout = new QVBoxLayout(brandBlock);
    brandLayout->setContentsMargins(0, 0, 0, 0);
    brandLayout->setSpacing(2);
    m_brandLabel = new QLabel(
        QString(u"Cosmo<span style=\"color:%1\">Soft</span>"_s).arg(Theme::kAccentLink()), brandBlock);
    m_brandLabel->setObjectName(u"brandLabel"_s);
    m_brandLabel->setTextFormat(Qt::RichText);
    const QVariant workbenchFamily = qApp->property("workbenchFontFamily");
    if (workbenchFamily.isValid()) {
        QFont brandFont = m_brandLabel->font();
        brandFont.setFamily(workbenchFamily.toString());
        brandFont.setPointSize(26);
        brandFont.setBold(true);
        m_brandLabel->setFont(brandFont);
    }
    brandLayout->addWidget(m_brandLabel);

    m_missionMetaLabel = new QLabel(u"GMT: --:--:-- | -- --- ----"_s, brandBlock);
    m_missionMetaLabel->setObjectName(u"missionMeta"_s);
    m_missionMetaLabel->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    brandLayout->addWidget(m_missionMetaLabel);
    contentLayout->addWidget(brandBlock);

    updateMissionClock();
    if (!m_missionClockTimer) {
        m_missionClockTimer = new QTimer(this);
        m_missionClockTimer->setInterval(1000);
        connect(m_missionClockTimer, &QTimer::timeout, this, &MainWindow::updateMissionClock);
        m_missionClockTimer->start();
    }

    contentLayout->addStretch(1);

    auto makeNavButton = [](QAction *action,
                          QWidget *parent,
                          Qt::ToolButtonStyle style = Qt::ToolButtonTextOnly,
                          const QString &kind = u"navButton"_s,
                          const QSize &iconSize = QSize()) {
        auto *button = new QToolButton(parent);
        button->setProperty("kind", kind);
        button->setAutoRaise(false);
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::TabFocus);
        button->setDefaultAction(action);
        button->setAccessibleName(action->text());
        button->setToolTip(action->toolTip());
        button->setToolButtonStyle(style);
        if (iconSize.isValid()) {
            button->setIconSize(iconSize);
        }
        return button;
    };

    auto *navContainer = new QWidget(content);
    auto *navLayout = new QHBoxLayout(navContainer);
    navLayout->setContentsMargins(0, 0, 0, 0);
    navLayout->setSpacing(12);

    m_liveNavButton = makeNavButton(m_liveTelemetryAction, navContainer);
    m_dashboardNavButton = makeNavButton(m_dashboardAction, navContainer);
    m_eventLogNavButton = makeNavButton(m_eventLogAction, navContainer);
    m_settingsNavButton = makeNavButton(
        m_openSettingsAction,
        navContainer,
        Qt::ToolButtonIconOnly,
        u"iconButton"_s,
        QSize(44, 44));
    navLayout->addWidget(m_liveNavButton);
    navLayout->addWidget(m_dashboardNavButton);
    navLayout->addWidget(m_eventLogNavButton);
    navLayout->addWidget(m_settingsNavButton);

    contentLayout->addWidget(navContainer);

    toolbar->addWidget(content);
    updateToolbarLayout();
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    updateToolbarLayout();
}

void MainWindow::updateToolbarLayout() {
    if (!m_missionToolbar) {
        return;
    }

    constexpr int kCompactWidth = 900;
    constexpr int kNarrowWidth = 540;
    const int toolbarWidth = width();
    const int nextMode = toolbarWidth < kNarrowWidth
        ? 2
        : (toolbarWidth < kCompactWidth ? 1 : 0);
    const bool modeChanged = nextMode != m_toolbarLayoutMode;
    m_toolbarLayoutMode = nextMode;

    const bool compact = nextMode >= 1;
    const bool narrow = nextMode >= 2;
    m_missionToolbar->setProperty("compact", compact);
    m_missionToolbar->setProperty("narrow", narrow);

    if (m_missionMetaLabel) {
        m_missionMetaLabel->setVisible(!compact);
    }
    if (m_brandBlock) {
        m_brandBlock->setVisible(!narrow);
    }
    if (m_liveNavButton) {
        m_liveNavButton->setText(compact ? u"Live"_s : m_liveTelemetryAction->text());
    }
    if (m_dashboardNavButton) {
        m_dashboardNavButton->setText(compact ? u"Replay"_s : m_dashboardAction->text());
    }
    if (m_eventLogNavButton) {
        m_eventLogNavButton->setText(compact ? u"Log"_s : m_eventLogAction->text());
    }
    if (m_toolbarContent && m_toolbarContent->layout()) {
        m_toolbarContent->layout()->setSpacing(compact ? Theme::kSpaceBase : Theme::kSpaceXl);
    }
    if (m_connectionPageLabel) {
        m_connectionPageLabel->setMinimumWidth(compact ? 0 : 200);
        m_connectionPageLabel->setVisible(!narrow);
    }

    if (modeChanged) {
        // Dynamic QSS properties require repolishing after a mode transition.
        m_missionToolbar->setStyleSheet(buildToolbarStyleSheet());
    }
}

void MainWindow::updateSettingsIcon() {
    if (!m_openSettingsAction) {
        return;
    }

    QIcon settingsIcon;
    settingsIcon.addFile(
        settingsIconResource(Theme::kBgPanel()),
        QSize(),
        QIcon::Normal,
        QIcon::Off);
    settingsIcon.addFile(
        settingsIconResource(Theme::kBgButton()),
        QSize(),
        QIcon::Normal,
        QIcon::On);
    m_openSettingsAction->setIcon(settingsIcon);

    if (m_settingsWindow) {
        m_settingsWindow->setWindowIcon(
            QIcon(settingsIconResource(Theme::kBgBase())));
    }
}

void MainWindow::setupConnectionBar() {
    if (m_connectionBar) {
        return;
    }

    m_connectionBar = new QWidget(this);
    m_connectionBar->setObjectName(u"connectionStrip"_s);
    m_connectionBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto *row = new QHBoxLayout(m_connectionBar);
    row->setContentsMargins(16, 8, 16, 8);
    row->setSpacing(12);

    m_connectionPageLabel = new QLabel(u"Flight Monitoring"_s, m_connectionBar);
    m_connectionPageLabel->setObjectName(u"connectionStripContext"_s);
    m_connectionPageLabel->setWordWrap(false);
    m_connectionPageLabel->setMinimumWidth(200);

    m_openLogBtn = createActionButton(m_connectionBar, u"Open flight log"_s, u"folder-open"_s);
    m_clearFlightBtn = createActionButton(m_connectionBar, u"Clear flight data"_s, u"trash"_s);
    m_exportBtn = createActionButton(m_connectionBar, u"Export session to CSV"_s, u"export"_s);
    m_fakeTransmissionBtn = createActionButton(m_connectionBar, u"Start fake live transmission"_s, u"play"_s);

    row->addWidget(m_connectionPageLabel);
    row->addSpacing(16);
    row->addWidget(m_openLogBtn);
    row->addWidget(m_clearFlightBtn);
    row->addWidget(m_exportBtn);
    row->addWidget(m_fakeTransmissionBtn);
    row->addStretch(1);

    connect(m_openLogBtn, &QPushButton::clicked, this, &MainWindow::onOpenReplayFile);
    connect(m_clearFlightBtn, &QPushButton::clicked, this, &MainWindow::onClearFlightData);
    connect(m_exportBtn, &QPushButton::clicked, this, &MainWindow::onExportSession);
    connect(m_fakeTransmissionBtn, &QPushButton::clicked, this, &MainWindow::onToggleFakeTransmission);

    m_connectionBar->setStyleSheet(buildActionBarStyleSheet());
    updateToolbarLayout();
}

void MainWindow::updateBreadcrumb(const QString &context) {
    if (!m_connectionPageLabel) {
        return;
    }

    if (context.isEmpty()) {
        m_connectionPageLabel->setText(u"Flight Monitoring"_s);
    } else {
        m_connectionPageLabel->setText(
            QStringLiteral("Flight Monitoring › %1").arg(context));
    }
}

/*
void MainWindow::loadSerialPrefsToUi() {
    QSettings s(kSettingsOrg, kSettingsApp);
    const QString port = s.value(kSettingsSerialPort).toString();
    if (m_portCombo && !port.isEmpty()) {
        const int idx = m_portCombo->findText(port);
        if (idx >= 0) {
            m_portCombo->setCurrentIndex(idx);
        } else {
            m_portCombo->setCurrentText(port);
        }
    }
    if (m_baudCombo) {
        const QString baudStr = s.value(kSettingsSerialBaud, u"115200"_s).toString();
        const int idx = m_baudCombo->findText(baudStr);
        if (idx >= 0) {
            m_baudCombo->setCurrentIndex(idx);
        } else {
            m_baudCombo->setCurrentText(baudStr);
        }
    }
}

void MainWindow::persistSerialPrefs() {
    if (!m_portCombo || !m_baudCombo) {
        return;
    }
    QSettings s(kSettingsOrg, kSettingsApp);
    s.setValue(kSettingsSerialPort, m_portCombo->currentText().trimmed());
    s.setValue(kSettingsSerialBaud, m_baudCombo->currentText());
}
*/

void MainWindow::setupPages() {
    auto *central = new QWidget(this);
    auto *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);

    setupConnectionBar();
    if (m_connectionBar) {
        centralLayout->addWidget(m_connectionBar);
    }

    m_pages = new QStackedWidget(central);
    centralLayout->addWidget(m_pages, 1);
    setCentralWidget(central);

    m_flightDataPage = new DashboardPage(m_flightModel.get(), m_replay.get());
    m_pages->addWidget(m_flightDataPage);

    m_liveTelemetryPage = new LiveTelemetryPage(m_flightModel.get(), this);
    m_pages->addWidget(m_liveTelemetryPage);

    m_eventLogPage = new EventLogPage(m_pages);
    m_pages->addWidget(m_eventLogPage);

    m_pages->setCurrentWidget(m_flightDataPage);

    connect(m_dashboardAction, &QAction::triggered, this, [this]() {
        m_pages->setCurrentWidget(m_flightDataPage);
        if (m_flightModel && m_flightModel->replayMode() && m_replay) {
            const int replayIndex = m_replay->index();
            if (replayIndex <= 0) {
                m_flightModel->setDisplayedSample(FlightSample{});
            } else {
                applyReplayTelemetrySample(replayIndex);
            }
        }
        updateBreadcrumb();
    });
    connect(m_liveTelemetryAction, &QAction::triggered, this, [this]() {
        if (m_replayTelemetryCoalesceTimer) {
            m_replayTelemetryCoalesceTimer->stop();
        }
        m_pages->setCurrentWidget(m_liveTelemetryPage);
        updateBreadcrumb(m_serialPortSummary.isEmpty() ? u"Live Telemetry"_s : m_serialPortSummary);
    });
    connect(m_eventLogAction, &QAction::triggered, this, [this]() {
        if (m_replayTelemetryCoalesceTimer) {
            m_replayTelemetryCoalesceTimer->stop();
        }
        m_pages->setCurrentWidget(m_eventLogPage);
        updateBreadcrumb(u"Event Log"_s);
    });

    connect(m_liveTelemetryPage, &LiveTelemetryPage::scanDevicesRequested,
            this, &MainWindow::onScanLiveDevices);
    connect(m_liveTelemetryPage, &LiveTelemetryPage::connectDeviceRequested,
            this, &MainWindow::onConnectLiveDevice);
    connect(m_liveTelemetryPage, &LiveTelemetryPage::disconnectRequested,
            this, &MainWindow::onDisconnectLiveDevice);
    connect(m_liveTelemetryPage, &LiveTelemetryPage::startDemoRequested,
            this, &MainWindow::onStartLiveDemo);

    onScanLiveDevices();
}

void MainWindow::openSettingsWindow() {
    if (!m_openSettingsAction) {
        return;
    }

    if (!m_settingsWindow) {
        m_settingsWindow = new SettingsPage();
        m_settingsWindow->setAttribute(Qt::WA_DeleteOnClose);
        m_settingsWindow->setWindowTitle(u"CosmoSoft Settings"_s);
        m_settingsWindow->setWindowIcon(
            QIcon(settingsIconResource(Theme::kBgBase())));
        m_settingsWindow->resize(640, 560);

        connect(m_settingsWindow, &QObject::destroyed, this, [this]() {
            m_settingsWindow = nullptr;
            if (m_openSettingsAction) {
                m_openSettingsAction->setChecked(false);
            }
        });

        connect(m_settingsWindow, &SettingsPage::unitSystemChanged,
                m_flightDataPage, &DashboardPage::setImperialUnits);
        connect(m_settingsWindow, &SettingsPage::unitSystemChanged,
                m_liveTelemetryPage, &LiveTelemetryPage::setImperialUnits);
        connect(m_settingsWindow, &SettingsPage::uiSoundsEnabledChanged,
                this, [this](bool enabled) { m_uiSoundsEnabled = enabled; });
    }

    m_settingsWindow->show();
    m_settingsWindow->raise();
    m_settingsWindow->activateWindow();
    m_openSettingsAction->setChecked(true);
    statusBar()->showMessage(u"Settings window opened."_s, 2000);
}

void MainWindow::updateMissionClock() {
    if (!m_missionMetaLabel) {
        return;
    }

    const QDateTime localNow = QDateTime::currentDateTime();
    const int offsetSeconds = localNow.offsetFromUtc();
    const int absOffsetSeconds = qAbs(offsetSeconds);
    const int offsetHours = absOffsetSeconds / 3600;
    const int offsetMinutes = (absOffsetSeconds % 3600) / 60;

    QString offsetString = QStringLiteral("GMT%1%2")
                               .arg(offsetSeconds >= 0 ? u'+' : u'-')
                               .arg(offsetHours, 2, 10, QLatin1Char('0'));
    if (offsetMinutes > 0) {
        offsetString += QStringLiteral(":%1").arg(offsetMinutes, 2, 10, QLatin1Char('0'));
    }

    const QString timestamp = QStringLiteral("%1 | %2")
                                  .arg(offsetString, localNow.toString(u"HH:mm:ss | dd MMM yyyy"_s));
    m_missionMetaLabel->setText(timestamp);
}

// NOTE: Serial port functionality commented out for future live mode
/*
void MainWindow::refreshSerialPorts() {
    auto scanner = SerialPortScannerFactory::createSerialPortScanner();
    if (!scanner || !m_portCombo) {
        return;
    }
    const QString prev = m_portCombo->currentText().trimmed();
    QStringList ports;
    for (const auto &p : scanner->enumeratePorts()) {
        ports.append(QString::fromStdString(p));
    }
    m_portCombo->blockSignals(true);
    m_portCombo->clear();
    m_portCombo->addItems(ports);
    if (!prev.isEmpty()) {
        const int idx = m_portCombo->findText(prev);
        if (idx >= 0) {
            m_portCombo->setCurrentIndex(idx);
        } else {
            m_portCombo->setCurrentText(prev);
        }
    } else {
        loadSerialPrefsToUi();
    }
    m_portCombo->blockSignals(false);
}
*/

void MainWindow::onReplayPositionChanged(int trailLength) {
    m_pendingReplayTelemetryTrail = trailLength;
    if (trailLength <= 0) {
        if (m_replayTelemetryCoalesceTimer) {
            m_replayTelemetryCoalesceTimer->stop();
        }
        if (m_flightDataPage && m_flightDataPage->isVisible()) {
            m_flightModel->setDisplayedSample(FlightSample{});
        }
        return;
    }
    if (!m_loadedSession || trailLength > static_cast<int>(m_loadedSession->samples.size())) {
        return;
    }
    if (!m_flightDataPage || !m_flightDataPage->isVisible()) {
        return;
    }
    if (m_replay && m_replay->isPlaying()) {
        if (m_replayTelemetryCoalesceTimer
            && !m_replayTelemetryCoalesceTimer->isActive()) {
            m_replayTelemetryCoalesceTimer->start();
        }
        return;
    }
    applyReplayTelemetrySample(trailLength);
}

void MainWindow::applyReplayTelemetrySample(int trailLength) {
    if (!m_loadedSession || trailLength <= 0 || trailLength > static_cast<int>(m_loadedSession->samples.size())) {
        return;
    }
    m_flightModel->setDisplayedSample(m_loadedSession->samples[static_cast<std::size_t>(trailLength - 1)]);
}

void MainWindow::applyPendingReplayTelemetryStrip() {
    if (m_flightDataPage && m_flightDataPage->isVisible()) {
        applyReplayTelemetrySample(m_pendingReplayTelemetryTrail);
    }
}

void MainWindow::refreshFakeTransmissionButton() {
    if (!m_fakeTransmissionBtn) {
        return;
    }

    const bool active = m_fakeTransmissionTimer && m_fakeTransmissionTimer->isActive();
    m_fakeTransmissionBtn->setToolTip(active ? u"Stop fake live transmission"_s : u"Start fake live transmission"_s);
    m_fakeTransmissionBtn->setAccessibleName(m_fakeTransmissionBtn->toolTip());
    m_fakeTransmissionBtn->setIcon(
        m_fakeTransmissionBtn->style()->standardIcon(active ? QStyle::SP_MediaStop : QStyle::SP_MediaPlay));
}

void MainWindow::stopFakeTransmission(bool completed) {
    const bool wasActive = m_fakeTransmissionTimer && m_fakeTransmissionTimer->isActive();
    if (m_fakeTransmissionTimer) {
        m_fakeTransmissionTimer->stop();
    }
    refreshFakeTransmissionButton();

    if (completed) {
        showStatusMessage(u"Fake live transmission complete."_s, 3000);
        appendToLog(false, u"Fake live transmission complete."_s);
    }
    if (wasActive && m_liveTelemetryPage && m_serialPortSummary.isEmpty()) {
        m_liveTelemetryPage->setActiveConnection(
            completed ? u"Fake demo complete"_s : QString(),
            false);
    }
}

void MainWindow::onToggleFakeTransmission() {
    if (m_fakeTransmissionTimer && m_fakeTransmissionTimer->isActive()) {
        stopFakeTransmission(false);
        showStatusMessage(u"Fake live transmission stopped."_s, 2500);
        appendToLog(false, u"Fake live transmission stopped."_s);
        return;
    }

    onStartLiveDemo();
}

void MainWindow::onStartLiveDemo() {
    stopSerial();
    stopFakeTransmission(false);
    prepareLiveSession(u"Fake live transmission"_s);
    m_fakeTransmissionSamples.clear();
    m_fakeTransmissionByteCounts.clear();
    const auto packets = cosmo::flightlink::buildLaunchProfilePackets(160);
    m_fakeTransmissionSamples.reserve(packets.size());
    m_fakeTransmissionByteCounts.reserve(packets.size());
    for (const auto &packet : packets) {
        m_fakeTransmissionSamples.push_back(packet.sample);
        m_fakeTransmissionByteCounts.push_back(static_cast<qint64>(packet.line.size() + 1));
    }
    m_fakeTransmissionIndex = 0;

    if (m_pages && m_liveTelemetryPage) {
        m_pages->setCurrentWidget(m_liveTelemetryPage);
    }
    if (m_liveTelemetryAction) {
        m_liveTelemetryAction->setChecked(true);
    }
    if (m_liveTelemetryPage) {
        m_liveTelemetryPage->setActiveConnection(u"Fake demo"_s, true);
    }

    updateBreadcrumb(u"Fake live transmission"_s);
    showStatusMessage(u"Fake live transmission started."_s, 2500);
    appendToLog(false, u"Fake live transmission started."_s);
    if (m_fakeTransmissionTimer) {
        m_fakeTransmissionTimer->start();
    }
    refreshFakeTransmissionButton();
    pushFakeTransmissionSample();
}

void MainWindow::pushFakeTransmissionSample() {
    if (m_fakeTransmissionIndex >= m_fakeTransmissionSamples.size()) {
        stopFakeTransmission(true);
        return;
    }

    const auto index = m_fakeTransmissionIndex++;
    const FlightSample sample = m_fakeTransmissionSamples[index];
    m_logManager->appendSample(sample);
    m_flightModel->appendSample(sample);
    if (index < m_fakeTransmissionByteCounts.size()) {
        m_flightModel->addBytesReceived(m_fakeTransmissionByteCounts[index]);
    }
}

void MainWindow::onOpenReplayFile() {
    QSettings s(kSettingsOrg, kSettingsApp);
    QString startDir = s.value(kSettingsReplayDir, QDir::homePath()).toString();
    if (startDir.isEmpty()) {
        startDir = QDir::homePath();
    }

    const QString path = QFileDialog::getOpenFileName(
        this,
        u"Open flight log"_s,
        startDir,
        u"Flight logs (*.csv *.xlsx *.telem);;CSV (*.csv);;XLSX (*.xlsx);;TELEM (*.telem);;All files (*)"_s);
    if (path.isEmpty()) {
        return;
    }

    stopFakeTransmission(false);
    stopSerial();
    s.setValue(kSettingsReplayDir, QFileInfo(path).absolutePath());

    loadFlightLogAsync(path);
}

void MainWindow::loadFlightLogAsync(const QString &path) {
    cancelFlightLogLoad(false);
    const std::uint64_t generation = ++m_loadGeneration;
    auto cancelFlag = std::make_shared<std::atomic_bool>(false);
    m_loadCancelFlag = cancelFlag;

    auto *progress = new QProgressDialog(u"Loading flight log…"_s, u"Cancel"_s, 0, 0, this);
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    m_loadProgress = progress;
    if (m_openLogBtn) {
        m_openLogBtn->setEnabled(false);
    }
    connect(progress, &QObject::destroyed, this, [this, progress]() {
        if (m_loadProgress == progress) {
            m_loadProgress = nullptr;
        }
    });
    connect(progress, &QProgressDialog::canceled, this, [this, generation]() {
        if (generation == m_loadGeneration) {
            cancelFlightLogLoad(true);
        }
    });
    progress->show();

    const QPointer<QProgressDialog> guardedProgress(progress);
    auto *watcher = new QFutureWatcher<FlightLogLoadResult>(this);
    connect(watcher, &QFutureWatcher<FlightLogLoadResult>::finished, this,
            [this, watcher, path, generation, guardedProgress]() {
        if (guardedProgress) {
            if (m_loadProgress == guardedProgress) {
                m_loadProgress = nullptr;
            }
            guardedProgress->disconnect(this);
            guardedProgress->close();
            guardedProgress->deleteLater();
        }

        if (generation != m_loadGeneration) {
            watcher->deleteLater();
            return;
        }

        FlightLogLoadResult r = watcher->result();
        watcher->deleteLater();
        m_loadCancelFlag.reset();
        if (m_openLogBtn) {
            m_openLogBtn->setEnabled(true);
        }
        if (r.canceled) {
            showStatusMessage(u"Flight-log loading canceled."_s, 2500);
            return;
        }
        if (r.error) {
            const QString message = QString::fromStdString(*r.error);
            QMessageBox::warning(this, u"Could not load log"_s, message);
            appendToLog(true, message);
            return;
        }
        m_loadedSession = std::move(r.session);
        m_loadedPreview = std::move(r.preview);
        m_flightModel->resetSession();
        m_flightModel->setReplayMode(true);
        m_replay->setSession(m_loadedSession, m_loadedPreview);
        if (m_flightDataPage) {
            m_flightDataPage->setReplaySession(m_loadedSession, m_loadedPreview);
        }
        if (m_liveTelemetryPage) {
            m_liveTelemetryPage->setActiveConnection(QString(), false);
        }
        addRecentFile(path);
        const QString filename = QFileInfo(path).fileName();
        updateBreadcrumb(QStringLiteral("Session: %1").arg(filename));
        const QString loadMsg = QStringLiteral("Loaded flight: %1").arg(path);
        showStatusMessage(loadMsg, 4000);
        appendToLog(false, loadMsg);
    });
    const QFuture<FlightLogLoadResult> future = QtConcurrent::run([path, cancelFlag]() {
        return loadFlightLogAtPath(path, cancelFlag);
    });
    watcher->setFuture(future);
}

void MainWindow::cancelFlightLogLoad(bool showStatus) {
    const bool hadActiveLoad = static_cast<bool>(m_loadCancelFlag);
    ++m_loadGeneration;
    if (m_loadCancelFlag) {
        m_loadCancelFlag->store(true, std::memory_order_relaxed);
        m_loadCancelFlag.reset();
    }
    if (m_loadProgress) {
        auto *progress = m_loadProgress;
        m_loadProgress = nullptr;
        progress->disconnect(this);
        progress->close();
        progress->deleteLater();
    }
    if (m_openLogBtn) {
        m_openLogBtn->setEnabled(true);
    }
    if (showStatus && hadActiveLoad) {
        showStatusMessage(u"Flight-log loading canceled."_s, 2500);
    }
}

void MainWindow::onClearFlightData() {
    const bool hasData = !m_logManager->session().samples.empty()
                      || (m_loadedSession && !m_loadedSession->samples.empty());
    if (hasData) {
        const auto reply = QMessageBox::question(
            this,
            u"Clear flight data"_s,
            u"All loaded and recorded flight data will be lost.\n\nContinue?"_s,
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (reply != QMessageBox::Yes) {
            return;
        }
    }
    cancelFlightLogLoad(false);
    stopFakeTransmission(false);
    stopSerial();
    m_logManager->clear();
    m_replay->stop();
    m_loadedSession.reset();
    m_loadedPreview.reset();
    m_replay->setSession(nullptr, nullptr);
    m_flightModel->setReplayMode(false);
    m_flightModel->resetByteCounter();
    m_flightModel->resetSession();
    if (m_flightDataPage) {
        m_flightDataPage->setReplaySession(nullptr, nullptr);
    }
    if (m_liveTelemetryPage) {
        m_liveTelemetryPage->setActiveConnection(QString(), false);
    }
    updateBreadcrumb();  // Reset to default
    showStatusMessage(u"Cleared flight replay data."_s, 2000);
    appendToLog(false, u"Flight data cleared."_s);
}

void MainWindow::onShowAbout() {
    AboutDialog dlg(this);
    dlg.exec();
}

void MainWindow::updateBrandShadowColor() {
    if (!m_brandShadow) {
        return;
    }
    const QColor toolbarSurface(Theme::kBgPanel());
    const bool lightBackground = toolbarSurface.isValid()
        && relativeLuminance(toolbarSurface) > 0.5;
    QColor shadowColor(lightBackground
                           ? Theme::kTextPrimary()
                           : Theme::kBgDark());
    if (!shadowColor.isValid()) {
        shadowColor = QColor(Theme::kBorderPanel());
    }
    shadowColor.setAlpha(lightBackground ? 110 : 175);
    m_brandShadow->setColor(shadowColor);
}

void MainWindow::onThemeChanged() {
    updateSettingsIcon();

    updateBrandShadowColor();

    auto *toolbar = findChild<QToolBar *>(u"missionToolbar"_s);
    if (toolbar) {
        toolbar->setStyleSheet(buildToolbarStyleSheet());
    }

    if (m_brandLabel) {
        m_brandLabel->setText(
            QString(u"Cosmo<span style=\"color:%1\">Soft</span>"_s).arg(Theme::kAccentLink()));
    }

    if (m_connectionBar) {
        m_connectionBar->setStyleSheet(buildActionBarStyleSheet());
    }
}

void MainWindow::onExportSession() {
    if (m_exportInProgress) {
        showStatusMessage(u"A session export is already in progress."_s, 2500);
        return;
    }

    std::shared_ptr<const FlightSession> session;
    if (m_flightModel && m_flightModel->replayMode() && m_loadedSession) {
        session = m_loadedSession;
    }

    const FlightSession &liveSession = m_logManager->session();
    const bool hasSamples = session ? !session->samples.empty() : !liveSession.samples.empty();
    if (!hasSamples) {
        showStatusMessage(u"No samples to export — connect a serial port or load a log first."_s, 4000);
        return;
    }

    const QString path = QFileDialog::getSaveFileName(
        this,
        u"Export session"_s,
        QDir::homePath(),
        u"CSV (*.csv);;All files (*)"_s);
    if (path.isEmpty()) {
        return;
    }

    if (!session) {
        try {
            // Live telemetry remains mutable on the GUI thread, so export a
            // stable snapshot rather than reading it from the worker thread.
            session = std::make_shared<FlightSession>(liveSession);
        } catch (const std::exception &) {
            const QString message = u"Export failed — could not snapshot the session."_s;
            showStatusMessage(message, 5000);
            appendToLog(true, message);
            return;
        }
    }

    const std::uint64_t generation = ++m_exportGeneration;
    m_exportInProgress = true;
    if (m_exportBtn) {
        m_exportBtn->setEnabled(false);
    }
    showStatusMessage(QStringLiteral("Exporting session to %1…").arg(path));

    auto *watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this,
            [this, watcher, path, generation]() {
        if (generation != m_exportGeneration) {
            watcher->deleteLater();
            return;
        }

        const bool succeeded = watcher->result();
        watcher->deleteLater();
        m_exportInProgress = false;
        if (m_exportBtn) {
            m_exportBtn->setEnabled(true);
        }
        if (succeeded) {
            const QString exportMsg = QStringLiteral("Session exported to %1").arg(path);
            showStatusMessage(exportMsg, 4000);
            appendToLog(false, exportMsg);
        } else {
            const QString exportErrMsg = QStringLiteral("Export failed — could not write to %1").arg(path);
            showStatusMessage(exportErrMsg, 5000);
            appendToLog(true, exportErrMsg);
        }
    });
    watcher->setFuture(QtConcurrent::run([path, session]() {
        return exportFlightSessionAtPath(path, session);
    }));
}

void MainWindow::onScanLiveDevices() {
    if (m_scanCancelFlag) {
        m_scanCancelFlag->store(true, std::memory_order_relaxed);
    }
    const std::uint64_t generation = ++m_scanGeneration;
    auto cancelFlag = std::make_shared<std::atomic_bool>(false);
    m_scanCancelFlag = cancelFlag;
    showStatusMessage(u"Scanning for serial devices…"_s);

    auto *watcher = new QFutureWatcher<SerialPortScanResult>(this);
    connect(watcher, &QFutureWatcher<SerialPortScanResult>::finished, this,
            [this, watcher, generation]() {
        if (generation != m_scanGeneration) {
            watcher->deleteLater();
            return;
        }

        SerialPortScanResult result = watcher->result();
        watcher->deleteLater();
        m_scanCancelFlag.reset();
        if (result.canceled) {
            return;
        }
        if (result.error) {
            const QString message = QStringLiteral("Serial device scan failed: %1")
                                        .arg(QString::fromStdString(*result.error));
            showStatusMessage(message, 5000);
            appendToLog(true, message);
            return;
        }

        QStringList ports;
        for (const auto &port : result.ports) {
            ports.append(QString::fromStdString(port));
        }
        if (m_liveTelemetryPage) {
            m_liveTelemetryPage->setAvailablePorts(ports);
        }

        const QString message = ports.isEmpty()
            ? u"No serial devices found."_s
            : QStringLiteral("Found %1 serial device(s).").arg(ports.size());
        showStatusMessage(message, 2500);
        appendToLog(false, message);
    });
    watcher->setFuture(QtConcurrent::run([cancelFlag]() {
        return scanSerialPorts(cancelFlag);
    }));
}

void MainWindow::onConnectLiveDevice(const QString &portName, int baud) {
    startSerial(portName, baud);
}

void MainWindow::onDisconnectLiveDevice() {
    stopSerial();
    showStatusMessage(u"Live telemetry disconnected."_s, 2500);
}

void MainWindow::prepareLiveSession(const QString &context) {
    cancelFlightLogLoad(false);
    m_replay->stop();
    m_loadedSession.reset();
    m_loadedPreview.reset();
    m_replay->setSession(nullptr, nullptr);
    m_logManager->clear();
    m_flightModel->setReplayMode(false);
    m_flightModel->resetByteCounter();
    m_flightModel->resetSession();
    if (m_flightDataPage) {
        m_flightDataPage->setReplaySession(nullptr, nullptr);
    }
    if (m_liveTelemetryPage) {
        m_liveTelemetryPage->resetLiveState();
    }
    updateBreadcrumb(context);
}

void MainWindow::startSerial(const QString &portName, int baud) {
    stopSerial();
    if (portName.isEmpty()) {
        showStatusMessage(u"Select a serial port first."_s, 3000);
        return;
    }
    stopFakeTransmission(false);

    m_comms = CommsFactory::createSerialComms(portName.toStdString(), baud);
    if (!m_comms || !m_comms->open()) {
        showStatusMessage(u"Failed to open serial port."_s, 5000);
        appendToLog(true, QStringLiteral("Failed to open serial port: %1").arg(portName));
        m_comms.reset();
        if (m_liveTelemetryPage) {
            m_liveTelemetryPage->setActiveConnection(QString(), false);
        }
        return;
    }

    m_serialPortSummary = QStringLiteral("%1 @ %2").arg(portName).arg(baud);
    prepareLiveSession(m_serialPortSummary);

    const std::uint64_t generation = ++m_liveGeneration;
    m_lineDecodeWorker = std::make_unique<cosmo::telemetry::LineTelemetryDecodeWorker>(
        [this, generation](cosmo::telemetry::LineTelemetryBatch batch) mutable {
            if (m_liveBatchMailbox->push(generation, std::move(batch))) {
                QMetaObject::invokeMethod(
                    this,
                    [this, generation]() { drainLiveTelemetryBatches(generation); },
                    Qt::QueuedConnection);
            }
        },
        cosmo::telemetry::LineTelemetryWorkerConfig{},
        [this, generation](const std::string &error) {
            const QString message = QStringLiteral("Telemetry decoder delivery failed: %1")
                                        .arg(QString::fromStdString(error));
            QMetaObject::invokeMethod(
                this,
                [this, generation, message]() {
                    if (generation == m_liveGeneration) {
                        onParserError(message);
                        stopSerial();
                    }
                },
                Qt::QueuedConnection);
        });

    if (!m_lineDecodeWorker->start()) {
        showStatusMessage(u"Telemetry decoder failed to start."_s, 5000);
        appendToLog(true, u"Telemetry decoder failed to start."_s);
        m_lineDecodeWorker.reset();
        m_comms->close();
        m_comms.reset();
        m_serialPortSummary.clear();
        return;
    }

    m_serialWorker = std::make_unique<SerialWorker>(
        m_comms.get(),
        [this](std::vector<uint8_t> chunk) {
            if (m_lineDecodeWorker) {
                m_lineDecodeWorker->enqueueChunk(std::move(chunk));
            }
        },
        [this, generation](const std::string &err) {
            const QString msg = QString::fromStdString(err);
            QMetaObject::invokeMethod(
                this,
                [this, generation, msg]() {
                    if (generation == m_liveGeneration) {
                        onParserError(msg);
                        stopSerial();
                        showStatusMessage(u"Live telemetry disconnected after a serial failure."_s, 5000);
                    }
                },
                Qt::QueuedConnection);
        });

    if (!m_serialWorker->start()) {
        showStatusMessage(u"Serial reader failed to start."_s, 5000);
        appendToLog(true, u"Serial reader failed to start."_s);
        m_serialWorker.reset();
        m_lineDecodeWorker->stop();
        m_lineDecodeWorker.reset();
        drainLiveTelemetryBatches(generation);
        ++m_liveGeneration;
        if (m_comms) {
            m_comms->close();
        }
        m_comms.reset();
        m_serialPortSummary.clear();
        if (m_liveTelemetryPage) {
            m_liveTelemetryPage->setActiveConnection(QString(), false);
        }
        return;
    }

    if (m_pages && m_liveTelemetryPage) {
        m_pages->setCurrentWidget(m_liveTelemetryPage);
    }
    if (m_liveTelemetryAction) {
        m_liveTelemetryAction->setChecked(true);
    }
    if (m_liveTelemetryPage) {
        m_liveTelemetryPage->setActiveConnection(m_serialPortSummary, true);
    }
    const QString connectMsg = QStringLiteral("Connected to %1 @ %2").arg(portName).arg(baud);
    showStatusMessage(connectMsg, 3000);
    appendToLog(false, connectMsg);
}

void MainWindow::drainLiveTelemetryBatches(const std::uint64_t generation) {
    if (generation != m_liveGeneration || !m_liveBatchMailbox) {
        return;
    }

    auto result = m_liveBatchMailbox->take(generation);
    for (auto &batch : result.batches) {
        constexpr auto kMaxByteCount = static_cast<std::uint64_t>(
            std::numeric_limits<qint64>::max());
        const qint64 receivedBytes = static_cast<qint64>(
            batch.receivedBytes > kMaxByteCount
                ? kMaxByteCount
                : batch.receivedBytes);
        m_flightModel->addBytesReceived(receivedBytes);

        FlightSampleBatch samples;
        samples.reserve(static_cast<qsizetype>(batch.samples.size()));
        for (auto &sample : batch.samples) {
            m_logManager->appendSample(sample);
            samples.append(std::move(sample));
        }
        m_flightModel->appendLiveBatch(samples);

        if (batch.malformedLines > 0) {
            onParserError(
                QStringLiteral("Skipped %1 malformed live telemetry row(s).")
                    .arg(static_cast<qulonglong>(batch.malformedLines)));
        }
        if (batch.droppedChunks > 0 || batch.droppedBytes > 0) {
            onParserError(
                QStringLiteral(
                    "Dropped %1 queued telemetry chunk(s) (%2 bytes) while decoding.")
                    .arg(static_cast<qulonglong>(batch.droppedChunks))
                    .arg(static_cast<qulonglong>(batch.droppedBytes)));
        }
    }

    if (result.discardedDecodedSamples > 0) {
        onParserError(
            QStringLiteral("Dropped %1 decoded telemetry sample(s) while the GUI was busy.")
                .arg(static_cast<qulonglong>(result.discardedDecodedSamples)));
    }
}

void MainWindow::stopSerial() {
    const std::uint64_t generation = m_liveGeneration;
    if (m_serialWorker) {
        m_serialWorker->stop();
        m_serialWorker.reset();
    }
    if (m_lineDecodeWorker) {
        m_lineDecodeWorker->stop();
        m_lineDecodeWorker.reset();
    }
    drainLiveTelemetryBatches(generation);
    ++m_liveGeneration;
    if (m_comms) {
        m_comms->close();
        m_comms.reset();
    }
    const bool wasConnected = !m_serialPortSummary.isEmpty();
    const QString previous = m_serialPortSummary;
    m_serialPortSummary.clear();
    if (wasConnected) {
        const QString msg = QStringLiteral("Serial port disconnected: %1").arg(previous);
        appendToLog(false, msg);
        if (m_liveTelemetryPage) {
            m_liveTelemetryPage->setActiveConnection(QString(), false);
        }
    }
}
