#include "MainWindow.h"
<<<<<<< HEAD
<<<<<<< HEAD
#include "pages/MonitoringPage.h"   // Live telemetry overview.
#include "pages/FlightDataPage.h"    // Placeholder for ground-station settings.
#include "pages/ChartPage.h"       // Imaginary chart viewer until data is wired up.
#include "pages/SettingsPage.h"       // Imaginary chart viewer until data is wired up.
=======
#include "pages/DashboardPage.h"   // Live telemetry overview.
#include "pages/SettingsPage.h"    // Placeholder for ground-station settings.
#include "pages/ChartPage.h"       // Imaginary chart viewer until data is wired up.
>>>>>>> cb12191 (logistic files commit)
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDateTime>
#include <QFont>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
<<<<<<< HEAD
#include <QSize>
=======
>>>>>>> cb12191 (logistic files commit)
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStatusBar>
#include <QtGlobal>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>
<<<<<<< HEAD

using namespace Qt::StringLiterals;

// Entry point for the GUI shell; constructs the basic chrome and loads placeholder pages.
MainWindow::MainWindow(QWidget *parent): QMainWindow(parent) {
    setWindowTitle(u"CosmoSoft"_s);                                        // Title bar text so the window is identifiable.
    setWindowIcon(QIcon(":/images/Logo_rounded.png"));                            // Use the rounded logo bundled in resources.qrc.
    setupActions();                                                               // Prepare navigation commands first.
    setupToolbar();                                          // Install the toolbar directly under the title bar.
    setupDataBar();                                         // Build the telemetry strip that sits under the toolbar.
    setupPages();                                            // Fill the central widget with placeholder pages.
    statusBar()->showMessage(u"DO NOT FORGET TO CONNECT WIFI AND CABLE TO ROCKET."_s); // Friendly status message on boot.
}

void MainWindow::showStatusMessage(const QString &message, int timeout) {
    if (auto *sb = statusBar()) {
        sb->showMessage(message, timeout);
    }
}

