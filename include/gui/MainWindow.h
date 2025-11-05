#ifndef COSMO_SOFT_MAINWINDOW_H
#define COSMO_SOFT_MAINWINDOW_H

#include <QMainWindow>    // Base class that already owns menu/status bars and a central widget slot.

class QAction;
class QStackedWidget;
class QWidget;

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
    void setupChartPage();   // Prepare the sample chart content page.

    QAction *m_showDashboardAction = nullptr;
    QAction *m_showSettingsAction = nullptr;

    QStackedWidget *m_pages = nullptr;
    QWidget *m_dashboardPage = nullptr;
    QWidget *m_settingsPage = nullptr;
    QWidget *m_chartPage = nullptr;
};

#endif // COSMO_SOFT_MAINWINDOW_H
