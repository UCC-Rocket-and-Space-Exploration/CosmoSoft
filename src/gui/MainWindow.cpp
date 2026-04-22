#include "gui/MainWindow.h"

#include "domain/FlightSample.h"
#include "domain/FlightSession.h"
#include "gateway/comms/CommsFactory.h"
#include "gateway/comms/ISerialPortScanner.h"
#include "gateway/comms/SerialPortScannerFactory.h"
#include "gui/FlightDataModel.h"
#include "gui/SettingsKeys.h"
#include "services/persistence/FlightLogManager.h"
#include "gui/FlightReplayController.h"
#include "gui/pages/DashboardPage.h"
#include "gui/pages/MonitoringPage.h"
#include "gui/AboutDialog.h"
#include "gui/pages/MapPage.h"
#include "gui/pages/SettingsPage.h"
#include "gateway/comms/SerialWorker.h"
#include "services/import/SampleFileLoader.h"
#include "services/telemetry/Framer.h"
#include "services/telemetry/Parser.h"
#include "services/telemetry/ParserWorker.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QFont>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
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

#include <optional>

using namespace Qt::StringLiterals;

namespace {

struct FlightLogLoadResult {
    std::optional<std::string> error;
    FlightSession session;
};

[[nodiscard]] FlightLogLoadResult loadFlightLogAtPath(const QString &path) {
    FlightLogLoadResult r;
    if (path.endsWith(u".telem", Qt::CaseInsensitive)) {
        Framer framer;
        Parser parser;
        r.error = SampleFileLoader::loadTelemFile(path.toStdString(), r.session, framer, parser);
    } else {
        r.error = SampleFileLoader::loadTheseusCsv(path.toStdString(), r.session);
    }
    return r;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_flightModel(std::make_unique<FlightDataModel>(this)),
      m_replay(std::make_unique<FlightReplayController>(this)),
      m_logManager(std::make_unique<FlightLogManager>()) {
    setWindowTitle(u"CosmoSoft"_s);
    setWindowIcon(QIcon(u":/images/Logo_rounded.png"_s));

    setupActions();
    setupToolbar();
    setupDataBar();
    setupPages();
    refreshSerialPorts();

    connect(m_replay.get(), &FlightReplayController::positionChanged, this, &MainWindow::onReplayPositionChanged);

    m_replayTelemetryCoalesceTimer = new QTimer(this);
    m_replayTelemetryCoalesceTimer->setSingleShot(true);
    m_replayTelemetryCoalesceTimer->setInterval(50);
    connect(m_replayTelemetryCoalesceTimer, &QTimer::timeout, this, &MainWindow::applyPendingReplayTelemetryStrip);

    const auto flushReplayTelemetryStrip = [this]() {
        if (m_replayTelemetryCoalesceTimer) {
            m_replayTelemetryCoalesceTimer->stop();
        }
        const int idx = m_replay ? m_replay->index() : 0;
        if (idx <= 0) {
            m_flightModel->setDisplayedSample(FlightSample{});
            syncTelemetryStrip();
        } else {
            applyReplayTelemetrySample(idx);
        }
    };
    connect(m_replay.get(), &FlightReplayController::playbackPaused, this, flushReplayTelemetryStrip);
    connect(m_replay.get(), &FlightReplayController::playbackStopped, this, flushReplayTelemetryStrip);
    connect(m_replay.get(), &FlightReplayController::playbackFinished, this, flushReplayTelemetryStrip);

    m_dataRateTimer = new QTimer(this);
    m_dataRateTimer->setInterval(1000);
    connect(m_dataRateTimer, &QTimer::timeout, this, &MainWindow::updateDataRateLabel);
    m_dataRateTimer->start();

    statusBar()->showMessage(u"DO NOT FORGET TO CONNECT WIFI AND CABLE TO ROCKET."_s);

    if (const auto geom = QSettings(kSettingsOrg, kSettingsApp).value(kSettingsWindowMainGeo).toByteArray(); !geom.isEmpty())
        restoreGeometry(geom);
}

MainWindow::~MainWindow() {
    QSettings(kSettingsOrg, kSettingsApp).setValue(kSettingsWindowMainGeo, saveGeometry());
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
    constexpr std::size_t kMaxLogEntries = 2000;
    if (m_logEntries.size() >= kMaxLogEntries) {
        m_logEntries.erase(m_logEntries.begin());
    }
    m_logEntries.emplace_back(isError, text);

    if (m_settingsWindow) {
        if (isError) {
            m_settingsWindow->appendLogError(text);
        } else {
            m_settingsWindow->appendLogEntry(text);
        }
    }
}

void MainWindow::setupActions() {
    m_showMonitoringAction = new QAction(u"Monitoring"_s, this);
    m_showMonitoringAction->setToolTip(u"Switch to the monitoring page."_s);

    m_showFlightDataAction = new QAction(u"Flight data"_s, this);
    m_showFlightDataAction->setToolTip(u"Switch to flight data and charts."_s);

    m_showMapAction = new QAction(u"Map"_s, this);
    m_showMapAction->setToolTip(u"Switch to the lat/lon map page."_s);

    QIcon settingsIcon;
    settingsIcon.addFile(u":/icons/settings_button.png"_s, QSize(), QIcon::Normal, QIcon::Off);
    settingsIcon.addFile(u":/icons/settings_button_black.png"_s, QSize(), QIcon::Normal, QIcon::On);
    m_openSettingsAction = new QAction(settingsIcon, u"Settings"_s, this);
    m_openSettingsAction->setToolTip(u"Open the settings window."_s);
    m_openSettingsAction->setCheckable(true);

    connect(m_showMonitoringAction, &QAction::triggered, this, [this]() {
        m_pages->setCurrentWidget(m_monitoringPage);
        updateTopBarsForCurrentPage();
        statusBar()->showMessage(u"Monitoring page selected."_s, 2000);
    });

    connect(m_showFlightDataAction, &QAction::triggered, this, [this]() {
        m_pages->setCurrentWidget(m_flightDataPage);
        updateTopBarsForCurrentPage();
        statusBar()->showMessage(u"Flight data page selected."_s, 2000);
    });

    connect(m_showMapAction, &QAction::triggered, this, [this]() {
        m_pages->setCurrentWidget(m_mapPage);
        updateTopBarsForCurrentPage();
        statusBar()->showMessage(u"Map page selected."_s, 2000);
    });

    connect(m_openSettingsAction, &QAction::triggered, this, [this]() { openSettingsWindow(); });
}

void MainWindow::setupToolbar() {
    auto *toolbar = new QToolBar(u"Mission Toolbar"_s, this);
    toolbar->setObjectName(u"missionToolbar"_s);
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    toolbar->setAllowedAreas(Qt::TopToolBarArea);
    toolbar->setStyleSheet(uR"(
        QToolBar#missionToolbar {
            background: rgba(73, 73, 73, 0.95);
            padding: 10px 10px;
            border: none;
        }

        QWidget#toolbarContent {
            background: transparent;
            margin: 0;
        }

        QWidget#brandBlock QLabel#brandLabel {
            font-size: 26px;
            font-weight: 400;
            font-family: "Workbench","Courier New", "Roboto Mono", monospace;
            letter-spacing: 3px;
            color: #f4f4f4;
        }
        QWidget#brandBlock QLabel#missionMeta {
            font-size: 14px;
            color: #dadada;
            font-family: "Red Hat Mono", "Courier New", "Roboto Mono", monospace;
        }

        QLabel#missionPageTitle {
            font-size: 15px;
            font-weight: 600;
            color: #c8c8c8;
            letter-spacing: 2px;
            font-family: "Red Hat Mono", "Courier New", "Roboto Mono", monospace;
        }

        QToolButton[kind="navButton"] {
            font-size: 12px;
            min-width: 150px;
            padding: 5px 8px;
            border: 2px solid #cfcfcf;
            border-radius: 0;
            background-color: #4b4b4b;
            color: #f7f7f7;
            letter-spacing: 1px;
            font-family: "Red Hat Mono", "Courier New", "Roboto Mono", monospace;
        }

        QToolButton[kind="navButton"]:hover {
            background-color: #5c5c5c;
        }

        QToolButton[kind="navButton"]:checked {
            background-color: #dfdfdf;
            color: #101010;
            border-color: #ffffff;
        }

        QToolButton[kind="navButton"]:disabled {
            color: rgba(255, 255, 255, 120);
            border-color: rgba(255, 255, 255, 70);
            background-color: rgba(255, 255, 255, 0);
        }

        QToolButton[kind="iconButton"] {
            min-width: 30px;
            min-height: 30px;
            border: none;
            background-color: transparent;
        }

        QToolButton[kind="iconButton"]:hover {
            background-color: rgba(255, 255, 255, 0.08);
        }

        QToolButton[kind="iconButton"]:checked {
            background-color: rgba(255, 255, 255, 0.15);
        }
    )"_s);
    addToolBar(Qt::TopToolBarArea, toolbar);

    auto *text_shadow = new QGraphicsDropShadowEffect(this);
    text_shadow->setBlurRadius(5);
    text_shadow->setColor(QColor(0, 0, 0, 160));
    text_shadow->setOffset(1, 1);

    auto *content = new QWidget(toolbar);
    content->setObjectName(u"toolbarContent"_s);
    auto *contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(24);

    auto *brandBlock = new QWidget(content);
    brandBlock->setObjectName(u"brandBlock"_s);
    brandBlock->setGraphicsEffect(text_shadow);
    auto *brandLayout = new QVBoxLayout(brandBlock);
    brandLayout->setContentsMargins(0, 0, 0, 0);
    brandLayout->setSpacing(2);
    auto *brandLabel = new QLabel(u"Cosmo<span style=\"color:#000000\">Soft</span>"_s, brandBlock);
    brandLabel->setObjectName(u"brandLabel"_s);
    brandLabel->setTextFormat(Qt::RichText);
    const QVariant workbenchFamily = qApp->property("workbenchFontFamily");
    if (workbenchFamily.isValid()) {
        QFont brandFont = brandLabel->font();
        brandFont.setFamily(workbenchFamily.toString());
        brandFont.setPointSize(26);
        brandFont.setBold(true);
        brandLabel->setFont(brandFont);
    }
    brandLayout->addWidget(brandLabel);

    m_missionMetaLabel = new QLabel(u"GMT: --:--:-- | -- --- ----"_s, brandBlock);
    m_missionMetaLabel->setObjectName(u"missionMeta"_s);
    m_missionMetaLabel->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    brandLayout->addWidget(m_missionMetaLabel);
    contentLayout->addWidget(brandBlock);

    m_toolbarPageLabel = new QLabel(u"Monitoring"_s, content);
    m_toolbarPageLabel->setObjectName(u"missionPageTitle"_s);
    contentLayout->addWidget(m_toolbarPageLabel);
    contentLayout->addSpacing(8);

    updateMissionClock();
    if (!m_missionClockTimer) {
        m_missionClockTimer = new QTimer(this);
        m_missionClockTimer->setInterval(1000);
        connect(m_missionClockTimer, &QTimer::timeout, this, &MainWindow::updateMissionClock);
        m_missionClockTimer->start();
    }

    contentLayout->addStretch(1);

    auto *navGroup = new QActionGroup(this);
    navGroup->setExclusive(true);
    m_showMonitoringAction->setCheckable(true);
    m_showFlightDataAction->setCheckable(true);
    m_showMapAction->setCheckable(true);
    navGroup->addAction(m_showMonitoringAction);
    navGroup->addAction(m_showFlightDataAction);
    navGroup->addAction(m_showMapAction);
    m_showMonitoringAction->setChecked(true);

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
        button->setDefaultAction(action);
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
    navLayout->addWidget(makeNavButton(m_showMonitoringAction, navContainer));
    navLayout->addWidget(makeNavButton(m_showFlightDataAction, navContainer));
    navLayout->addWidget(makeNavButton(m_showMapAction, navContainer));

    auto *aboutAction = new QAction(u"About"_s, this);
    aboutAction->setToolTip(u"About CosmoSoft"_s);
    connect(aboutAction, &QAction::triggered, this, &MainWindow::onShowAbout);
    auto *aboutBtn = makeNavButton(aboutAction, navContainer, Qt::ToolButtonTextOnly, u"navButton"_s);
    aboutBtn->setCheckable(false);
    navLayout->addWidget(aboutBtn);

    navLayout->addWidget(makeNavButton(m_openSettingsAction, navContainer, Qt::ToolButtonIconOnly, u"iconButton"_s, QSize(44, 44)));

    contentLayout->addWidget(navContainer);

    toolbar->addWidget(content);
}

