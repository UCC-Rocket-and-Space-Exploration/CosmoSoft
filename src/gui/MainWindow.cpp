#include "MainWindow.h"
#include "pages/DashboardPage.h"
#include "pages/SettingsPage.h"
#include "pages/ChartPage.h"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QFont>
#include <QIcon>
#include <QLabel>
#include <QHBoxLayout>
#include <QSizePolicy>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>
#include <QStackedWidget>

using namespace Qt::StringLiterals;

MainWindow::MainWindow(QWidget *parent)
        : QMainWindow(parent) {
    setWindowTitle(u"CosmoSoft"_s);              // Title bar text so the window is identifiable.
    setWindowIcon(QIcon(":/images/Logo_rounded.png"));       // Use the rounded logo bundled in resources.qrc.

    setupActions();                                          // Prepare navigation commands first.
    setupToolbar();                                          // Install the toolbar directly under the title bar.
    setupPages();                                            // Fill the central widget with placeholder pages.

    statusBar()->showMessage(u"DO NOT FORGET TO CONNECT WIFI AND CABLE TO ROCKET."_s); // Friendly status message on boot.
}

void MainWindow::setupActions() {

    // Actions encapsulate the intent behind toolbar/menu buttons.
    m_showDashboardAction = new QAction(u"Monitoring"_s, this);
    m_showDashboardAction->setToolTip(u"Switch to the dashboard page."_s);

    m_showSettingsAction = new QAction(u"Flight Data"_s, this);
    m_showSettingsAction->setToolTip(u"Switch to the flight data (settings placeholder) page."_s);

    m_showChartAction = new QAction(u"Coolstuff"_s, this);
    m_showChartAction->setToolTip(u"Switch to the charts page."_s);

    // Each action simply points the stacked widget at the matching page.
    connect(m_showDashboardAction, &QAction::triggered, this, [this]() {
        m_pages->setCurrentWidget(m_dashboardPage);
        statusBar()->showMessage(u"Dashboard page selected."_s, 2000);
    });

    connect(m_showSettingsAction, &QAction::triggered, this, [this]() {
        m_pages->setCurrentWidget(m_settingsPage);
        statusBar()->showMessage(u"Settings page selected."_s, 2000);
    });

    connect(m_showChartAction, &QAction::triggered, this, [this]() {
        m_pages->setCurrentWidget(m_chartPage);
        statusBar()->showMessage(u"Chart page selected."_s, 2000);
    });
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
            font-weight: 700;
            font-family: "Workbench","Courier New", "Roboto Mono", monospace;
            letter-spacing: 2px;
            color: #f2f2f2;
        }

        QWidget#brandBlock QLabel#brandLabelBlack {
                    color: #f2f2f2;

        }

        QWidget#brandBlock QLabel#missionMeta {
            font-size: 14px;
            color: #000000ff;
            font-family: "Red Hat Mono", "Courier New", "Roboto Mono", monospace;
        }

        QToolButton[kind="navButton"] {
            font-size: 12px;
            min-width: 150px;
            padding: 10px 15px;
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
            background-color: #3b3b3b;
        }
    )"_s);
    addToolBar(Qt::TopToolBarArea, toolbar);

    auto *content = new QWidget(toolbar);
    content->setObjectName(u"toolbarContent"_s);
    auto *contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(24);

    auto *brandBlock = new QWidget(content);
    brandBlock->setObjectName(u"brandBlock"_s);
    auto *brandLayout = new QVBoxLayout(brandBlock);
    brandLayout->setContentsMargins(0, 0, 0, 0);
    brandLayout->setSpacing(2);
    auto *brandLabel = new QLabel(u"Cosmo","Soft"_s, brandBlock);
    brandLabel->setObjectName(u"brandLabel"_s);
    const QVariant workbenchFamily = qApp->property("workbenchFontFamily");
    if (workbenchFamily.isValid()) {
        QFont brandFont = brandLabel->font();
        brandFont.setFamily(workbenchFamily.toString());
        brandFont.setPointSize(26);
        brandFont.setBold(true);
        brandLabel->setFont(brandFont);
    }
    brandLayout->addWidget(brandLabel);
    auto *missionMeta = new QLabel(
            u"Mission: N/A | GMT: N/A | TELEMETRY <span style=\"color:#ff5f5f;\">UNKNOWN</span>"_s,
            brandBlock);
    missionMeta->setObjectName(u"missionMeta"_s);
    missionMeta->setTextFormat(Qt::RichText);
    brandLayout->addWidget(missionMeta);
    contentLayout->addWidget(brandBlock);

    contentLayout->addStretch(1);

    auto *navGroup = new QActionGroup(this);
    navGroup->setExclusive(true);
    for (auto *action : {m_showDashboardAction, m_showSettingsAction, m_showChartAction}) {
        action->setCheckable(true);
        navGroup->addAction(action);
    }
    m_showDashboardAction->setChecked(true);

    auto makeNavButton = [](QAction *action, QWidget *parent) {
        auto *button = new QToolButton(parent);
        button->setProperty("kind", u"navButton"_s);
        button->setAutoRaise(false);
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setDefaultAction(action);
        return button;
    };

    auto *navContainer = new QWidget(content);
    auto *navLayout = new QHBoxLayout(navContainer);
    navLayout->setContentsMargins(0, 0, 0, 0);
    navLayout->setSpacing(12);
    navLayout->addWidget(makeNavButton(m_showDashboardAction, navContainer));
    navLayout->addWidget(makeNavButton(m_showSettingsAction, navContainer));

    navLayout->addWidget(makeNavButton(m_showChartAction, navContainer));

    contentLayout->addWidget(navContainer);

    toolbar->addWidget(content);
}

void MainWindow::setupPages() {
    m_pages = new QStackedWidget(this);                 // Central stacked widget lives inside MainWindow.
    setCentralWidget(m_pages);

    // Each page lives in its own QWidget subclass so logic stays modular.
    m_dashboardPage = new DashboardPage(this);
    m_settingsPage = new SettingsPage(this);
    m_chartPage = new ChartPage(this);

    // Order determines indices; we keep all pages accessible via actions.
    m_pages->addWidget(m_dashboardPage);
    m_pages->addWidget(m_settingsPage);
    m_pages->addWidget(m_chartPage);
    m_pages->setCurrentWidget(m_dashboardPage);          // Default landing page.
}
