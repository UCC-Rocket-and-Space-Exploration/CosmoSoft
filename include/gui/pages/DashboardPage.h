#ifndef COSMO_SOFT_DASHBOARDPAGE_H
#define COSMO_SOFT_DASHBOARDPAGE_H

#include <QElapsedTimer>
#include <QPointer>
#include <QVector>
#include <QWidget>

#include <array>
#include <deque>
#include <memory>
#include <vector>

#include "domain/FlightSample.h"
#include "domain/FlightSession.h"
#include "services/preview/FlightPreviewCache.h"

class QChart;
class QChartView;
class QFrame;
class QLabel;
class QLineSeries;
class QPaintEvent;
class QPushButton;
class QResizeEvent;
class QShowEvent;
class QScrollArea;
class QStackedWidget;
class QShortcut;
class QTimer;
class QToolButton;
class QValueAxis;
class QVBoxLayout;

class FlightDataModel;
class FlightReplayController;
class Map3DWidget;
class ReplayBar;
class TelemetryChartView;
class TracesPanel;

/**
 * @class DashboardPage
 * @brief Main flight-data dashboard: traces panel, telemetry chart, and replay bar.
 *
 * Supports two operating modes controlled by FlightDataModel::replayMode():
 *
 *  **Live mode** — FlightDataModel emits liveSamplesReceived(); samples are buffered
 *  in m_liveSamples (capped at kMaxLiveBufferSamples) and the chart is rebuilt
 *  at most 20 times per second by the shared render scheduler.
 *
 *  **Replay mode** — A FlightSession is loaded via setReplaySession(); ReplayBar
 *  forwards each confirmed controller position to the dashboard. Chart work is
 *  capped at ≈ 30 fps and deferred while its view is hidden.
 *
 * Each enabled metric has an aligned plot in its own measurement units.
 * Dense overviews retain original bucket minima/maxima; zooming restores every
 * sample. Hover inspection always resolves to an original sample.
 */
class DashboardPage : public QWidget {
    Q_OBJECT

public:
    /** Total number of supported telemetry metrics (matches the metric lookup tables). */
    static constexpr int kMetricCount = 9;

    explicit DashboardPage(FlightDataModel *model, FlightReplayController *replay, QWidget *parent = nullptr);

    /**
     * @brief Selects metric or imperial presentation units for dashboard views.
     *
     * Stored flight samples and preview selection remain in their raw SI form.
     */
    void setImperialUnits(bool imperial);

    /**
     * Switches to replay mode and loads @p session.
     * Resets the scrubber, rebuilds the chart from the full session, and
     * positions the replay controller at the end (most recent sample).
     * Pass nullptr to return to an idle state.
     */
    void setReplaySession(std::shared_ptr<const FlightSession> session,
                          std::shared_ptr<const cosmo::preview::FlightPreviewCache> preview);

    /**
     * Updates the visible replay trail to @p trailLength samples.
     * Also synchronizes the ReplayBar when an external caller changes the trail.
     */
    void setReplayTrailLength(int trailLength);

    /** Target display budget per series; missing-value boundaries are retained in addition. */
    static constexpr int kMaxChartDisplayPoints = 6000;

    /** Maximum live samples kept in memory; older samples are discarded. */
    static constexpr int kMaxLiveBufferSamples = 25000;

  signals:
    /** @brief Request the main window flight-log picker. */
    void openLogRequested();
    /** @brief Request the live telemetry connection page. */
    void liveTelemetryRequested();

  protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private slots:
    void onDisplayedSampleChanged(const FlightSample &sample);
    void onLiveSamplesReceived(const QVector<FlightSample> &samples);
    void onSessionReset();
    void onResetChartZoom();
    void onChartVisualOptionsToggled();

private:
    // ── Replay chart management ───────────────────────────────────────────────
    void applyReplayControllerPosition(int trailLength);
    void scheduleReplayChartRebuild();   ///< Mark replay chart data dirty.
    void flushReplayChartRebuild();      ///< Flush the latest visible replay state.
    void rebuildReplayCharts(int trailLength);