void MainWindow::setupDataBar() {
    if (m_dataBar) {
        return;
    }

    m_dataBar = new QWidget(this);
    m_dataBar->setObjectName(u"telemetryStrip"_s);
    m_dataBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto *dataLayout = new QHBoxLayout(m_dataBar);
    dataLayout->setContentsMargins(16, 6, 16, 6);
    dataLayout->setSpacing(24);

    auto buildBadgeLabel = [](const QString &text, QWidget *parent) {
        auto *label = new QLabel(text, parent);
        label->setObjectName(u"telemetryBadge"_s);
        label->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        label->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
        return label;
    };

    m_dataStripPageLabel = buildBadgeLabel(u"Monitoring"_s, m_dataBar);
    m_dataStripPageLabel->setObjectName(u"telemetryStripPage"_s);
    m_dataLinkStatusLabel = buildBadgeLabel(u"LINK: idle"_s, m_dataBar);
    m_dataRateLabel = buildBadgeLabel(u"RATE: -- B/s"_s, m_dataBar);

    m_droppedBadgeLabel = buildBadgeLabel(QString{}, m_dataBar);
    m_droppedBadgeLabel->setObjectName(u"telemetryDropBadge"_s);
    m_droppedBadgeLabel->setVisible(false);

    dataLayout->addWidget(m_dataStripPageLabel);
    dataLayout->addWidget(m_dataLinkStatusLabel);
    dataLayout->addWidget(m_dataRateLabel);
    dataLayout->addWidget(m_droppedBadgeLabel);
    dataLayout->addStretch(1);

    m_dataBar->setStyleSheet(uR"(
        QWidget#telemetryStrip {
            background: rgba(26, 26, 26, 0.95);
            color: #f0f0f0;
            border-top: 1px solid rgba(255, 255, 255, 0.08);
            border-bottom: 1px solid rgba(0, 0, 0, 0.7);
        }

        QWidget#telemetryStrip QLabel#telemetryStripPage {
            font-size: 11px;
            color: #8fa0b0;
            letter-spacing: 3px;
            font-weight: 600;
            font-family: "Red Hat Mono", "Courier New", "Roboto Mono", monospace;
        }

        QWidget#telemetryStrip QLabel#telemetryBadge {
            font-size: 12px;
            color: #f7f7f7;
            letter-spacing: 1px;
            font-family: "Red Hat Mono", "Courier New", "Roboto Mono", monospace;
        }

        QWidget#telemetryStrip QLabel#telemetryDropBadge {
            font-size: 12px;
            color: #ff6b6b;
            letter-spacing: 1px;
            font-family: "Red Hat Mono", "Courier New", "Roboto Mono", monospace;
        }
    )"_s);
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

    m_connectionPageLabel = new QLabel(m_connectionBar);
    m_connectionPageLabel->setObjectName(u"connectionStripContext"_s);
    m_connectionPageLabel->setWordWrap(false);
    m_connectionPageLabel->setMinimumWidth(200);
    m_connectionPageLabel->setStyleSheet(
        u"color: #9aa7b8; font-size: 12px; font-family: \"Red Hat Mono\", monospace;"_s);

    m_serialControlBlock = new QWidget(m_connectionBar);
    auto *serialRow = new QHBoxLayout(m_serialControlBlock);
    serialRow->setContentsMargins(0, 0, 0, 0);
    serialRow->setSpacing(12);

    auto *portLabel = new QLabel(u"Port"_s, m_serialControlBlock);
    portLabel->setStyleSheet(u"color: #c8c8c8; font-family: \"Red Hat Mono\", monospace;"_s);
    m_portCombo = new QComboBox(m_serialControlBlock);
    m_portCombo->setEditable(true);
    m_portCombo->setMinimumWidth(200);
    m_portCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);

    auto *baudLabel = new QLabel(u"Baud"_s, m_serialControlBlock);
    baudLabel->setStyleSheet(portLabel->styleSheet());
    m_baudCombo = new QComboBox(m_serialControlBlock);
    const QList<int> bauds = {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600};
    for (int b : bauds) {
        m_baudCombo->addItem(QString::number(b), b);
    }
    m_baudCombo->setCurrentIndex(4);

    auto *refreshBtn = new QPushButton(u"Refresh"_s, m_serialControlBlock);
    auto *connectBtn = new QPushButton(u"Connect"_s, m_serialControlBlock);
    auto *disconnectBtn = new QPushButton(u"Disconnect"_s, m_serialControlBlock);
    serialRow->addWidget(portLabel);
    serialRow->addWidget(m_portCombo);
    serialRow->addWidget(baudLabel);
    serialRow->addWidget(m_baudCombo);
    serialRow->addWidget(refreshBtn);
    serialRow->addWidget(connectBtn);
    serialRow->addWidget(disconnectBtn);

    auto *openLogBtn    = new QPushButton(u"Open log…"_s, m_connectionBar);
    auto *clearFlightBtn = new QPushButton(u"Clear flight"_s, m_connectionBar);
    auto *exportBtn     = new QPushButton(u"Export session…"_s, m_connectionBar);

    row->addWidget(m_connectionPageLabel);
    row->addWidget(m_serialControlBlock);
    row->addSpacing(12);
    row->addWidget(openLogBtn);
    row->addWidget(clearFlightBtn);
    row->addWidget(exportBtn);
    row->addStretch(1);

    connect(refreshBtn, &QPushButton::clicked, this, &MainWindow::refreshSerialPorts);
    connect(connectBtn, &QPushButton::clicked, this, [this]() {
        persistSerialPrefs();
        const QString port = m_portCombo ? m_portCombo->currentText().trimmed() : QString{};
        int baud = 115200;
        if (m_baudCombo) {
            baud = m_baudCombo->currentData().toInt();
            if (baud <= 0) {
                baud = m_baudCombo->currentText().toInt();
            }
            if (baud <= 0) {
                baud = 115200;
            }
        }
        startSerial(port, baud);
    });
    connect(disconnectBtn, &QPushButton::clicked, this, &MainWindow::stopSerial);
    connect(openLogBtn,    &QPushButton::clicked, this, &MainWindow::onOpenReplayFile);
    connect(clearFlightBtn, &QPushButton::clicked, this, &MainWindow::onClearFlightData);
    connect(exportBtn,     &QPushButton::clicked, this, &MainWindow::onExportSession);

    m_connectionBar->setStyleSheet(uR"(
        QWidget#connectionStrip {
            background: rgba(34, 34, 34, 0.98);
            color: #f0f0f0;
            border-bottom: 1px solid rgba(255, 255, 255, 0.06);
        }
        QWidget#connectionStrip QComboBox {
            background-color: #1a1a1a;
            color: #f5f5f5;
            border: 1px solid #4d4d4d;
            border-radius: 4px;
            padding: 4px 8px;
            min-height: 22px;
            font-family: "Red Hat Mono", "Courier New", monospace;
        }
        QWidget#connectionStrip QComboBox::drop-down { border: none; width: 22px; }
        QWidget#connectionStrip QComboBox QAbstractItemView {
            background-color: #2b2d33;
            color: #f5f5f5;
            selection-background-color: #4b4b4b;
        }
        QWidget#connectionStrip QPushButton {
            border: 1px solid #6a6a6a;
            border-radius: 4px;
            padding: 4px 10px;
            min-height: 28px;
            background-color: #3d3f47;
            color: #f0f0f0;
            font-family: "Red Hat Mono", "Courier New", monospace;
            font-size: 11px;
        }
        QWidget#connectionStrip QPushButton:hover { background-color: #4d4f57; }
        QWidget#connectionStrip QPushButton:pressed { background-color: #2d2f37; }
    )"_s);

    loadSerialPrefsToUi();
}

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

