#include "gui/MainWindow.h"

#include "domain/FlightSample.h"
#include "domain/FlightSession.h"
#include "gateway/comms/CommsFactory.h"
#include "gateway/comms/ISerialPortScanner.h"
#include "gateway/comms/SerialPortScannerFactory.h"
#include "gui/FlightDataModel.h"
#include "gui/FlightReplayController.h"
#include "gui/pages/DashboardPage.h"
#include "gui/pages/MonitoringPage.h"
#include "gui/pages/SettingsPage.h"
#include "services/comms/SerialWorker.h"
#include "services/import/SampleFileLoader.h"
#include "services/telemetry/Framer.h"
#include "services/telemetry/Parser.h"
#include "services/telemetry/ParserWorker.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QMessageBox>
#include <QFont>
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

using namespace Qt::StringLiterals;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_flightModel(std::make_unique<FlightDataModel>(this)),
      m_replay(std::make_unique<FlightReplayController>(this)) {
    setWindowTitle(u"CosmoSoft"_s);
    setWindowIcon(QIcon(u":/images/Logo_rounded.png"_s));

    setupActions();
    setupToolbar();
    setupDataBar();
    setupPages();

    connect(m_replay.get(), &FlightReplayController::positionChanged, this, &MainWindow::onReplayPositionChanged);

    m_dataRateTimer = new QTimer(this);
    m_dataRateTimer->setInterval(1000);
    connect(m_dataRateTimer, &QTimer::timeout, this, &MainWindow::updateDataRateLabel);
    m_dataRateTimer->start();

    statusBar()->showMessage(u"DO NOT FORGET TO CONNECT WIFI AND CABLE TO ROCKET."_s);
}

MainWindow::~MainWindow() {
    stopSerial();
}

void MainWindow::showStatusMessage(const QString &message, int timeout) {
    if (auto *sb = statusBar()) {
        sb->showMessage(message, timeout);
    }
}

void MainWindow::onParserError(const QString &message) {
    showStatusMessage(message, 5000);
}

void MainWindow::setupActions() {
    m_showMonitoringAction = new QAction(u"Monitoring"_s, this);
    m_showMonitoringAction->setToolTip(u"Switch to the monitoring page."_s);

    m_showFlightDataAction = new QAction(u"Flight data"_s, this);
    m_showFlightDataAction->setToolTip(u"Switch to flight data and charts."_s);

    QIcon settingsIcon;
    settingsIcon.addFile(u":/icons/settings_button.png"_s, QSize(), QIcon::Normal, QIcon::Off);
    settingsIcon.addFile(u":/icons/settings_button_black.png"_s, QSize(), QIcon::Normal, QIcon::On);
    m_openSettingsAction = new QAction(settingsIcon, u"Settings"_s, this);
    m_openSettingsAction->setToolTip(u"Open the settings window."_s);
    m_openSettingsAction->setCheckable(true);

    connect(m_showMonitoringAction, &QAction::triggered, this, [this]() {
        m_pages->setCurrentWidget(m_monitoringPage);
        statusBar()->showMessage(u"Monitoring page selected."_s, 2000);
    });

    connect(m_showFlightDataAction, &QAction::triggered, this, [this]() {
        m_pages->setCurrentWidget(m_flightDataPage);
        statusBar()->showMessage(u"Flight data page selected."_s, 2000);
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
    navGroup->addAction(m_showMonitoringAction);
    navGroup->addAction(m_showFlightDataAction);
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

    m_dataLinkStatusLabel = buildBadgeLabel(u"LINK: idle"_s, m_dataBar);
    m_dataRateLabel = buildBadgeLabel(u"RATE: -- B/s"_s, m_dataBar);
    dataLayout->addWidget(m_dataLinkStatusLabel);
    dataLayout->addWidget(m_dataRateLabel);
    dataLayout->addStretch(1);

    m_dataBar->setStyleSheet(uR"(
        QWidget#telemetryStrip {
            background: rgba(26, 26, 26, 0.95);
            color: #f0f0f0;
            border-top: 1px solid rgba(255, 255, 255, 0.08);
            border-bottom: 1px solid rgba(0, 0, 0, 0.7);
        }

        QWidget#telemetryStrip QLabel#telemetryBadge {
            font-size: 12px;
            color: #f7f7f7;
            letter-spacing: 1px;
            font-family: "Red Hat Mono", "Courier New", "Roboto Mono", monospace;
            }
    )"_s);
}

