#ifndef COSMO_SOFT_DASHBOARDPAGE_H
#define COSMO_SOFT_DASHBOARDPAGE_H

#include <QWidget>

#include <array>
#include <vector>

#include "domain/FlightSample.h"
#include "domain/FlightSession.h"

class QChart;
class QChartView;
class QFrame;
class QLabel;
class QLineSeries;
class QPaintEvent;
class QPushButton;
class QStackedWidget;
class QProgressBar;
class QTimer;
class QToolButton;
class QValueAxis;
class QVBoxLayout;

class FlightDataModel;
class FlightReplayController;
class Map3DWidget;
class ReplayBar;
class TracesPanel;

/**
 * @class DashboardPage
 * @brief Main flight-data dashboard: traces panel, telemetry chart, and replay bar.
 *
 * Supports two operating modes controlled by FlightDataModel::replayMode():
 *
 *  **Live mode** — FlightDataModel emits sampleUpdated(); samples are buffered
 *  in m_liveSamples (capped at kMaxLiveBufferSamples) and the chart is rebuilt
 *  at most once per 50 ms via the live coalesce timer.
 *
 *  **Replay mode** — A FlightSession is loaded via setReplaySession() and the
 *  visible trail is controlled by setReplayTrailLength() which is called by
 *  MainWindow on every FlightReplayController tick.  Chart redraws are coalesced
 *  to ≈ 30 fps so timeline scrubbing stays smooth.
 *
 * When the sample count exceeds kMaxChartDisplayPoints the chart uses uniform
 * decimation; the mapping from display-point index back to original sample is
 * stored in m_hoverSampleIndexMap to keep hover readouts accurate.
 *
 * When two or more metrics are enabled the Y axis is normalised to [0, 1] so
 * all traces overlay on the same scale (the axis title changes to "Normalized").
 */
class DashboardPage : public QWidget {
    Q_OBJECT

public:
    /** Total number of supported telemetry metrics (matches the metric lookup tables). */
    static constexpr int kMetricCount = 9;

    explicit DashboardPage(FlightDataModel *model, FlightReplayController *replay, QWidget *parent = nullptr);

    /**
     * Switches to replay mode and loads @p session.
     * Resets the scrubber, rebuilds the chart from the full session, and
     * positions the replay controller at the end (most recent sample).
     * Pass nullptr to return to an idle state.
     */
    void setReplaySession(const FlightSession *session);

    /**
     * Updates the visible replay trail to @p trailLength samples.
     * Called by MainWindow on every FlightReplayController::positionChanged signal.
     */
    void setReplayTrailLength(int trailLength);

    /** Maximum display points sent to Qt Charts per series (decimation threshold). */
    static constexpr int kMaxChartDisplayPoints = 6000;

    /** Maximum live samples kept in memory; older samples are discarded. */
    static constexpr int kMaxLiveBufferSamples = 25000;

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void onSampleUpdated(const FlightSample &sample);
    void onSessionReset();
    void onResetChartZoom();
    void onChartVisualOptionsToggled();

private:
    // ── Replay chart management ───────────────────────────────────────────────
    void applyReplayControllerPosition(int trailLength);
    void scheduleReplayChartRebuild();   ///< Restart the 33 ms coalesce timer.
    void flushReplayChartRebuild();      ///< Cancel timer and rebuild immediately.
    void rebuildReplayCharts(int trailLength);

    // ── Live chart management ─────────────────────────────────────────────────
    void scheduleLiveChartRebuild();     ///< Restart the 50 ms coalesce timer.
    void rebuildLiveSeriesFromHistory();

    // ── Chart helpers ─────────────────────────────────────────────────────────

    /** Common chart-building pipeline shared by replay and live chart rebuilds. */
    void buildChartFromSamples(const std::vector<FlightSample> &samples, int end);

    /** Builds the chart toolbar and populates all toolbar member pointers. */
    void buildChartToolbar(QWidget *chartHeader, QVBoxLayout *chartHeaderLay);

    /** Zooms both axes in or out by one step, centered on the current view. */
    void zoomChartAxesAtCenter(bool zoomIn);

    /** Dispatches to rebuildReplayCharts or rebuildLiveSeriesFromHistory depending on mode. */
    void refreshAllSeriesFromData();
    void applyChartTheme();
    void refreshPageStyleSheet();
    void updateChartStatsLabel();
    [[nodiscard]] static QString buildDashboardQss();

    /**
     * Applies per-series point-marker and label visibility based on point count
     * and the number of currently enabled metrics.
     */
    void applySeriesPointDisplay(QLineSeries *series, int pointCount, int nEnabledMetrics) const;
    [[nodiscard]] int countEnabledMetrics() const;
    [[nodiscard]] QString formatMultiMetricHover(double tSec, int displayPointIndex1Based) const;

