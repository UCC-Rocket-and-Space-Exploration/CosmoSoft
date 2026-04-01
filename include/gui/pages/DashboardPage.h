#ifndef COSMO_SOFT_DASHBOARDPAGE_H
#define COSMO_SOFT_DASHBOARDPAGE_H

#include <QWidget>

#include <array>
#include <vector>

#include "domain/FlightSample.h"
#include "domain/FlightSession.h"

class QChart;
class QChartView;
class QCheckBox;
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
    static constexpr int kMetricCount = 9;

    explicit DashboardPage(FlightDataModel *model, FlightReplayController *replay, QWidget *parent = nullptr);
    void setReplaySession(const FlightSession *session);
    void setReplayTrailLength(int trailLength);

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void onSampleUpdated(const FlightSample &sample);
    void onSessionReset();
    void onAnyMetricToggled();
    void onResetChartZoom();
    void onTracePresetAltTempPress();
    void onChartVisualOptionsToggled();

private:
    void rebuildReplayCharts(int trailLength);
    void appendLiveChartPoint(const FlightSample &sample);
    void rebuildLiveSeriesFromHistory();
    void refreshAllSeriesFromData();
    void applyChartTheme();
    void updateHoverReadoutDefault();
    void updateChartStatsLabel();
    void updateReplayPanel();
    void syncCheckboxStatesFromFlags();
    void ensureAtLeastOneMetricEnabled();
    void applySeriesPointDisplay(QLineSeries *series, int pointCount, int nEnabledMetrics) const;

    [[nodiscard]] int countEnabledMetrics() const;
    [[nodiscard]] QString formatMultiMetricHover(double tSec, int sampleIndex, int totalSamples) const;

    FlightDataModel *m_model = nullptr;
    FlightReplayController *m_replay = nullptr;
    const FlightSession *m_session = nullptr;

    QLabel *m_velValue = nullptr;
    QLabel *m_altValue = nullptr;
    QLabel *m_tempValue = nullptr;
    QLabel *m_pressValue = nullptr;

    QChart *m_chart = nullptr;
    QChartView *m_chartView = nullptr;
    std::array<QLineSeries *, kMetricCount> m_lineSeries{};
    QValueAxis *m_axisX = nullptr;
    QValueAxis *m_axisY = nullptr;

    std::array<QCheckBox *, kMetricCount> m_metricChecks{};
    std::array<bool, kMetricCount> m_metricEnabled{};
    QPushButton *m_tracePresetBtn = nullptr;
    QPushButton *m_zoomResetBtn = nullptr;
    QCheckBox *m_showMarkersCheck = nullptr;
    QCheckBox *m_showPointValuesCheck = nullptr;
    QLabel *m_hoverReadoutLabel = nullptr;
    QLabel *m_chartStatsLabel = nullptr;

    int m_lastReplayTrailLength = 0;

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