void MainWindow::setupActions() {

    // Actions encapsulate the intent behind toolbar/menu buttons.
    m_showMonitoringAction = new QAction(u"Monitoring"_s, this);
    m_showMonitoringAction->setToolTip(u"Switch to the monitoring page."_s);

    m_showFlightDataAction = new QAction(u"Flight Data"_s, this);
    m_showFlightDataAction->setToolTip(u"Switch to the flight data page."_s);

    m_showChartAction = new QAction(u"Charts"_s, this);
    m_showChartAction->setToolTip(u"Switch to the charts page."_s);

    QIcon settingsIcon;
    settingsIcon.addFile(u":/icons/settings_button.png"_s, QSize(), QIcon::Normal, QIcon::Off);
    settingsIcon.addFile(u":/icons/settings_button_black.png"_s, QSize(), QIcon::Normal, QIcon::On);
    m_openSettingsAction = new QAction(settingsIcon, u"Settings"_s, this);
    m_openSettingsAction->setToolTip(u"Open the settings window."_s);
    m_openSettingsAction->setCheckable(true);

    // Each action simply points the stacked widget at the matching page.
    connect(m_showMonitoringAction, &QAction::triggered, this, [this]() {
        m_pages->setCurrentWidget(m_monitoringPage);
        statusBar()->showMessage(u"Monitoring page selected."_s, 2000);
    });

    connect(m_showFlightDataAction, &QAction::triggered, this, [this]() {
        m_pages->setCurrentWidget(m_flightDataPage);
        statusBar()->showMessage(u"Flight data page selected."_s, 2000);
    });

    connect(m_showChartAction, &QAction::triggered, this, [this]() {
        m_pages->setCurrentWidget(m_chartPage);
        statusBar()->showMessage(u"Chart page selected."_s, 2000);
    });

    connect(m_openSettingsAction, &QAction::triggered, this, [this]() {
        openSettingsWindow();
=======

#include <QAction>
#include <cmath>
#include <QLabel>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>
#include <QtMath>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QPainter>
#include <QStackedWidget>
=======
>>>>>>> cb12191 (logistic files commit)

using namespace Qt::StringLiterals;

// Entry point for the GUI shell; constructs the basic chrome and loads placeholder pages.
MainWindow::MainWindow(QWidget *parent)
        : QMainWindow(parent) {
<<<<<<< HEAD
    setWindowTitle(u"CosmoSoft UI Skeleton"_s);              // Title bar text so the window is identifiable.

    setupActions();                                          // Prepare navigation commands first.
    setupToolbar();                                          // Install the toolbar directly under the title bar.
    setupPages();                                            // Fill the central widget with placeholder pages.

    statusBar()->showMessage(u"Ready – explore the scaffolded UI."_s); // Friendly status message on boot.
=======
    setWindowTitle(u"CosmoSoft<style/>"_s);                                        // Title bar text so the window is identifiable.
    setWindowIcon(QIcon(":/images/Logo_rounded.png"));                            // Use the rounded logo bundled in resources.qrc.
    setupActions();                                                               // Prepare navigation commands first.
    setupToolbar();                                          // Install the toolbar directly under the title bar.
    setupPages();                                            // Fill the central widget with placeholder pages.
    statusBar()->showMessage(u"DO NOT FORGET TO CONNECT WIFI AND CABLE TO ROCKET."_s); // Friendly status message on boot.
>>>>>>> cb12191 (logistic files commit)
}

void MainWindow::setupActions() {
    // Actions encapsulate the intent behind toolbar/menu buttons.
    m_showDashboardAction = new QAction(u"Dashboard"_s, this);
    m_showDashboardAction->setToolTip(u"Switch to the dashboard page."_s);

    m_showSettingsAction = new QAction(u"Settings"_s, this);
    m_showSettingsAction->setToolTip(u"Switch to the settings page."_s);

    // Each action simply points the stacked widget at the matching page.
    connect(m_showDashboardAction, &QAction::triggered, this, [this]() {
        m_pages->setCurrentWidget(m_dashboardPage);
        statusBar()->showMessage(u"Dashboard page selected."_s, 2000);
    });

    connect(m_showSettingsAction, &QAction::triggered, this, [this]() {
        m_pages->setCurrentWidget(m_settingsPage);
        statusBar()->showMessage(u"Settings page selected."_s, 2000);
>>>>>>> 1c03da1 (UI Skeleton)
    });
}

void MainWindow::setupToolbar() {
<<<<<<< HEAD
<<<<<<< HEAD
=======
>>>>>>> cb12191 (logistic files commit)
    // QToolBar integrates directly with QMainWindow, so new users get docking,
    // layout management, and keyboard shortcuts “for free” without manual layout work.
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
<<<<<<< HEAD
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
=======
>>>>>>> cb12191 (logistic files commit)
        }
    )"_s);
    addToolBar(Qt::TopToolBarArea, toolbar);

<<<<<<< HEAD
    // TEXT SHADOWS.
=======
    // Add a subtle drop shadow to the toolbar for depth.
>>>>>>> cb12191 (logistic files commit)
    QGraphicsDropShadowEffect* text_shadow = new QGraphicsDropShadowEffect(this);
    text_shadow->setBlurRadius(5);
    text_shadow->setColor(QColor(0, 0, 0, 160));
    text_shadow->setOffset(1, 1);

    auto *content = new QWidget(toolbar);
    content->setObjectName(u"toolbarContent"_s);
    auto *contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(24);

<<<<<<< HEAD
=======


>>>>>>> cb12191 (logistic files commit)
    // Group the logo/mission labels inside their own QWidget so the stylesheet can target them easily.
    auto *brandBlock = new QWidget(content);
    brandBlock->setObjectName(u"brandBlock"_s);
    brandBlock->setGraphicsEffect(text_shadow);
    auto *brandLayout = new QVBoxLayout(brandBlock);
    brandLayout->setContentsMargins(0, 0, 0, 0);
    brandLayout->setSpacing(2);
    auto *brandLabel = new QLabel(u"Cosmo<span style=\"color:#000000\">Soft</span>"_s, brandBlock);
    brandLabel->setObjectName(u"brandLabel"_s);
    brandLabel->setTextFormat(Qt::RichText);
    // Fonts are registered in main.cpp; expose the resolved family via qApp so we don’t need global singletons.
    const QVariant workbenchFamily = qApp->property("workbenchFontFamily");
    if (workbenchFamily.isValid()) {
        QFont brandFont = brandLabel->font();
        brandFont.setFamily(workbenchFamily.toString());
        brandFont.setPointSize(26);
        brandFont.setBold(true);
        brandLabel->setFont(brandFont);
    }
    brandLayout->addWidget(brandLabel);

    // Mission meta line: show the live UTC clock so UI feels tethered to ground ops.
    m_missionMetaLabel = new QLabel(u"GMT: --:--:-- | -- --- ----"_s, brandBlock);
    m_missionMetaLabel->setObjectName(u"missionMeta"_s);
    m_missionMetaLabel->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    brandLayout->addWidget(m_missionMetaLabel);
    contentLayout->addWidget(brandBlock);

    updateMissionClock();  // Seed immediately so the label never shows placeholder data.
    if (!m_missionClockTimer) {
        m_missionClockTimer = new QTimer(this);
        m_missionClockTimer->setInterval(1000);  // Update every second to keep HH:mm:ss accurate.
        connect(m_missionClockTimer, &QTimer::timeout, this, &MainWindow::updateMissionClock);
        m_missionClockTimer->start();
    }

    contentLayout->addStretch(1);

    // QActionGroup locks the nav buttons into a radio-group so only one destination can be “checked” at a time.
    auto *navGroup = new QActionGroup(this);
    navGroup->setExclusive(true);
    for (auto *action : {m_showMonitoringAction, m_showFlightDataAction, m_showChartAction}) {
        action->setCheckable(true);
        navGroup->addAction(action);
    }
    m_showMonitoringAction->setChecked(true);

    // Helper to wrap each QAction inside a QToolButton; QMainWindow handles shortcuts/enable state automatically.
<<<<<<< HEAD
    auto makeNavButton = [](QAction *action,
            QWidget *parent,
            Qt::ToolButtonStyle style = Qt::ToolButtonTextOnly,
            QString kind = u"navButton"_s,
            QSize iconSize = QSize()) {
=======
    auto makeNavButton = [](QAction *action, QWidget *parent) {
>>>>>>> cb12191 (logistic files commit)
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
    navLayout->addWidget(makeNavButton(m_showChartAction, navContainer));
    navLayout->addWidget(makeNavButton(m_openSettingsAction, navContainer, Qt::ToolButtonIconOnly, u"iconButton"_s, QSize(44, 44)));

    contentLayout->addWidget(navContainer);

    toolbar->addWidget(content);
}

void MainWindow::setupDataBar() {
    if (m_dataBar) {
        return; // Nothing to do if we've already built it.
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

    m_dataLinkStatusLabel = buildBadgeLabel(u"DATA BAR. MAYBE... in future"_s, m_dataBar);
    m_dataRateLabel = buildBadgeLabel(u"RATE: --"_s, m_dataBar);
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
    // QStackedWidget is the Qt6 “page router”: we add each QWidget once and flip between them with setCurrentWidget().
    auto *central = new QWidget(this);
    auto *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);

    if (!m_dataBar) {
        setupDataBar();  // Ensure strip exists before wiring layout.
    }
    if (m_dataBar) {
        centralLayout->addWidget(m_dataBar);
    }

    m_pages = new QStackedWidget(central);                 // Central stacked widget lives inside MainWindow.
    centralLayout->addWidget(m_pages, /*stretch=*/1);
    setCentralWidget(central);

    // Each page lives in its own QWidget subclass so logic stays modular.
    m_monitoringPage = new MonitoringPage(this);
    m_flightDataPage = new FlightDataPage(this);
    m_chartPage = new ChartPage(this);

    // Order determines indices; we keep all pages accessible via actions.
    m_pages->addWidget(m_monitoringPage);
    m_pages->addWidget(m_flightDataPage);
    m_pages->addWidget(m_chartPage);
    m_pages->setCurrentWidget(m_monitoringPage);          // Default landing page.
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

        connect(m_settingsWindow, &QObject::destroyed, this, [this]() {
            m_settingsWindow = nullptr;
            if (m_openSettingsAction) {
                m_openSettingsAction->setChecked(false);
            }
        });
    }

    m_settingsWindow->show();
    m_settingsWindow->raise();
    m_settingsWindow->activateWindow();
    m_openSettingsAction->setChecked(true);
    statusBar()->showMessage(u"Settings window opened."_s, 2000);
}

// Compute and inject the current local timestamp plus GMT offset into the mission meta label.
void MainWindow::updateMissionClock() {
    if (!m_missionMetaLabel) {
        return;  // Toolbar was not built yet; nothing to update.
    }

    const QDateTime localNow = QDateTime::currentDateTime();
    const int offsetSeconds = localNow.offsetFromUtc();
    const int absOffsetSeconds = qAbs(offsetSeconds);
    const int offsetHours = absOffsetSeconds / 3600;
    const int offsetMinutes = (absOffsetSeconds % 3600) / 60;

    // Format GMT±HH[:MM] so even half-hour zones look correct.
    QString offsetString = QStringLiteral("GMT%1%2")
            .arg(offsetSeconds >= 0 ? u'+' : u'-')
            .arg(offsetHours, 2, 10, QLatin1Char('0'));
    if (offsetMinutes > 0) {
        offsetString += QStringLiteral(":%1").arg(offsetMinutes, 2, 10, QLatin1Char('0'));
    }

    const QString timestamp = QStringLiteral("%1 | %2")
            .arg(offsetString, localNow.toString(u"HH:mm:ss | dd MMM yyyy"_s));
    m_missionMetaLabel->setText(timestamp);
=======
    auto *toolbar = addToolBar(u"Main Toolbar"_s);      // QMainWindow handles lifetime.
    toolbar->setMovable(false);                         // Keep the toolbar docked for now.

    toolbar->addAction(m_showDashboardAction);          // Primary navigation button.
    toolbar->addAction(m_showSettingsAction);           // Secondary navigation button.

    // The next actions are placeholders that hint at future functionality.
    auto *chartsAction = toolbar->addAction(u"Charts"_s);
    chartsAction->setEnabled(false);
    chartsAction->setToolTip(u"Placeholder for chart tools."_s);

    auto *logsAction = toolbar->addAction(u"Logs"_s);
    logsAction->setEnabled(false);
    logsAction->setToolTip(u"Placeholder for log viewer."_s);
}

void MainWindow::setupPages() {
    // QStackedWidget is the Qt6 “page router”: we add each QWidget once and flip between them with setCurrentWidget().
    m_pages = new QStackedWidget(this);                 // Central stacked widget lives inside MainWindow.
    setCentralWidget(m_pages);

    // Dashboard placeholder – a simple column of informative labels.
    m_dashboardPage = new QWidget(this);
    auto *dashboardLayout = new QVBoxLayout(m_dashboardPage);
    dashboardLayout->addWidget(new QLabel(u"Dashboard placeholder."_s, m_dashboardPage));
    dashboardLayout->addWidget(new QLabel(u"Add telemetry summaries and widgets here."_s, m_dashboardPage));

    // Settings placeholder – another simple column layout.
    m_settingsPage = new QWidget(this);
    auto *settingsLayout = new QVBoxLayout(m_settingsPage);
    settingsLayout->addWidget(new QLabel(u"Settings placeholder."_s, m_settingsPage));
    settingsLayout->addWidget(new QLabel(u"Add configuration controls here."_s, m_settingsPage));

    setupChartPage();                                     // Creates m_chartPage with a simple chart.

    // Order determines indices; we keep all pages accessible via actions.
    m_pages->addWidget(m_dashboardPage);
    m_pages->addWidget(m_settingsPage);
    m_pages->addWidget(m_chartPage);
    m_pages->setCurrentWidget(m_dashboardPage);          // Default landing page.
}

<<<<<<< HEAD
void MainWindow::setupChartPage() {
    // Build a line series with sample data (sine wave to mimic telemetry variation).
    auto *series = new QLineSeries(this);
    series->setName(u"Sample Telemetry"_s);

    for (int degrees = 0; degrees <= 360; degrees += 30) {
        const double radians = qDegreesToRadians(static_cast<double>(degrees));
        series->append(degrees, std::sin(radians));
    }

    auto *chart = new QChart();
    chart->addSeries(series);
    chart->setTitle(u"Flight Altitude Trend (placeholder data)"_s);

    // X axis reports the sample angle; in real data this would be time or packet count.
    auto *axisX = new QValueAxis();
    axisX->setTitleText(u"Sample (degrees placeholder)"_s);
    axisX->setTickCount(series->count());
    chart->addAxis(axisX, Qt::AlignBottom);
    series->attachAxis(axisX);

    // Y axis shows the sine output so we can see a wave.
    auto *axisY = new QValueAxis();
    axisY->setTitleText(u"Altitude (normalized)"_s);
    axisY->setRange(-1.1, 1.1);
    chart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisY);

    chart->legend()->setVisible(true);
    chart->legend()->setAlignment(Qt::AlignBottom);

    auto *chartView = new QChartView(chart);
    chartView->setRenderHint(QPainter::Antialiasing);    // Smooth lines for a nicer look.

    m_chartPage = new QWidget(this);
    auto *chartLayout = new QVBoxLayout(m_chartPage);
    chartLayout->addWidget(new QLabel(u"Telemetry Chart"_s, m_chartPage));
    chartLayout->addWidget(chartView);
    chartLayout->addWidget(new QLabel(u"Replace this sample with live data when ready."_s, m_chartPage));
>>>>>>> 1c03da1 (UI Skeleton)
=======
// Compute and inject the current local timestamp plus GMT offset into the mission meta label.
void MainWindow::updateMissionClock() {
    if (!m_missionMetaLabel) {
        return;  // Toolbar was not built yet; nothing to update.
    }

    const QDateTime localNow = QDateTime::currentDateTime();
    const int offsetSeconds = localNow.offsetFromUtc();
    const int absOffsetSeconds = qAbs(offsetSeconds);
    const int offsetHours = absOffsetSeconds / 3600;
    const int offsetMinutes = (absOffsetSeconds % 3600) / 60;

    // Format GMT±HH[:MM] so even half-hour zones look correct.
    QString offsetString = QStringLiteral("GMT%1%2")
            .arg(offsetSeconds >= 0 ? u'+' : u'-')
            .arg(offsetHours, 2, 10, QLatin1Char('0'));
    if (offsetMinutes > 0) {
        offsetString += QStringLiteral(":%1").arg(offsetMinutes, 2, 10, QLatin1Char('0'));
    }

    const QString timestamp = QStringLiteral("%1 | %2")
            .arg(offsetString, localNow.toString(u"HH:mm:ss | dd MMM yyyy"_s));
    m_missionMetaLabel->setText(timestamp);
>>>>>>> cb12191 (logistic files commit)
}
