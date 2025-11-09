#include "MainWindow.h"

#include "pages/DashboardPage.h"
#include "pages/SettingsPage.h"
#include "pages/ChartPage.h"

#include <QAction>
#include <QIcon>
#include <QStatusBar>
#include <QToolBar>
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
    m_showDashboardAction = new QAction(u"Dashboard"_s, this);
    m_showDashboardAction->setToolTip(u"Switch to the dashboard page."_s);

    m_showSettingsAction = new QAction(u"Settings"_s, this);
    m_showSettingsAction->setToolTip(u"Switch to the settings page."_s);

    m_showChartAction = new QAction(u"Charts"_s, this);
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
    auto *toolbar = new QToolBar(u"Main Toolbar"_s, this); // QMainWindow handles lifetime.
    toolbar->setObjectName(u"mainToolbar"_s);
    toolbar->setMovable(false);                           // Keep the toolbar docked for now.
    toolbar->setOrientation(Qt::Horizontal);              // Place buttons in a column on the right.
    toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);  // Use text-only pills for now.
    toolbar->setStyleSheet(uR"(
        QToolBar {
            background: rgba(75, 75, 75, 95);
            spacing: 10px;
        }
        QToolButton {
            border: 2px solid white;
            border-radius: 6px;
            padding: 6px 12px;
            color: white;
        }
        QToolButton:disabled {
            border-color: rgba(255, 255, 255, 80);
            color: rgba(255, 255, 255, 150);
        }
    )"_s);
    addToolBar(Qt::RightToolBarArea, toolbar);            // Dock the toolbar on the right edge.

    toolbar->addAction(m_showDashboardAction);          // Primary navigation button.
    toolbar->addAction(m_showSettingsAction);           // Secondary navigation button.
    toolbar->addAction(m_showChartAction);              // Chart page navigation button.

    // Placeholder slot for future features that are not wired yet.
    auto *logsAction = toolbar->addAction(u"Logs"_s);
    logsAction->setEnabled(false);
    logsAction->setToolTip(u"Placeholder for log viewer."_s);
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
