#ifndef COSMO_SOFT_MONITORINGPAGE_H
#define COSMO_SOFT_MONITORINGPAGE_H

#include <QWidget>

class QPaintEvent;

class MainWindow;

// MonitoringPage hosts summary tiles and checklists displayed on the main screen.
// The backend can later expose data setters so these labels update with live telemetry.
class MonitoringPage : public QWidget {
    Q_OBJECT

public:
    explicit MonitoringPage(MainWindow *hostWindow, QWidget *parent = nullptr);
    ~MonitoringPage() override = default;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    MainWindow *m_hostWindow = nullptr;
};

#endif // COSMO_SOFT_MONITORINGPAGE_H