    // ── Live chart management ─────────────────────────────────────────────────
    void scheduleLiveChartRebuild();     ///< Mark live chart data dirty.
    void rebuildLiveSeriesFromHistory();

    // ── Shared chart render scheduler ─────────────────────────────────────
    void scheduleRenderPass();
    void processScheduledUpdates(bool forceImmediate = false);
    [[nodiscard]] bool graphViewIsActive() const;

    // ── Chart helpers ─────────────────────────────────────────────────────────

    /** Common chart-building pipeline shared by replay and live chart rebuilds. */
    void buildChartFromSamples(const std::vector<FlightSample> &samples, int end);
    void buildChartFromSampleIndices(const std::vector<FlightSample> &samples, const std::vector<int> &sampleIndices,
                                     int logicalSampleCount, int begin, int end);
    [[nodiscard]] int chartPointBudget() const;

    /** Builds the chart toolbar and populates all toolbar member pointers. */
    void buildChartToolbar(QWidget *chartHeader, QVBoxLayout *chartHeaderLay);

    /** Zooms both axes in or out by one step, centered on the current view. */
    void zoomChartAxesAtCenter(bool zoomIn);

    /** Dispatches to rebuildReplayCharts or rebuildLiveSeriesFromHistory depending on mode. */
    void refreshAllSeriesFromData();
    void applyChartTheme();
    void configureTracePlots();
    void enterComparisonView();
    void exitComparisonView();
    void rebuildComparisonChart();
    void updateComparisonControls();
    void updateCompactToolbar();
    [[nodiscard]] const FlightSample *hoverSample(int index1Based) const;
    void refreshPageStyleSheet();
    [[nodiscard]] static QString buildDashboardQss();

    /**
     * Applies per-series point-marker and label visibility based on point count
     * in the visible range.
     */
    void applySeriesPointDisplay(QLineSeries *series, int pointCount) const;
    [[nodiscard]] int countEnabledMetrics() const;
    [[nodiscard]] QString formatMultiMetricHover(double tSec, int displayPointIndex1Based) const;

    // ── Data sources ──────────────────────────────────────────────────────────
    FlightDataModel              *m_model   = nullptr;
    QPointer<FlightReplayController> m_replay;
    std::shared_ptr<const FlightSession> m_session;  ///< Shared loaded replay session.
    std::shared_ptr<const cosmo::preview::FlightPreviewCache> m_preview;

    // ── Widgets ───────────────────────────────────────────────────────────────
    TracesPanel *m_tracesPanel       = nullptr;   ///< Metric toggle + live readout panel.
    ReplayBar   *m_replayBar       = nullptr;   ///< Transport bar (buttons, scrubber, labels).
    QLabel      *m_emptyStateLabel   = nullptr;  ///< Guidance shown when no data is loaded.
    QLabel      *m_sessionInfoLabel  = nullptr;  ///< Session metadata summary.

    // ── Chart ─────────────────────────────────────────────────────────────────
    QChart             *m_chart     = nullptr;
    TelemetryChartView *m_chartView = nullptr;
    std::array<QLineSeries *, kMetricCount> m_lineSeries{};
    QValueAxis *m_axisX  = nullptr;
    QValueAxis *m_axisY  = nullptr;
    std::array<QChart *, kMetricCount> m_traceCharts{};
    std::array<TelemetryChartView *, kMetricCount> m_traceViews{};
    std::array<QValueAxis *, kMetricCount> m_traceAxesX{};
    std::array<QValueAxis *, kMetricCount> m_traceAxesY{};
    QScrollArea *m_chartScroll = nullptr;
    QWidget *m_chartContent = nullptr;
    QStackedWidget *m_graphStack = nullptr;
    QChart *m_comparisonChart = nullptr;
    TelemetryChartView *m_comparisonView = nullptr;
    QValueAxis *m_comparisonAxisX = nullptr;
    QValueAxis *m_comparisonAxisLeft = nullptr;
    QValueAxis *m_comparisonAxisRight = nullptr;
    bool m_syncingChartAxes = false;
    bool m_syncingComparisonAxes = false;
    bool m_comparisonActive = false;
    QVector<QShortcut *> m_chartShortcuts; ///< Shortcuts active only inside the graph view.

