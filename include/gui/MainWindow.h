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

// MainWindow assembles the high-level Qt UI skeleton (toolbar, stacked page, and settings entry point).
<<<<<<< HEAD
=======

class QAction;
class QLabel;
class QStackedWidget;
class QTimer;
class QWidget;

// MainWindow assembles the high-level Qt UI skeleton (toolbar, stacked pages, and a chart demo).
>>>>>>> 1c03da1 (UI Skeleton)
=======
>>>>>>> 495a53e (remove some pages)
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
    void openSettingsWindow(); // Launch the detached settings window.
    void updateMissionClock();  // Refresh the GMT label with the current UTC timestamp.

    QAction *m_showMonitoringAction = nullptr;
    QAction *m_openSettingsAction = nullptr;

    QStackedWidget *m_pages = nullptr;
    MonitoringPage *m_monitoringPage = nullptr;
    SettingsPage *m_settingsWindow = nullptr;

    QLabel *m_missionMetaLabel = nullptr;   // Pointer to the GMT readout in the toolbar.
    QTimer *m_missionClockTimer = nullptr;  // Ticks every second to update the UTC timestamp.

    QWidget *m_dataBar = nullptr;           // Thin strip shown under the toolbar.
    QLabel *m_dataLinkStatusLabel = nullptr;
    QLabel *m_dataRateLabel = nullptr;
};

#endif // COSMO_SOFT_MAINWINDOW_H
