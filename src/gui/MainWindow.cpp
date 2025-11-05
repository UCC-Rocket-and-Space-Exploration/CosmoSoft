#include "MainWindow.h"

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

using namespace Qt::StringLiterals;

MainWindow::MainWindow(QWidget *parent)
        : QMainWindow(parent) {
    setWindowTitle(u"CosmoSoft UI Skeleton"_s);              // Title bar text so the window is identifiable.

    setupActions();                                          // Prepare navigation commands first.
    setupToolbar();                                          // Install the toolbar directly under the title bar.
    setupPages();                                            // Fill the central widget with placeholder pages.

    statusBar()->showMessage(u"Ready – explore the scaffolded UI."_s); // Friendly status message on boot.
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
    });
}

void MainWindow::setupToolbar() {
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
}
