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
class QComboBox;
class QPushButton;
class QSlider;
class QTimer;
class QToolButton;
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

    static constexpr int kMaxChartDisplayPoints = 6000;
    static constexpr int kMaxLiveBufferSamples = 25000;

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void onSampleUpdated(const FlightSample &sample);
    void onSessionReset();
    void onAnyMetricToggled();
    void onResetChartZoom();
    void onChartVisualOptionsToggled();

private:
    void applyReplayControllerPosition(int trailLength);
    void scheduleReplayChartRebuild();
    void flushReplayChartRebuild();
    void rebuildReplayCharts(int trailLength);
    void scheduleLiveChartRebuild();
    void rebuildLiveSeriesFromHistory();
    void zoomChartAxesAtCenter(bool zoomIn);
    void refreshAllSeriesFromData();
    void applyChartTheme();
    void updateHoverReadoutDefault();
    void updateChartStatsLabel();
    void updateReplayPanel();
    void syncReplayTransportChrome();
    void syncCheckboxStatesFromFlags();
    void ensureAtLeastOneMetricEnabled();
    void applySeriesPointDisplay(QLineSeries *series, int pointCount, int nEnabledMetrics) const;

    [[nodiscard]] int countEnabledMetrics() const;
    [[nodiscard]] QString formatMultiMetricHover(double tSec, int displayPointIndex1Based) const;

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
    QPushButton *m_zoomOutBtn = nullptr;
    QPushButton *m_zoomInBtn = nullptr;
    QPushButton *m_zoomResetBtn = nullptr;
    QToolButton *m_showMarkersToggle = nullptr;
    QToolButton *m_showPointValuesToggle = nullptr;
    QLabel *m_chartInteractionHint = nullptr;
    QLabel *m_hoverReadoutLabel = nullptr;
    QLabel *m_chartStatsLabel = nullptr;

    QTimer *m_liveChartCoalesceTimer = nullptr;
    QTimer *m_replayChartCoalesceTimer = nullptr;
    bool m_preserveChartAxes = false;

    std::vector<int> m_hoverSampleIndexMap;
    int m_hoverLogicalSampleCount = 0;
    int m_replayChartBuiltTrailLength = -1;

    int m_lastReplayTrailLength = 0;

    std::vector<FlightSample> m_liveSamples;

    QToolButton *m_playPauseBtn = nullptr;
    QToolButton *m_stopBtn = nullptr;
    QToolButton *m_jumpStartBtn = nullptr;
    QToolButton *m_jumpEndBtn = nullptr;
    QSlider *m_replaySlider = nullptr;
    QLabel *m_replayBarTitle = nullptr;
    QLabel *m_replaySampleCaption = nullptr;
    QLabel *m_replayTimeLeftLabel = nullptr;
    QLabel *m_replayTimeRightLabel = nullptr;
    QLabel *m_replayInfoLabel = nullptr;
    QComboBox *m_speedCombo = nullptr;

    QString m_replayActivityText;
};

#endif // COSMO_SOFT_DASHBOARDPAGE_H