void MainWindow::setupPages() {
    auto *central = new QWidget(this);
    auto *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);

    setupConnectionBar();
    if (m_connectionBar) {
        centralLayout->addWidget(m_connectionBar);
    }

    if (!m_dataBar) {
        setupDataBar();
    }
    if (m_dataBar) {
        centralLayout->addWidget(m_dataBar);
    }

    m_pages = new QStackedWidget(central);
    centralLayout->addWidget(m_pages, 1);
    setCentralWidget(central);

    m_monitoringPage = new MonitoringPage(m_flightModel.get());
    m_flightDataPage = new DashboardPage(m_flightModel.get(), m_replay.get());
    m_mapPage        = new MapPage(m_flightModel.get(), m_replay.get());
    m_pages->addWidget(m_monitoringPage);
    m_pages->addWidget(m_flightDataPage);
    m_pages->addWidget(m_mapPage);
    m_pages->setCurrentWidget(m_monitoringPage);

    connect(m_pages, &QStackedWidget::currentChanged, this, [this](int) {
        updateTopBarsForCurrentPage();
    });
    connect(m_flightModel.get(), &FlightDataModel::replayModeChanged, this, [this](bool) {
        syncTelemetryStrip();
    });

    updateTopBarsForCurrentPage();
}

bool MainWindow::isMonitoringPageActive() const {
    return m_pages && m_pages->currentWidget() == m_monitoringPage;
}

