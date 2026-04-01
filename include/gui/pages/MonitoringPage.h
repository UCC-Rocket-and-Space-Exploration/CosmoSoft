#ifndef COSMO_SOFT_MONITORINGPAGE_H
#define COSMO_SOFT_MONITORINGPAGE_H

#include <QWidget>

#include "domain/FlightSample.h"

class QPaintEvent;

class QLabel;
class MainWindow;
class FlightDataModel;

class MonitoringPage : public QWidget {
    Q_OBJECT

public:
    explicit MonitoringPage(MainWindow *hostWindow, FlightDataModel *model, QWidget *parent = nullptr);
    ~MonitoringPage() override = default;

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void onSampleUpdated(const FlightSample &sample);

private:
    MainWindow *m_hostWindow = nullptr;
    FlightDataModel *m_model = nullptr;
    QLabel *m_summaryLabel = nullptr;
};

#endif // COSMO_SOFT_MONITORINGPAGE_H