    // ── Data sources ──────────────────────────────────────────────────────────
    FlightDataModel        *m_model   = nullptr;
    FlightReplayController *m_replay  = nullptr;
    const FlightSession    *m_session = nullptr;  ///< Non-owning; set by setReplaySession().

    // ── Widgets ───────────────────────────────────────────────────────────────
    TracesPanel *m_tracesPanel       = nullptr;   ///< Metric toggle + live readout panel.
    ReplayBar   *m_replayBar       = nullptr;   ///< Transport bar (buttons, scrubber, labels).
    QLabel      *m_emptyStateLabel   = nullptr;  ///< Guidance shown when no data is loaded.
    QLabel      *m_sessionInfoLabel  = nullptr;  ///< Session metadata summary.

    // ── Chart ─────────────────────────────────────────────────────────────────
    QChart     *m_chart     = nullptr;
    QChartView *m_chartView = nullptr;
    std::array<QLineSeries *, kMetricCount> m_lineSeries{};
    QValueAxis *m_axisX  = nullptr;
    QValueAxis *m_axisY  = nullptr;
    QValueAxis *m_axisY2 = nullptr;  ///< Secondary right-hand Y axis for dual-metric mode.

    // ── View switcher (Graph / Map) ───────────────────────────────────────────
    QStackedWidget *m_viewStack    = nullptr;
    Map3DWidget    *m_mapWidget    = nullptr;
    QPushButton    *m_graphViewBtn = nullptr;
    QPushButton    *m_mapViewBtn   = nullptr;

    // ── Chart toolbar buttons ─────────────────────────────────────────────────
    QPushButton  *m_zoomOutBtn            = nullptr;
    QPushButton  *m_zoomInBtn             = nullptr;
    QPushButton  *m_zoomResetBtn          = nullptr;
    QToolButton  *m_showMarkersToggle     = nullptr;
    QToolButton  *m_showPointValuesToggle = nullptr;
    QToolButton  *m_followToggle          = nullptr;
    QToolButton  *m_tracesToggleBtn       = nullptr;
    QLabel       *m_chartStatsLabel       = nullptr;

    // ── Graph-only toolbar groups (hidden in Map view) ────────────────────────
    QWidget *m_zoomGroup    = nullptr;
    QWidget *m_toggleGroup  = nullptr;
    QWidget *m_actionGroup  = nullptr;
    QWidget *m_chartHelpBtn = nullptr;

    /** Shows/hides toolbar buttons appropriate to the current view (Graph vs Map). */
    void updateToolbarForView();

    // ── Chart loading indicator ─────────────────────────────────────────────
    QProgressBar *m_chartLoadingBar = nullptr;
    void showChartLoadingIndicator();
    void hideChartLoadingIndicator();

    // ── Event markers ─────────────────────────────────────────────────────────
    struct EventMarker {
        double timeSec;
        QString name;
    };
    std::vector<EventMarker> m_eventMarkers;
    std::vector<QLineSeries *> m_markerSeries;
    void addEventMarker(double timeSec, const QString &name);
    void clearEventMarkers();
    void redrawEventMarkers();

    // ── Chart update coalescing ───────────────────────────────────────────────
    QTimer *m_liveChartCoalesceTimer   = nullptr;  ///< 50 ms, single-shot.
    QTimer *m_replayChartCoalesceTimer = nullptr;  ///< 33 ms, single-shot.

    /**
     * When true, rebuildReplayCharts / rebuildLiveSeriesFromHistory skip the
     * automatic axis rescaling.  Set after any user pan/zoom; cleared on
     * session reset or metric toggle.
     */
    bool m_preserveChartAxes = false;

    /**
     * Maps each decimated display-point index back to the original logical
     * sample index in m_liveSamples / m_session->samples.
     */
    std::vector<int> m_hoverSampleIndexMap;
    int m_hoverLogicalSampleCount    = 0;   ///< Total logical samples at last rebuild.
    int m_replayChartBuiltTrailLength = -1; ///< Trail length at last full replay rebuild.

    int m_lastReplayTrailLength = 0;  ///< Most recent trail length from the controller.

    /** Circular live-telemetry buffer, bounded to kMaxLiveBufferSamples. */
    std::vector<FlightSample> m_liveSamples;

    /**
     * Mirrors TracesPanel::enabledMetrics() — kept in sync via enabledMetricsChanged
     * signal so chart-building code can read it without calling the panel each time.
     */
    std::array<bool, kMetricCount> m_metricEnabled{};
};

#endif // COSMO_SOFT_DASHBOARDPAGE_H