bool MainWindow::isMapPageActive() const {
    return m_pages && m_pages->currentWidget() == m_mapPage;
}

void MainWindow::updateTopBarsForCurrentPage() {
    const bool monitoring = isMonitoringPageActive();
    const bool map        = isMapPageActive();

    if (m_toolbarPageLabel) {
        if (monitoring) {
            m_toolbarPageLabel->setText(u"Monitoring"_s);
        } else if (map) {
            m_toolbarPageLabel->setText(u"Map"_s);
        } else {
            m_toolbarPageLabel->setText(u"Flight data"_s);
        }
    }

    if (m_connectionPageLabel) {
        if (monitoring) {
            m_connectionPageLabel->setText(
                u"Serial — port, baud, Connect. Flight logs — Open log…"_s);
        } else if (map) {
            m_connectionPageLabel->setText(
                u"Map — lat/lon flight path. Load a log or connect for live tracking."_s);
        } else {
            m_connectionPageLabel->setText(
                u"Replay — Open log… or Clear flight. Serial controls are on Monitoring."_s);
        }
    }

    if (m_serialControlBlock) {
        m_serialControlBlock->setVisible(monitoring);
    }
    if (m_flightModel) {
        m_prevBytesForRate = m_flightModel->totalBytesReceived();
    }
    syncTelemetryStrip();
    updateDataRateLabel();
}

