#ifndef COSMO_SOFT_MAINWINDOW_H
#define COSMO_SOFT_MAINWINDOW_H

#include <QMainWindow>    // Base class that already owns menu/status bars and a central widget slot.

class QAction;
class QLabel;
class QStackedWidget;
class QTimer;
class QWidget;
class DashboardPage;
class SettingsPage;
class ChartPage;

// MainWindow assembles the high-level Qt UI skeleton (toolbar, stacked pages, and a chart demo).
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

private:
    void setupActions();     // Create QAction objects that will drive toolbar navigation.
    void setupToolbar();     // Build the visible toolbar with skeleton buttons.
    void setupPages();       // Construct the stacked pages that behave like separate windows.
    void updateMissionClock();  // Refresh the GMT label with the current UTC timestamp.

    QAction *m_showDashboardAction = nullptr;
    QAction *m_showSettingsAction = nullptr;
    QAction *m_showChartAction = nullptr;

    QStackedWidget *m_pages = nullptr;
    DashboardPage *m_dashboardPage = nullptr;
    SettingsPage *m_settingsPage = nullptr;
    ChartPage *m_chartPage = nullptr;

    QLabel *m_missionMetaLabel = nullptr;   // Pointer to the GMT readout in the toolbar.
    QTimer *m_missionClockTimer = nullptr;  // Ticks every second to update the UTC timestamp.
};

#endif // COSMO_SOFT_MAINWINDOW_H