    // ── View switcher (Graph / Map) ───────────────────────────────────────────
    QStackedWidget *m_viewStack    = nullptr;
    Map3DWidget    *m_mapWidget    = nullptr;
    QPushButton    *m_graphViewBtn = nullptr;
    QPushButton    *m_mapViewBtn   = nullptr;
    QWidget *m_viewSwitchGroup = nullptr;

    // ── Chart toolbar buttons ─────────────────────────────────────────────────
    QPushButton  *m_zoomOutBtn            = nullptr;
    QPushButton  *m_zoomInBtn             = nullptr;
    QPushButton  *m_zoomResetBtn          = nullptr;
    QToolButton  *m_showMarkersToggle     = nullptr;
    QToolButton  *m_showPointValuesToggle = nullptr;
    QToolButton  *m_followToggle          = nullptr;
    QToolButton  *m_tracesToggleBtn       = nullptr;
    QPushButton  *m_compareSelectedBtn    = nullptr;
    QPushButton  *m_exitComparisonBtn     = nullptr;

    // ── Graph-only toolbar groups (hidden in Map view) ────────────────────────
    QWidget *m_zoomGroup    = nullptr;
    QWidget *m_toggleGroup  = nullptr;
    QWidget *m_actionGroup  = nullptr;
    QToolButton *m_chartHelpBtn = nullptr;
    QLabel *m_chartHelpHint = nullptr;

    /** Shows/hides toolbar buttons appropriate to the current view (Graph vs Map). */
    void updateToolbarForView();

    // ── Event markers ─────────────────────────────────────────────────────────
    struct EventMarker {
        double timeSec;
        QString name;
    };
    std::vector<EventMarker> m_eventMarkers;
    std::vector<QLineSeries *> m_markerSeries;
    void addEventMarker(double timeSec, const QString &name);
    void clearEventMarkers();
    void applyEventMarkerTheme(QLineSeries *series) const;
    void refreshEventMarkerTheme();
    void updateEventMarkerGeometry();

    // ── Bounded chart scheduling ─────────────────────────────────────────────
    static constexpr int kLiveChartIntervalMs = 50;
    static constexpr int kInteractiveIntervalMs = 33;

    QTimer *m_renderSchedulerTimer = nullptr;  ///< The only chart render timer.
    QElapsedTimer m_renderClock;
    qint64 m_lastLiveChartRenderMs = -kLiveChartIntervalMs;
    qint64 m_lastReplayChartRenderMs = -kInteractiveIntervalMs;
    bool m_liveChartDirty = false;
    bool m_replayChartDirty = false;

    /**
     * When true, rebuildReplayCharts / rebuildLiveSeriesFromHistory skip the
     * automatic axis rescaling.  Set after any user pan/zoom; cleared on
     * session reset or metric toggle.
     */
    bool m_preserveChartAxes = false;

    /**
     * Maps each visible-time lookup entry back to the original logical
     * sample index in m_liveSamples / m_session->samples.
     */
    std::vector<int> m_hoverSampleIndexMap;
    QVector<double> m_chartHoverXValues;
    int m_hoverLogicalSampleCount    = 0;   ///< Total logical samples at last rebuild.
    int m_replayChartBuiltTrailLength = -1; ///< Trail length at last full replay rebuild.
    int m_replayChartBuiltBucket = -1;      ///< Preview bucket at last replay rebuild.

    int m_lastReplayTrailLength = 0;  ///< Most recent trail length from the controller.
    bool m_imperialUnits = false;     ///< True when telemetry is presented in imperial units.
    bool m_splitterSavePending = false; ///< Coalesces QSettings writes from splitterMoved.

    /** Circular live-telemetry buffer, bounded to kMaxLiveBufferSamples. */
    std::deque<FlightSample> m_liveSamples;
    std::vector<FlightSample> m_liveChartScratch;

    /**
     * Mirrors TracesPanel::enabledMetrics() — kept in sync via enabledMetricsChanged
     * signal so chart-building code can read it without calling the panel each time.
     */
    std::array<bool, kMetricCount> m_metricEnabled{};
};

#endif // COSMO_SOFT_DASHBOARDPAGE_H