void MainWindow::syncTelemetryStrip() {
    if (!m_dataStripPageLabel || !m_dataLinkStatusLabel || !m_dataRateLabel || !m_flightModel) {
        return;
    }

    if (isMonitoringPageActive()) {
        m_dataStripPageLabel->setText(u"MONITORING"_s);
        if (m_serialPortSummary.isEmpty()) {
            m_dataLinkStatusLabel->setText(u"LINK: idle"_s);
        } else {
            m_dataLinkStatusLabel->setText(QStringLiteral("LINK: %1").arg(m_serialPortSummary));
        }
        return;
    }

    if (isMapPageActive()) {
        m_dataStripPageLabel->setText(u"MAP"_s);
        const QString link = m_serialPortSummary.isEmpty() ? u"idle"_s : m_serialPortSummary;
        m_dataLinkStatusLabel->setText(QStringLiteral("LINK: %1").arg(link));
        return;
    }

    m_dataStripPageLabel->setText(u"FLIGHT DATA"_s);
    const bool replay = m_flightModel->replayMode();
    const int n   = m_replay ? m_replay->sampleCount() : 0;
    const int pos = m_replay ? m_replay->index() : 0;
    if (replay && n > 0) {
        m_dataLinkStatusLabel->setText(
            QStringLiteral("SESSION: replay · %1 / %2 samples").arg(pos).arg(n));
        m_dataRateLabel->setText(u"HINT: Play / slider on Flight data page"_s);
    } else if (replay && n == 0) {
        m_dataLinkStatusLabel->setText(u"SESSION: replay (empty)"_s);
        m_dataRateLabel->setText(u"Open a log to load samples"_s);
    } else {
        const QString link = m_serialPortSummary.isEmpty() ? u"idle"_s : m_serialPortSummary;
        m_dataLinkStatusLabel->setText(QStringLiteral("SESSION: live · %1").arg(link));
    }
}

