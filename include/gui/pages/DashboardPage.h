#ifndef COSMO_SOFT_DASHBOARDPAGE_H
#define COSMO_SOFT_DASHBOARDPAGE_H

#include <QWidget>

#include <vector>

#include "domain/FlightSample.h"
#include "domain/FlightSession.h"

class QChart;
class QChartView;
class QComboBox;
class QLabel;
class QLineSeries;
class QPaintEvent;
class QPushButton;
class QSlider;
class QDoubleSpinBox;
class QValueAxis;

class FlightDataModel;
class FlightReplayController;

class DashboardPage : public QWidget {
    Q_OBJECT

public:
    explicit DashboardPage(FlightDataModel *model, FlightReplayController *replay, QWidget *parent = nullptr);
    void setReplaySession(const FlightSession *session);
    void setReplayTrailLength(int trailLength);

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void onSampleUpdated(const FlightSample &sample);
    void onSessionReset();
    void onMetricComboChanged(int index);
    void onPrevMetric();
    void onNextMetric();
    void onResetChartZoom();

private:
    void rebuildReplayCharts(int trailLength);
    void appendLiveChartPoint(const FlightSample &sample);
    void rebuildLiveSeriesFromHistory();
    void applyMetricToChartUi();
    void updateLiveAxisRanges();
    void updateHoverReadoutDefault();
    void updateChartStatsLabel();
    void updateReplayPanel();
    [[nodiscard]] QString formatMetricHover(double tSec, double value, int sampleIndex, int totalSamples) const;

    static double elapsedSeconds(long t0Ms, long tMs);

    FlightDataModel *m_model = nullptr;
    FlightReplayController *m_replay = nullptr;
    const FlightSession *m_session = nullptr;

    QLabel *m_velValue = nullptr;
    QLabel *m_altValue = nullptr;
    QLabel *m_tempValue = nullptr;
    QLabel *m_pressValue = nullptr;

    QChart *m_chart = nullptr;
    QChartView *m_chartView = nullptr;
    QLineSeries *m_series = nullptr;
    QValueAxis *m_axisX = nullptr;
    QValueAxis *m_axisY = nullptr;

    QComboBox *m_metricCombo = nullptr;
    QPushButton *m_metricPrevBtn = nullptr;
    QPushButton *m_metricNextBtn = nullptr;
    QPushButton *m_zoomResetBtn = nullptr;
    QLabel *m_hoverReadoutLabel = nullptr;
    QLabel *m_chartStatsLabel = nullptr;

    int m_metricIndex = 0;
    int m_lastReplayTrailLength = 0;

    long m_t0Ms = 0;
    bool m_haveT0 = false;
    std::vector<FlightSample> m_liveSamples;

    QPushButton *m_playBtn = nullptr;
    QPushButton *m_pauseBtn = nullptr;
    QPushButton *m_stopBtn = nullptr;
    QPushButton *m_jumpStartBtn = nullptr;
    QPushButton *m_jumpEndBtn = nullptr;
    QSlider *m_replaySlider = nullptr;
    QLabel *m_replayInfoLabel = nullptr;
    QDoubleSpinBox *m_speedSpin = nullptr;

    QString m_replayActivityText;
};

#endif // COSMO_SOFT_DASHBOARDPAGE_H
