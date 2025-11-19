#ifndef COSMO_SOFT_MAINWINDOW_H
#define COSMO_SOFT_MAINWINDOW_H

#include <QMainWindow>    // Base class that already owns menu/status bars and a central widget slot.
#include <QString>

class QAction;
class QLabel;
class QStackedWidget;
class QTimer;
class QWidget;
class MonitoringPage;
class SettingsPage;
class ChartPage;

// MainWindow assembles the high-level Qt UI skeleton (toolbar, stacked pages, and a chart demo).
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    void showStatusMessage(const QString &message, int timeout = 0);
    ~MainWindow() override = default;

private:
    void setupActions();     // Create QAction objects that will drive toolbar navigation.
    void setupToolbar();     // Build the visible toolbar with skeleton buttons.
    void setupDataBar();     // Create the thin telemetry strip that sits under the toolbar.
    void setupPages();       // Construct the stacked pages that behave like separate windows.
    void updateMissionClock();  // Refresh the GMT label with the current UTC timestamp.

    QAction *m_showMonitoringAction = nullptr;
    QAction *m_showSettingsAction = nullptr;
    QAction *m_showChartAction = nullptr;

    QStackedWidget *m_pages = nullptr;
    MonitoringPage *m_monitoringPage = nullptr;
    SettingsPage *m_settingsPage = nullptr;
    ChartPage *m_chartPage = nullptr;

    QLabel *m_missionMetaLabel = nullptr;   // Pointer to the GMT readout in the toolbar.
    QTimer *m_missionClockTimer = nullptr;  // Ticks every second to update the UTC timestamp.

    QWidget *m_dataBar = nullptr;           // Thin strip shown under the toolbar.
    QLabel *m_dataLinkStatusLabel = nullptr;
    QLabel *m_dataRateLabel = nullptr;
};

#endif // COSMO_SOFT_MAINWINDOW_H