void MainWindow::openSettingsWindow() {
    if (!m_openSettingsAction) {
        return;
    }

    if (!m_settingsWindow) {
        m_settingsWindow = new SettingsPage();
        m_settingsWindow->setAttribute(Qt::WA_DeleteOnClose);
        m_settingsWindow->setWindowTitle(u"CosmoSoft Settings"_s);
        m_settingsWindow->setWindowIcon(QIcon(u":/icons/settings_button.png"_s));
        m_settingsWindow->resize(520, 750);

        connect(m_settingsWindow, &QObject::destroyed, this, [this]() {
            m_settingsWindow = nullptr;
            if (m_openSettingsAction) {
                m_openSettingsAction->setChecked(false);
            }
        });

        for (const auto &[isError, text] : m_logEntries) {
            if (isError) {
                m_settingsWindow->appendLogError(text);
            } else {
                m_settingsWindow->appendLogEntry(text);
            }
        }
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

void MainWindow::updateDataRateLabel() {
    if (!m_dataRateLabel || !m_flightModel) {
        return;
    }
    const qint64 total = m_flightModel->totalBytesReceived();
    const qint64 delta = total - m_prevBytesForRate;
    m_prevBytesForRate = total;
    if (isMonitoringPageActive()) {
        m_dataRateLabel->setText(QStringLiteral("RATE: %1 B/s").arg(delta));
    } else if (!m_flightModel->replayMode()) {
        m_dataRateLabel->setText(QStringLiteral("RATE: %1 B/s").arg(delta));
    }

    if (m_droppedBadgeLabel) {
        const std::size_t dropped = m_rawQueue.dropped();
        if (dropped != m_lastDroppedCount) {
            m_lastDroppedCount = dropped;
            if (dropped > 0) {
                m_droppedBadgeLabel->setText(
                    QStringLiteral("⚠ %1 dropped").arg(static_cast<qulonglong>(dropped)));
                m_droppedBadgeLabel->setVisible(true);
            } else {
                m_droppedBadgeLabel->setVisible(false);
            }
        }
    }
}

void MainWindow::refreshSerialPorts() {
    std::unique_ptr<ISerialPortScanner> scanner(SerialPortScannerFactory::createSerialPortScanner());
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

void MainWindow::onReplayPositionChanged(int trailLength) {
    if (m_mapPage) {
        m_mapPage->setReplayTrailLength(trailLength);
    }

    if (trailLength <= 0) {
        if (m_replayTelemetryCoalesceTimer) {
            m_replayTelemetryCoalesceTimer->stop();
        }
        m_flightModel->setDisplayedSample(FlightSample{});
        syncTelemetryStrip();
        return;
    }
    if (trailLength > static_cast<int>(m_loadedSession.samples.size())) {
        return;
    }
    m_pendingReplayTelemetryTrail = trailLength;
    if (m_replay && m_replay->isPlaying()) {
        if (m_replayTelemetryCoalesceTimer) {
            m_replayTelemetryCoalesceTimer->start();
        }
        return;
    }
    applyReplayTelemetrySample(trailLength);
}

void MainWindow::applyReplayTelemetrySample(int trailLength) {
    if (trailLength <= 0 || trailLength > static_cast<int>(m_loadedSession.samples.size())) {
        return;
    }
    m_flightModel->setDisplayedSample(m_loadedSession.samples[static_cast<std::size_t>(trailLength - 1)]);
    syncTelemetryStrip();
}

void MainWindow::applyPendingReplayTelemetryStrip() {
    applyReplayTelemetrySample(m_pendingReplayTelemetryTrail);
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
        u"Flight logs (*.csv *.telem);;CSV (*.csv);;TELEM (*.telem);;All files (*)"_s);
    if (path.isEmpty()) {
        return;
    }

    s.setValue(kSettingsReplayDir, QFileInfo(path).absolutePath());

    stopSerial();

    auto *progress = new QProgressDialog(u"Loading flight log…"_s, QString(), 0, 0, this);
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setCancelButton(nullptr);
    progress->show();

    auto *watcher = new QFutureWatcher<FlightLogLoadResult>(this);
    connect(watcher, &QFutureWatcher<FlightLogLoadResult>::finished, this, [this, watcher, path, progress]() {
        progress->close();
        progress->deleteLater();
        FlightLogLoadResult r = watcher->result();
        watcher->deleteLater();
        if (r.error) {
            QMessageBox::warning(
                this,
                u"Could not load log"_s,
                QString::fromStdString(*r.error));
            return;
        }
        m_loadedSession = std::move(r.session);
        m_flightModel->resetSession();
        m_flightModel->setReplayMode(true);
        m_replay->setSession(m_loadedSession);
        if (m_flightDataPage) {
            m_flightDataPage->setReplaySession(&m_loadedSession);
        }
        if (m_mapPage) {
            m_mapPage->setReplaySession(&m_loadedSession);
        }
        syncTelemetryStrip();
        const QString loadMsg = QStringLiteral("Loaded flight: %1").arg(path);
        showStatusMessage(loadMsg, 4000);
        appendToLog(false, loadMsg);
    });
    const QFuture<FlightLogLoadResult> future = QtConcurrent::run([path]() {
        return loadFlightLogAtPath(path);
    });
    watcher->setFuture(future);
}

void MainWindow::onClearFlightData() {
    m_logManager->clear();
    m_replay->stop();
    m_loadedSession.samples.clear();
    m_replay->setSession({});
    m_flightModel->setReplayMode(false);
    m_flightModel->resetSession();
    if (m_flightDataPage) {
        m_flightDataPage->setReplaySession(nullptr);
    }
    if (m_mapPage) {
        m_mapPage->setReplaySession(nullptr);
    }
    syncTelemetryStrip();
    showStatusMessage(u"Cleared flight replay data."_s, 2000);
    appendToLog(false, u"Flight data cleared."_s);
}

void MainWindow::onShowAbout() {
    AboutDialog dlg(this);
    dlg.exec();
}

void MainWindow::onExportSession() {
    const auto &session = m_logManager->session();
    if (session.samples.empty()) {
        showStatusMessage(u"No samples to export — connect a serial port or load a log first."_s, 4000);
        return;
    }

    const QString path = QFileDialog::getSaveFileName(
        this,
        u"Export session"_s,
        QDir::homePath(),
        u"Text log (*.txt);;All files (*)"_s);
    if (path.isEmpty()) {
        return;
    }

    m_logManager->setOutputPath(path.toStdString());
    if (m_logManager->exportSessionToTextFile()) {
        const QString exportMsg = QStringLiteral("Session exported to %1").arg(path);
        showStatusMessage(exportMsg, 4000);
        appendToLog(false, exportMsg);
    } else {
        const QString exportErrMsg = QStringLiteral("Export failed — could not write to %1").arg(path);
        showStatusMessage(exportErrMsg, 5000);
        appendToLog(true, exportErrMsg);
    }
}

void MainWindow::startSerial(const QString &portName, int baud) {
    stopSerial();
    if (portName.isEmpty()) {
        showStatusMessage(u"Select a serial port first."_s, 3000);
        return;
    }

    m_comms = CommsFactory::createSerialComms(portName.toStdString(), baud);
    if (!m_comms || !m_comms->open()) {
        showStatusMessage(u"Failed to open serial port."_s, 5000);
        m_comms.reset();
        return;
    }

    m_prevBytesForRate = m_flightModel->totalBytesReceived();

    m_parserWorker = std::make_unique<ParserWorker>(
        m_rawQueue,
        [this](FlightSample &&s) {
            m_logManager->appendSample(s);
            FlightDataModel *model = m_flightModel.get();
            QMetaObject::invokeMethod(
                model,
                "appendSample",
                Qt::QueuedConnection,
                Q_ARG(FlightSample, s));
        },
        [this](std::string_view err) {
            const QString msg = QString::fromUtf8(err.data(), static_cast<int>(err.size()));
            QMetaObject::invokeMethod(this, "onParserError", Qt::QueuedConnection, Q_ARG(QString, msg));
        });

    if (!m_parserWorker->start()) {
        showStatusMessage(u"Parser thread failed to start."_s, 5000);
        m_parserWorker.reset();
        m_comms.reset();
        return;
    }

    m_serialWorker = std::make_unique<SerialWorker>(
        m_comms.get(),
        [this](std::vector<uint8_t> chunk) {
            const qint64 n = static_cast<qint64>(chunk.size());
            m_rawQueue.push(std::move(chunk));
            FlightDataModel *model = m_flightModel.get();
            QMetaObject::invokeMethod(
                model,
                "addBytesReceived",
                Qt::QueuedConnection,
                Q_ARG(qint64, n));
        },
        [this](const std::string &err) {
            const QString msg = QString::fromStdString(err);
            QMetaObject::invokeMethod(this, "onParserError", Qt::QueuedConnection, Q_ARG(QString, msg));
        });

    if (!m_serialWorker->start()) {
        showStatusMessage(u"Serial reader failed to start."_s, 5000);
        m_serialWorker.reset();
        m_parserWorker.reset();
        m_comms.reset();
        return;
    }

    m_serialPortSummary = QStringLiteral("%1 @ %2").arg(portName).arg(baud);
    m_flightModel->setReplayMode(false);
    if (m_flightDataPage) {
        m_flightDataPage->setReplaySession(nullptr);
    }
    if (m_mapPage) {
        m_mapPage->setReplaySession(nullptr);
    }
    syncTelemetryStrip();
    const QString connectMsg = QStringLiteral("Connected to %1 @ %2").arg(portName).arg(baud);
    showStatusMessage(connectMsg, 3000);
    appendToLog(false, connectMsg);
}

void MainWindow::stopSerial() {
    if (m_serialWorker) {
        m_serialWorker->stop();
        m_serialWorker.reset();
    }
    if (m_parserWorker) {
        m_parserWorker->stop();
        m_parserWorker.reset();
    }
    if (m_comms) {
        m_comms->close();
        m_comms.reset();
    }
    const bool wasConnected = !m_serialPortSummary.isEmpty();
    m_serialPortSummary.clear();
    syncTelemetryStrip();
    if (wasConnected) {
        appendToLog(false, u"Serial port disconnected."_s);
    }
}