void MainWindow::setupPages() {
    auto *central = new QWidget(this);
    auto *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);

    if (!m_dataBar) {
        setupDataBar();
    }
    if (m_dataBar) {
        centralLayout->addWidget(m_dataBar);
    }

    m_pages = new QStackedWidget(central);
    centralLayout->addWidget(m_pages, 1);
    setCentralWidget(central);

    m_monitoringPage = new MonitoringPage(this, m_flightModel.get());
    m_flightDataPage = new DashboardPage(m_flightModel.get(), m_replay.get());
    m_pages->addWidget(m_monitoringPage);
    m_pages->addWidget(m_flightDataPage);
    m_pages->setCurrentWidget(m_monitoringPage);
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
        m_settingsWindow->resize(520, 600);

        connect(m_settingsWindow, &SettingsPage::refreshPortsRequested, this, &MainWindow::refreshSerialPorts);
        connect(m_settingsWindow, &SettingsPage::connectRequested, this, &MainWindow::startSerial);
        connect(m_settingsWindow, &SettingsPage::disconnectRequested, this, &MainWindow::stopSerial);
        connect(m_settingsWindow, &SettingsPage::openReplayFileRequested, this, &MainWindow::onOpenReplayFile);
        connect(m_settingsWindow, &SettingsPage::clearFlightDataRequested, this, &MainWindow::onClearFlightData);

        connect(m_settingsWindow, &QObject::destroyed, this, [this]() {
            m_settingsWindow = nullptr;
            if (m_openSettingsAction) {
                m_openSettingsAction->setChecked(false);
            }
        });

        refreshSerialPorts();
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
    m_dataRateLabel->setText(QStringLiteral("RATE: %1 B/s").arg(delta));
}

void MainWindow::refreshSerialPorts() {
    std::unique_ptr<ISerialPortScanner> scanner(SerialPortScannerFactory::createSerialPortScanner());
    if (!scanner || !m_settingsWindow) {
        return;
    }
    QStringList ports;
    for (const auto &p : scanner->enumeratePorts()) {
        ports.append(QString::fromStdString(p));
    }
    m_settingsWindow->setPortNames(ports);
}

void MainWindow::onReplayPositionChanged(int trailLength) {
    if (trailLength <= 0) {
        m_flightModel->setDisplayedSample(FlightSample{});
        return;
    }
    if (trailLength > static_cast<int>(m_loadedSession.samples.size())) {
        return;
    }
    m_flightModel->setDisplayedSample(m_loadedSession.samples[static_cast<std::size_t>(trailLength - 1)]);
}

void MainWindow::onOpenReplayFile() {
    QString startDir;
    if (m_settingsWindow) {
        startDir = m_settingsWindow->replayDirectory();
    }
    if (startDir.isEmpty()) {
        startDir = QDir::homePath();
    }

    const QString path = QFileDialog::getOpenFileName(
        m_settingsWindow ? static_cast<QWidget *>(m_settingsWindow) : this,
        u"Open flight log"_s,
        startDir,
        u"Flight logs (*.csv *.telem);;CSV (*.csv);;TELEM (*.telem);;All files (*)"_s);
    if (path.isEmpty()) {
        return;
    }

    stopSerial();

    FlightSession session;
    std::optional<std::string> err;

    if (path.endsWith(u".telem", Qt::CaseInsensitive)) {
        Framer framer;
        Parser parser;
        err = SampleFileLoader::loadTelemFile(path.toStdString(), session, framer, parser);
    } else {
        err = SampleFileLoader::loadTheseusCsv(path.toStdString(), session);
    }

    if (err) {
        QMessageBox::warning(
            m_settingsWindow ? static_cast<QWidget *>(m_settingsWindow) : this,
            u"Could not load log"_s,
            QString::fromStdString(*err));
        return;
    }

    m_loadedSession = std::move(session);
    m_flightModel->resetSession();
    m_flightModel->setReplayMode(true);
    m_replay->setSession(m_loadedSession);
    if (m_flightDataPage) {
        m_flightDataPage->setReplaySession(&m_loadedSession);
    }
    showStatusMessage(QStringLiteral("Loaded flight: %1").arg(path), 4000);
}

void MainWindow::onClearFlightData() {
    m_replay->stop();
    m_loadedSession.samples.clear();
    m_replay->setSession({});
    m_flightModel->setReplayMode(false);
    m_flightModel->resetSession();
    if (m_flightDataPage) {
        m_flightDataPage->setReplaySession(nullptr);
    }
    showStatusMessage(u"Cleared flight replay data."_s, 2000);
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

    if (m_dataLinkStatusLabel) {
        m_dataLinkStatusLabel->setText(QStringLiteral("LINK: %1 @ %2").arg(portName).arg(baud));
    }
    m_flightModel->setReplayMode(false);
    if (m_flightDataPage) {
        m_flightDataPage->setReplaySession(nullptr);
    }
    if (m_settingsWindow) {
        m_settingsWindow->setSerialLinkStatus(QStringLiteral("Connected: %1 @ %2").arg(portName).arg(baud));
    }
    showStatusMessage(QStringLiteral("Connected to %1").arg(portName), 3000);
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
    if (m_dataLinkStatusLabel) {
        m_dataLinkStatusLabel->setText(u"LINK: idle"_s);
    }
    if (m_settingsWindow) {
        m_settingsWindow->setSerialLinkStatus(u"Disconnected."_s);
    }
}
