#ifndef COSMO_SOFT_DASHBOARDPAGE_H
#define COSMO_SOFT_DASHBOARDPAGE_H

#include <QWidget>

#include "domain/FlightSample.h"
#include "domain/FlightSession.h"

class FlightDataModel;
class FlightReplayController;
class QLabel;
class QLineSeries;
class QSlider;
class QPushButton;
class QDoubleSpinBox;
class QValueAxis;

class DashboardPage : public QWidget {
    Q_OBJECT

public:
    explicit DashboardPage(FlightDataModel *model, FlightReplayController *replay, QWidget *parent = nullptr);

    void setReplaySession(const FlightSession *session);
    void setReplayTrailLength(int trailLength);

private slots:
    void onSampleUpdated(const FlightSample &sample);
    void onSessionReset();

private:
    void rebuildReplayCharts(int trailLength);
    void appendLiveChartPoint(const FlightSample &sample);
    static double elapsedSeconds(long t0Ms, long tMs);

    FlightDataModel *m_model = nullptr;
    FlightReplayController *m_replay = nullptr;
    const FlightSession *m_session = nullptr;

    QLabel *m_velValue = nullptr;
    QLabel *m_altValue = nullptr;
    QLabel *m_telemValue = nullptr;
    QLabel *m_logLabel = nullptr;

    QLineSeries *m_altSeries = nullptr;
    QLineSeries *m_tempSeries = nullptr;
    QLineSeries *m_pressSeries = nullptr;
    QValueAxis *m_axisTime = nullptr;
    QValueAxis *m_axisTime2 = nullptr;
    QValueAxis *m_axisTime3 = nullptr;
    QValueAxis *m_axisAlt = nullptr;
    QValueAxis *m_axisTemp = nullptr;
    QValueAxis *m_axisPress = nullptr;

    long m_t0Ms = 0;
    bool m_haveT0 = false;

    QPushButton *m_playBtn = nullptr;
    QPushButton *m_pauseBtn = nullptr;
    QPushButton *m_stopBtn = nullptr;
    QSlider *m_replaySlider = nullptr;
    QLabel *m_replayStatus = nullptr;
    QDoubleSpinBox *m_speedSpin = nullptr;
};

#endif // COSMO_SOFT_DASHBOARDPAGE_H
