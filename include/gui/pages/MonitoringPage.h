#ifndef COSMO_SOFT_MONITORINGPAGE_H
#define COSMO_SOFT_MONITORINGPAGE_H

#include <QWidget>

#include <array>

#include "domain/FlightSample.h"

class QPaintEvent;
class QLabel;
class MainWindow;
class FlightDataModel;
class StatTileWidget;

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
    static constexpr int kTileCount = 9;

    MainWindow      *m_hostWindow  = nullptr;
    FlightDataModel *m_model       = nullptr;
    QLabel          *m_statusLabel = nullptr;

    std::array<StatTileWidget *, kTileCount> m_tiles{};
};

#endif // COSMO_SOFT_MONITORINGPAGE_H
