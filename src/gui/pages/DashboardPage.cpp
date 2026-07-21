/**
 * @file DashboardPage.cpp
 *
 * Main flight-data dashboard widget. Owns three visual regions:
 *
 *  ┌──────────────┬──────────────────────────────────────────┐
 *  │  Traces panel│  Telemetry chart (QChart / QChartView)   │
 *  │  (left)      │  + hover overlay + crosshair             │
 *  ├──────────────┴──────────────────────────────────────────┤
 *  │  Replay / live-buffer transport bar (bottom)            │
 *  └─────────────────────────────────────────────────────────┘
 *
 * The traces panel and chart are separated by a QSplitter so the user can resize
 * them; the splitter state is persisted in QSettings.
 *
 * Two data sources are supported:
 *  - Live mode  — samples arrive in batches from the serial decoder worker.
 *    They are stored in m_liveSamples (bounded to kMaxLiveBufferSamples) and
 *    chart redraws are capped at 20 Hz by the shared render scheduler.
 *  - Replay mode — a FlightSession is set via setReplaySession(); ReplayBar
 *    forwards each confirmed controller position to update the visible trail.
 *    Chart updates are capped at 30 Hz so scrolling the scrubber stays smooth.
 *
 * When more than kMaxChartDisplayPoints samples are present the chart uses
 * LTTB reduction (sampleIndicesForChartDisplay) to keep rendering fast.
 * m_hoverSampleIndexMap maps each decimated display-point index back to its
 * original logical sample index so hover readouts always show accurate values.
 */

#include "gui/pages/DashboardPage.h"

#include "domain/FlightSession.h"
#include "gui/FlightDataModel.h"
#include "gui/FlightReplayController.h"
#include "gui/Theme.h"
#include "gui/ThemeManager.h"
#include "gui/widgets/MetricDefs.h"
#include "gui/widgets/ReplayBar.h"
#include "gui/widgets/Map3DWidget.h"
#include "gui/widgets/TelemetryChartView.h"
#include "gui/widgets/TracesPanel.h"

#include <QApplication>
#include <QChart>
#include <QClipboard>
#include <QColor>
#include <QEvent>
#include <QFocusEvent>
#include <QKeySequence>
#include <QShortcut>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QPointF>
#include <QPushButton>
#include <QProgressBar>
#include <QScrollArea>
#include <QSettings>
#include <QShowEvent>
#include <QStackedWidget>

#include "gui/SettingsKeys.h"
#include <QSignalBlocker>
#include <QSplitter>
#include <QSizePolicy>
#include <QSize>
#include <QSlider>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QWheelEvent>
#include <QVBoxLayout>
#include <QWidget>
#include <QtCharts/QAbstractSeries>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>
#include <utility>

using namespace Qt::StringLiterals;
using namespace MetricDefs;

namespace {

constexpr int kMetricCount = DashboardPage::kMetricCount;

static_assert(
    FlightSession::kTraceMetricCount == static_cast<std::size_t>(kMetricCount),
    "FlightSession::metricsInSource size must match dashboard trace count");

/** All traces offered when no replay file, or per-column flags after CSV load. */
[[nodiscard]] std::array<bool, kMetricCount> traceOfferMaskForSession(const FlightSession *session)
{
    std::array<bool, kMetricCount> out;
    out.fill(true);
    if (!session || session->samples.empty()) {
        return out;
    }
    for (std::size_t i = 0; i < FlightSession::kTraceMetricCount; ++i) {
        out[i] = session->metricsInSource[i];
    }
    return out;
}

/**
 * LTTB (Largest Triangle Three Buckets) downsampling.
 *
 * Returns up to @p maxPts indices from [0, end) preserving visual peaks and
 * valleys that uniform stride-based decimation would miss.  For each bucket
 * the point with the largest triangle area (across all enabled metrics after
 * per-metric normalization) is selected, ensuring features in any single
 * metric are retained even when multiple traces are active.
 *
 * Falls back to uniform stride when no metrics are enabled.
 */
[[nodiscard]] std::vector<int> lttbIndicesForChartDisplay(
    const std::vector<FlightSample> &samples,
    int end, int maxPts,
    const std::array<bool, kMetricCount> &enabled,
    long tRef, bool sessionElapsed)
{
    std::vector<int> idx;
    if (end <= 0 || maxPts <= 0) return idx;
    if (end == 1) { idx.push_back(0); return idx; }
    if (end <= maxPts) {
        idx.resize(static_cast<std::size_t>(end));
        std::iota(idx.begin(), idx.end(), 0);
        return idx;
    }
    if (maxPts <= 2) {
        idx.push_back(0);
        if (end > 1) idx.push_back(end - 1);
        return idx;
    }

    std::vector<int> emi;
    for (int mi = 0; mi < kMetricCount; ++mi)
        if (enabled[static_cast<std::size_t>(mi)]) emi.push_back(mi);

    if (emi.empty()) {
        idx.reserve(static_cast<std::size_t>(maxPts));
        const long long denom = maxPts - 1;
        for (int k = 0; k < maxPts; ++k)
            idx.push_back(static_cast<int>((static_cast<long long>(k) * (end - 1)) / denom));
        return idx;
    }

    const auto nEmi = emi.size();
    std::vector<double> mMin(nEmi, std::numeric_limits<double>::infinity());
    std::vector<double> mMax(nEmi, -std::numeric_limits<double>::infinity());
    for (int i = 0; i < end; ++i) {
        const FlightSample &s = samples[static_cast<std::size_t>(i)];
        for (std::size_t m = 0; m < nEmi; ++m) {
            const double v = sampleValueForMetric(s, emi[m]);
            if (!std::isfinite(v)) {
                continue;
            }
            mMin[m] = std::min(mMin[m], v);
            mMax[m] = std::max(mMax[m], v);
        }
    }
    std::vector<double> mScale(nEmi);
    std::vector<bool> metricUsable(nEmi, false);
    for (std::size_t m = 0; m < nEmi; ++m) {
        const double range = mMax[m] - mMin[m];
        metricUsable[m] = std::isfinite(mMin[m]) && std::isfinite(mMax[m]);
        if (!metricUsable[m]) {
            mMin[m] = 0.0;
            mScale[m] = 1.0;
            continue;
        }
        mScale[m] = std::isfinite(range) && range > 1e-15 ? (1.0 / range) : 1.0;
    }

    idx.reserve(static_cast<std::size_t>(maxPts));
    idx.push_back(0);

    const int nBuckets = maxPts - 2;
    const double bucketSize = static_cast<double>(end - 2) / nBuckets;
    int prevSelected = 0;

    for (int b = 0; b < nBuckets; ++b) {
        const int bStart = static_cast<int>(std::floor(1.0 + b * bucketSize));
        const int bEnd   = std::min(static_cast<int>(std::floor(1.0 + (b + 1) * bucketSize)), end - 1);
        if (bStart >= bEnd) {
            idx.push_back(bStart);
            prevSelected = bStart;
            continue;
        }

        const int nbStart = bEnd;
        const int nbEnd   = (b + 1 < nBuckets)
            ? std::min(static_cast<int>(std::floor(1.0 + (b + 2) * bucketSize)), end - 1)
            : end;
        const int nbCount = std::max(nbEnd - nbStart, 1);

        double avgX = 0.0;
        std::vector<double> avgY(nEmi, 0.0);
        std::vector<int> avgCounts(nEmi, 0);
        for (int i = nbStart; i < nbEnd; ++i) {
            const FlightSample &s = samples[static_cast<std::size_t>(i)];
            avgX += chartXSeconds(tRef, s.timestamp, sessionElapsed);
            for (std::size_t m = 0; m < nEmi; ++m) {
                const double value = sampleValueForMetric(s, emi[m]);
                if (!metricUsable[m] || !std::isfinite(value)) {
                    continue;
                }
                avgY[m] += (value - mMin[m]) * mScale[m];
                ++avgCounts[m];
            }
        }
        avgX /= nbCount;
        for (std::size_t m = 0; m < nEmi; ++m) {
            if (avgCounts[m] > 0) {
                avgY[m] /= static_cast<double>(avgCounts[m]);
            }
        }

        const double prevX = chartXSeconds(tRef, samples[static_cast<std::size_t>(prevSelected)].timestamp, sessionElapsed);

        int bestIdx = bStart;
        double bestArea = -1.0;
        for (int i = bStart; i < bEnd; ++i) {
            const FlightSample &s = samples[static_cast<std::size_t>(i)];
            const double curX = chartXSeconds(tRef, s.timestamp, sessionElapsed);
            double maxArea = 0.0;
            bool hasFiniteMetric = false;
            for (std::size_t m = 0; m < nEmi; ++m) {
                const double previousValue = sampleValueForMetric(
                    samples[static_cast<std::size_t>(prevSelected)], emi[m]);
                const double currentValue = sampleValueForMetric(s, emi[m]);
                if (!metricUsable[m] || avgCounts[m] == 0
                    || !std::isfinite(previousValue) || !std::isfinite(currentValue)) {
                    continue;
                }
                hasFiniteMetric = true;
                const double prevY = (previousValue - mMin[m]) * mScale[m];
                const double curY  = (currentValue - mMin[m]) * mScale[m];
                const double area  = std::abs(
                    (prevX - avgX) * (curY - prevY)
                    - (prevX - curX) * (avgY[m] - prevY));
                maxArea = std::max(maxArea, area);
            }
            if (hasFiniteMetric && maxArea > bestArea) {
                bestArea = maxArea;
                bestIdx  = i;
            }
        }
        idx.push_back(bestIdx);
        prevSelected = bestIdx;
    }

    idx.push_back(end - 1);
    return idx;
}

} // namespace

DashboardPage::DashboardPage(FlightDataModel *model, FlightReplayController *replay, QWidget *parent)
    : QWidget(parent),
      m_model(model),
      m_replay(replay) {
    const QSettings unitSettings(kSettingsOrg, kSettingsApp);
    m_imperialUnits = unitSettings.value(kSettingsUnitSystem, kUnitSystemMetric)
                          .toString()
                          .compare(QString::fromLatin1(kUnitSystemImperial),
                                   Qt::CaseInsensitive) == 0;
    setObjectName(u"dashboardPage"_s);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAutoFillBackground(false);
    setStyleSheet(buildDashboardQss());

    // ── Root layout ───────────────────────────────────────────────────────────
    // The page has two rows: the splitter (traces + chart, stretchy) and the
    // replay/transport bar (fixed height, docked at the bottom).
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setSpacing(8);
    rootLayout->setContentsMargins(12, 12, 12, 12);

    // ── Replay transport bar ─────────────────────────────────────────────────
    m_replayBar = new ReplayBar(m_replay, m_model, this);
    if (m_replay) {
        connect(m_replay, &FlightReplayController::playbackPaused, this, [this] {
            // A seek pauses before its confirmed position signal. Queue the
            // flush so the old trail is never rebuilt immediately beforehand.
            QTimer::singleShot(0, this, &DashboardPage::flushReplayChartRebuild);
        });
        connect(m_replay, &FlightReplayController::playbackStopped,  this, &DashboardPage::flushReplayChartRebuild);
        connect(m_replay, &FlightReplayController::playbackFinished, this, &DashboardPage::flushReplayChartRebuild);
    }
    connect(m_replayBar, &ReplayBar::trailLengthChanged,
            this, &DashboardPage::applyReplayControllerPosition);

    m_sessionInfoLabel = new QLabel(this);
    m_sessionInfoLabel->setWordWrap(true);
    m_sessionInfoLabel->setVisible(false);
    m_sessionInfoLabel->setStyleSheet(
        QString(u"color: %1; font-size: 11px; background: %2; "
        u"border: 1px solid %3; border-radius: 8px; padding: 8px 12px;"_s)
            .arg(Theme::kTextMuted())
            .arg(Theme::kBgPanel())
            .arg(Theme::kBorderPanel()));
    rootLayout->addWidget(m_sessionInfoLabel);

    // ── Traces panel (left side of splitter) ─────────────────────────────────
    m_tracesPanel = new TracesPanel(this);
    m_tracesPanel->setImperialUnits(m_imperialUnits);
    m_metricEnabled = m_tracesPanel->enabledMetrics();
    connect(m_tracesPanel, &TracesPanel::enabledMetricsChanged, this,
            [this](const std::array<bool, kMetricCount> &enabled) {
        m_metricEnabled = enabled;
        m_preserveChartAxes = false;
        refreshAllSeriesFromData();
        });

    auto *chartHost = new QWidget(this);
    chartHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto *chartColumn = new QVBoxLayout(chartHost);
    chartColumn->setSpacing(0);
    chartColumn->setContentsMargins(0, 0, 0, 0);

    auto *chartFrame = new QFrame(chartHost);
    chartFrame->setObjectName(u"chartFrame"_s);
    auto *chartFrameLayout = new QVBoxLayout(chartFrame);
    chartFrameLayout->setContentsMargins(0, 0, 0, 0);
    chartFrameLayout->setSpacing(0);

    // ── Chart header — toolbar + stats label ─────────────────────────────────
    // Sits above the chart view with zero stretch.  The hover readout label and
    // interaction hint label are created here but kept hidden; they are replaced
    // by the floating on-chart overlay (TelemetryChartView::m_hoverOverlay).
    auto *chartHeader = new QWidget(chartFrame);
    auto *chartHeaderLay = new QVBoxLayout(chartHeader);
    chartHeaderLay->setContentsMargins(8, 8, 8, 8);
    chartHeaderLay->setSpacing(4);

    buildChartToolbar(chartHeader, chartHeaderLay);

    chartFrameLayout->addWidget(chartHeader, 0);

    m_emptyStateLabel = new QLabel(
        u"No flight data loaded.\n\n"
        u"Open a flight log via File → Open log…\n"
        u"or connect a serial port on the Monitoring page."_s,
        chartFrame);
    m_emptyStateLabel->setAlignment(Qt::AlignCenter);
    m_emptyStateLabel->setWordWrap(true);
    m_emptyStateLabel->setObjectName(u"chartEmptyState"_s);

    m_chart = new QChart();
    m_chart->setBackgroundRoundness(0);
    m_chart->setAnimationOptions(QChart::NoAnimation);

    for (int i = 0; i < kMetricCount; ++i) {
        auto *series = new QLineSeries();
        series->setName(metricDisplayTitle(i, m_imperialUnits));
        const QColor col = metricColor(i);
        series->setColor(col);
        series->setPen(QPen(col, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        m_lineSeries[static_cast<std::size_t>(i)] = series;
        m_chart->addSeries(series);
    }

    m_axisX = new QValueAxis();
    m_axisX->setTitleText(u"Time (s)"_s);
    m_axisX->setRange(0, 10);
    m_axisX->setTickCount(9);
    m_axisX->setLabelFormat(u"%.2f"_s);
    m_chart->addAxis(m_axisX, Qt::AlignBottom);

    m_axisY = new QValueAxis();
    m_axisY->setRange(-1, 1);
    m_axisY->setTickCount(6);
    m_axisY->setLabelFormat(u"%.3g"_s);
    m_chart->addAxis(m_axisY, Qt::AlignLeft);

    m_axisY2 = new QValueAxis();
    m_axisY2->setRange(-1, 1);
    m_axisY2->setTickCount(6);
    m_axisY2->setLabelFormat(u"%.3g"_s);
    m_axisY2->setVisible(false);
    m_chart->addAxis(m_axisY2, Qt::AlignRight);

    for (int i = 0; i < kMetricCount; ++i) {
        m_lineSeries[static_cast<std::size_t>(i)]->attachAxis(m_axisX);
        m_lineSeries[static_cast<std::size_t>(i)]->attachAxis(m_axisY);
    }
    connect(m_axisY, &QValueAxis::rangeChanged, this,
            [this](qreal, qreal) { updateEventMarkerGeometry(); });

    applyChartTheme();

    connect(m_zoomInBtn, &QPushButton::clicked, this, [this]() {
        zoomChartAxesAtCenter(true);
    });
    connect(m_zoomOutBtn, &QPushButton::clicked, this, [this]() {
        zoomChartAxesAtCenter(false);
    });

    // One deadline-aware scheduler owns all expensive chart work.
    // It is a rate limiter (never restarted by every incoming batch), so a
    // continuous stream cannot starve rendering as the former debounce did.
    m_renderClock.start();
    m_renderSchedulerTimer = new QTimer(this);
    m_renderSchedulerTimer->setSingleShot(true);
    m_renderSchedulerTimer->setTimerType(Qt::PreciseTimer);
    connect(m_renderSchedulerTimer, &QTimer::timeout, this, [this]() {
        processScheduledUpdates();
    });

    auto *tcv = new TelemetryChartView(m_chart, chartFrame);
    tcv->setObjectName(u"telemetryChartView"_s);
    tcv->setAccessibleName(u"Telemetry chart"_s);
    tcv->setAccessibleDescription(u"Interactive chart displaying flight telemetry data over time"_s);
    tcv->onUserAdjustedAxes = [this]() {
        m_preserveChartAxes = true;
        if (m_followToggle) m_followToggle->setChecked(false);
        if (m_model && m_model->replayMode() && m_preview) {
            scheduleReplayChartRebuild();
        }
    };
    m_chartView = tcv;
    tcv->setChart(m_chart);
    tcv->hoverDetail = [this](double tSec, int sampleIndex1Based, int /*totalSamples*/) {
        return formatMultiMetricHover(tSec, sampleIndex1Based);
    };
    // hoverReadout text is displayed by TelemetryChartView's own floating overlay;
    // no secondary label is needed in DashboardPage.
    tcv->hoverReadout = nullptr;

    // Keep unmodified chart keys local to the chart surface. Window-scoped
    // shortcuts steal ordinary input from the map and other application pages.
    auto *scZoomIn  = new QShortcut(QKeySequence(Qt::Key_Plus),  m_chartView);
    auto *scZoomIn2 = new QShortcut(QKeySequence(Qt::Key_Equal), m_chartView);
    auto *scZoomOut = new QShortcut(QKeySequence(Qt::Key_Minus), m_chartView);
    auto *scFit     = new QShortcut(QKeySequence(Qt::Key_F),     m_chartView);
    auto *scMarkers = new QShortcut(QKeySequence(Qt::Key_M),     m_chartView);
    auto *scValues  = new QShortcut(QKeySequence(Qt::Key_V),     m_chartView);
    auto *scTraces  = new QShortcut(QKeySequence(Qt::Key_T),     m_chartView);
    m_chartShortcuts = {
        scZoomIn, scZoomIn2, scZoomOut, scFit, scMarkers, scValues, scTraces,
    };
    for (auto *shortcut : m_chartShortcuts) {
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
    }
    connect(scZoomIn,  &QShortcut::activated, this, [this]() { zoomChartAxesAtCenter(true); });
    connect(scZoomIn2, &QShortcut::activated, this, [this]() { zoomChartAxesAtCenter(true); });
    connect(scZoomOut, &QShortcut::activated, this, [this]() { zoomChartAxesAtCenter(false); });
    connect(scFit,     &QShortcut::activated, this, &DashboardPage::onResetChartZoom);
    connect(scMarkers, &QShortcut::activated, this, [this]() {
        if (m_showMarkersToggle) {
            m_showMarkersToggle->setChecked(!m_showMarkersToggle->isChecked());
        }
    });
    connect(scValues, &QShortcut::activated, this, [this]() {
        if (m_showPointValuesToggle) {
            m_showPointValuesToggle->setChecked(!m_showPointValuesToggle->isChecked());
        }
    });
    connect(scTraces, &QShortcut::activated, this, [this]() {
        if (m_tracesToggleBtn) {
            m_tracesToggleBtn->setChecked(!m_tracesToggleBtn->isChecked());
        }
    });

    // ── View stack (Graph / Map / Empty state switcher) ──────────────────────
    m_viewStack = new QStackedWidget(chartFrame);
    m_viewStack->addWidget(m_chartView);   // index 0 — telemetry chart

    m_mapWidget = new Map3DWidget(m_viewStack);
    m_mapWidget->setImperialUnits(m_imperialUnits);
    m_mapWidget->setAccessibleName(u"Flight path map"_s);
    m_mapWidget->setAccessibleDescription(u"Interactive map showing the flight path and GPS coordinates"_s);
    m_viewStack->addWidget(m_mapWidget);   // index 1 — 3D flight path map
    m_viewStack->addWidget(m_emptyStateLabel); // index 2 — empty state
    m_viewStack->setCurrentIndex(2);
    updateToolbarForView();

    chartFrameLayout->addWidget(m_viewStack, 1);

    m_chartLoadingBar = new QProgressBar(chartFrame);
    m_chartLoadingBar->setObjectName(u"chartLoadingBar"_s);
    m_chartLoadingBar->setRange(0, 0);
    m_chartLoadingBar->setTextVisible(false);
    m_chartLoadingBar->setFixedHeight(3);
    m_chartLoadingBar->setVisible(false);
    chartFrameLayout->addWidget(m_chartLoadingBar, 0);

    connect(m_graphViewBtn, &QPushButton::clicked, this, [this](bool checked) {
        if (!checked) {
            m_graphViewBtn->setChecked(true);
            return;
        }
        m_viewStack->setCurrentIndex(0);
        m_mapViewBtn->setChecked(false);
        updateToolbarForView();
        processScheduledUpdates(true);
    });
    connect(m_mapViewBtn, &QPushButton::clicked, this, [this](bool checked) {
        if (!checked) {
            m_mapViewBtn->setChecked(true);
            return;
        }
        m_viewStack->setCurrentIndex(1);
        m_graphViewBtn->setChecked(false);
        updateToolbarForView();
        hideChartLoadingIndicator();
        processScheduledUpdates(true);
    });

    auto showDataView = [this]() {
        if (m_viewStack && m_viewStack->currentIndex() == 2) {
            m_viewStack->setCurrentIndex(0);
            if (m_graphViewBtn) m_graphViewBtn->setChecked(true);
            if (m_mapViewBtn) m_mapViewBtn->setChecked(false);
            updateToolbarForView();
            processScheduledUpdates(true);
        }
    };
    if (m_model) {
        connect(m_model, &FlightDataModel::displayedSampleChanged, this, showDataView);
    }

    chartColumn->addWidget(chartFrame, 1);

    // ── Splitter — traces panel | chart host ─────────────────────────────────
    // State (column widths) is persisted in QSettings so the user's layout
    // survives app restarts.
    auto *dashSplitter = new QSplitter(Qt::Horizontal, this);
    dashSplitter->setChildrenCollapsible(false);
    dashSplitter->addWidget(m_tracesPanel);
    dashSplitter->addWidget(chartHost);
    dashSplitter->setStretchFactor(0, 0);
    dashSplitter->setStretchFactor(1, 1);
    m_tracesPanel->setMinimumWidth(0);
    {
        QSettings dashSettings(kSettingsOrg, kSettingsApp);
        const QByteArray st = dashSettings.value(kSettingsDashSplitter).toByteArray();
        if (!st.isEmpty()) {
            dashSplitter->restoreState(st);
        } else {
            dashSplitter->setSizes({200, 1000});
        }
    }
    connect(dashSplitter, &QSplitter::splitterMoved, this, [dashSplitter]() {
        QSettings s(kSettingsOrg, kSettingsApp);
        s.setValue(kSettingsDashSplitter, dashSplitter->saveState());
    });
    connect(m_tracesToggleBtn, &QToolButton::toggled, this,
            [this, dashSplitter](bool show) {
        if (show) {
            m_tracesPanel->setVisible(true);
            dashSplitter->setSizes({200, dashSplitter->width() - 200});
        } else {
            m_tracesPanel->setVisible(false);
        }
    });
    rootLayout->addWidget(dashSplitter, 1);
    rootLayout->addWidget(m_replayBar);

    if (m_model) {
        connect(m_model, &FlightDataModel::displayedSampleChanged,
                this, &DashboardPage::onDisplayedSampleChanged);
        connect(m_model, &FlightDataModel::liveSamplesReceived,
                this, &DashboardPage::onLiveSamplesReceived);
        connect(m_model, &FlightDataModel::sessionReset,  this, &DashboardPage::onSessionReset);
        connect(m_model, &FlightDataModel::liveSamplesReceived,
                m_mapWidget, &Map3DWidget::onLiveSamplesReceived);
        connect(m_model, &FlightDataModel::sessionReset,
                m_mapWidget, &Map3DWidget::onSessionReset);
    }

    refreshAllSeriesFromData();

    connect(&cosmo::ThemeManager::instance(), &cosmo::ThemeManager::themeChanged,
            this, &DashboardPage::applyChartTheme);
    connect(&cosmo::ThemeManager::instance(), &cosmo::ThemeManager::themeChanged,
            this, &DashboardPage::refreshPageStyleSheet);
}

void DashboardPage::setImperialUnits(bool imperial) {
    if (m_imperialUnits == imperial) {
        return;
    }
    m_imperialUnits = imperial;
    if (m_tracesPanel) {
        m_tracesPanel->setImperialUnits(imperial);
    }
    if (m_mapWidget) {
        m_mapWidget->setImperialUnits(imperial);
    }
    for (int metricIndex = 0; metricIndex < kMetricCount; ++metricIndex) {
        if (auto *series = m_lineSeries[static_cast<std::size_t>(metricIndex)]) {
            series->setName(metricDisplayTitle(metricIndex, m_imperialUnits));
        }
    }
    refreshAllSeriesFromData();
}

void DashboardPage::applyChartTheme() {
    if (!m_chart || !m_axisX || !m_axisY) {
        return;
    }
    const QColor bg(Theme::kBgDark());
    const QColor plotBg(Theme::kBgBase());
    const QColor labelCol(Theme::kTextMid());
    const QColor gridCol(Theme::kBorderSubtle());
    const QPen gridPen(gridCol, 1, Qt::DotLine);

    for (int metricIndex = 0; metricIndex < kMetricCount; ++metricIndex) {
        auto *series = m_lineSeries[static_cast<std::size_t>(metricIndex)];
        if (!series) {
            continue;
        }
        const QColor color = metricColor(metricIndex);
        series->setColor(color);
        series->setPen(QPen(color, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    }
    refreshEventMarkerTheme();

    m_chart->setBackgroundBrush(bg);
    m_chart->setBackgroundPen(Qt::NoPen);
    m_chart->setPlotAreaBackgroundBrush(plotBg);
    m_chart->setPlotAreaBackgroundVisible(true);

    QFont axisFont = QApplication::font();
    axisFont.setFamily(u"Red Hat Mono"_s);
    QFont titleFont = axisFont;
    if (titleFont.pointSizeF() > 0.0) {
        titleFont.setPointSizeF(titleFont.pointSizeF() * 1.15);
    }
    titleFont.setBold(true);

    m_chart->setTitleFont(titleFont);
    m_chart->setTitleBrush(labelCol);

    QList<QValueAxis *> axes = {m_axisX, m_axisY};
    if (m_axisY2) axes.append(m_axisY2);
    for (auto *ax : axes) {
        ax->setLabelsFont(axisFont);
        ax->setTitleFont(axisFont);
        ax->setLabelsColor(labelCol);
        ax->setTitleBrush(QColor(Theme::kTextDim()));
        ax->setLinePenColor(QColor(Theme::kBorderDefault()));
        ax->setGridLinePen(gridPen);
        ax->setMinorGridLineVisible(false);
    }

    m_chart->legend()->setVisible(true);
    m_chart->legend()->setAlignment(Qt::AlignBottom);
    m_chart->legend()->setLabelColor(labelCol);
    m_chart->legend()->setBackgroundVisible(true);
    m_chart->legend()->setBrush(QColor(Theme::kBgPanel()));
    m_chart->legend()->setPen(QPen(QColor(Theme::kBorderPanel()), 1));
    m_chart->setMargins(QMargins(2, 2, 2, 6));
}

void DashboardPage::buildChartToolbar(QWidget *chartHeader, QVBoxLayout *chartHeaderLay) {
    auto *chartToolbar = new QHBoxLayout();
    chartToolbar->setSpacing(6);

    const auto toolGroupStyle = QString(u"background: %1; border-radius: 6px; border: none;"_s).arg(Theme::kBgPanel());

    m_tracesToggleBtn = new QToolButton(chartHeader);
    m_tracesToggleBtn->setObjectName(u"chartToggleBtn"_s);
    m_tracesToggleBtn->setText(u"● Traces"_s);
    m_tracesToggleBtn->setCheckable(true);
    m_tracesToggleBtn->setChecked(true);
    m_tracesToggleBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_tracesToggleBtn->setCursor(Qt::PointingHandCursor);
    m_tracesToggleBtn->setToolTip(u"Show/hide the traces side panel  [T key]"_s);
    chartToolbar->addWidget(m_tracesToggleBtn);
    connect(m_tracesToggleBtn, &QToolButton::toggled, this, [this](bool on) {
        m_tracesToggleBtn->setText(on ? u"● Traces"_s : u"○ Traces"_s);
    });

    auto *viewGroup = new QWidget(chartHeader);
    viewGroup->setObjectName(u"viewSwitchGroup"_s);
    auto *viewGroupLay = new QHBoxLayout(viewGroup);
    viewGroupLay->setContentsMargins(3, 3, 3, 3);
    viewGroupLay->setSpacing(2);

    m_graphViewBtn = new QPushButton(u"⊞ Graph"_s, viewGroup);
    m_graphViewBtn->setObjectName(u"viewSwitchBtn"_s);
    m_graphViewBtn->setCheckable(true);
    m_graphViewBtn->setChecked(true);
    m_graphViewBtn->setCursor(Qt::PointingHandCursor);
    m_graphViewBtn->setToolTip(u"Show telemetry chart"_s);
    viewGroupLay->addWidget(m_graphViewBtn);

    m_mapViewBtn = new QPushButton(u"◎ Map"_s, viewGroup);
    m_mapViewBtn->setObjectName(u"viewSwitchBtn"_s);
    m_mapViewBtn->setCheckable(true);
    m_mapViewBtn->setChecked(false);
    m_mapViewBtn->setCursor(Qt::PointingHandCursor);
    m_mapViewBtn->setToolTip(u"Show flight path map"_s);
    viewGroupLay->addWidget(m_mapViewBtn);
    chartToolbar->addWidget(viewGroup);

    m_zoomGroup = new QWidget(chartHeader);
    m_zoomGroup->setStyleSheet(toolGroupStyle);
    auto *zoomGroupLay = new QHBoxLayout(m_zoomGroup);
    zoomGroupLay->setContentsMargins(2, 2, 2, 2);
    zoomGroupLay->setSpacing(4);

    m_zoomOutBtn = new QPushButton(u"−"_s, m_zoomGroup);
    m_zoomOutBtn->setObjectName(u"chartZoomBtn"_s);
    m_zoomOutBtn->setToolTip(u"Zoom out ×2  [− key · scroll down]"_s);
    zoomGroupLay->addWidget(m_zoomOutBtn);

    m_zoomInBtn = new QPushButton(u"+"_s, m_zoomGroup);
    m_zoomInBtn->setObjectName(u"chartZoomBtn"_s);
    m_zoomInBtn->setToolTip(u"Zoom in ×2  [+ key · scroll up]"_s);
    zoomGroupLay->addWidget(m_zoomInBtn);

    m_zoomResetBtn = new QPushButton(u"Fit"_s, m_zoomGroup);
    m_zoomResetBtn->setObjectName(u"chartToolbarBtn"_s);
    m_zoomResetBtn->setToolTip(u"Fit chart to full data range  [F key]"_s);
    zoomGroupLay->addWidget(m_zoomResetBtn);
    connect(m_zoomResetBtn, &QPushButton::clicked, this, &DashboardPage::onResetChartZoom);
    chartToolbar->addWidget(m_zoomGroup);

    m_toggleGroup = new QWidget(chartHeader);
    m_toggleGroup->setStyleSheet(toolGroupStyle);
    auto *toggleGroupLay = new QHBoxLayout(m_toggleGroup);
    toggleGroupLay->setContentsMargins(2, 2, 2, 2);
    toggleGroupLay->setSpacing(4);

    m_showMarkersToggle = new QToolButton(m_toggleGroup);
    m_showMarkersToggle->setObjectName(u"chartToggleBtn"_s);
    m_showMarkersToggle->setText(u"● Markers"_s);
    m_showMarkersToggle->setCheckable(true);
    m_showMarkersToggle->setChecked(true);
    m_showMarkersToggle->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_showMarkersToggle->setCursor(Qt::PointingHandCursor);
    m_showMarkersToggle->setToolTip(
        u"Draw a dot on each sample — auto-disabled above 400 pts/trace.  [M key]"_s);
    toggleGroupLay->addWidget(m_showMarkersToggle);
    connect(m_showMarkersToggle, &QToolButton::toggled, this, [this](bool on) {
        m_showMarkersToggle->setText(on ? u"● Markers"_s : u"○ Markers"_s);
    });

    m_showPointValuesToggle = new QToolButton(m_toggleGroup);
    m_showPointValuesToggle->setObjectName(u"chartToggleBtn"_s);
    m_showPointValuesToggle->setText(u"○ Values"_s);
    m_showPointValuesToggle->setCheckable(true);
    m_showPointValuesToggle->setChecked(false);
    m_showPointValuesToggle->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_showPointValuesToggle->setCursor(Qt::PointingHandCursor);
    m_showPointValuesToggle->setToolTip(
        u"Show Y value next to each point — single trace only, max 100 points.  [V key]"_s);
    toggleGroupLay->addWidget(m_showPointValuesToggle);
    connect(m_showPointValuesToggle, &QToolButton::toggled, this, [this](bool on) {
        m_showPointValuesToggle->setText(on ? u"● Values"_s : u"○ Values"_s);
    });

    m_followToggle = new QToolButton(m_toggleGroup);
    m_followToggle->setObjectName(u"chartToggleBtn"_s);
    m_followToggle->setText(u"● Follow"_s);
    m_followToggle->setCheckable(true);
    m_followToggle->setChecked(true);
    m_followToggle->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_followToggle->setCursor(Qt::PointingHandCursor);
    m_followToggle->setToolTip(
        u"Auto-fit chart axes to data on each update.\n"
        u"Turn off to preserve your zoom level during replay.\n"
        u"Pan or zoom the chart to disable automatically."_s);
    toggleGroupLay->addWidget(m_followToggle);
    connect(m_followToggle, &QToolButton::toggled, this, [this](bool on) {
        m_followToggle->setText(on ? u"● Follow"_s : u"○ Follow"_s);
    });
    chartToolbar->addWidget(m_toggleGroup);

    connect(m_showMarkersToggle, &QToolButton::toggled, this, &DashboardPage::onChartVisualOptionsToggled);
    connect(m_showPointValuesToggle, &QToolButton::toggled, this, &DashboardPage::onChartVisualOptionsToggled);
    connect(m_followToggle, &QToolButton::toggled, this, [this](bool checked) {
        if (checked) {
            m_preserveChartAxes = false;
            refreshAllSeriesFromData();
        } else {
            m_preserveChartAxes = true;
        }
    });

    chartToolbar->addStretch(1);

    m_actionGroup = new QWidget(chartHeader);
    m_actionGroup->setStyleSheet(toolGroupStyle);
    auto *actionGroupLay = new QHBoxLayout(m_actionGroup);
    actionGroupLay->setContentsMargins(2, 2, 2, 2);
    actionGroupLay->setSpacing(4);

    auto *addMarkerBtn = new QPushButton(u"Marker"_s, m_actionGroup);
    addMarkerBtn->setObjectName(u"chartToolbarBtn"_s);
    addMarkerBtn->setToolTip(u"Add a named event marker at the chart center time"_s);
    actionGroupLay->addWidget(addMarkerBtn);
    connect(addMarkerBtn, &QPushButton::clicked, this, [this]() {
        if (!m_axisX) return;
        const double centerT = (m_axisX->min() + m_axisX->max()) * 0.5;
        bool ok = false;
        QString name = QInputDialog::getText(
            this, u"Add Event Marker"_s,
            QStringLiteral("Name for marker at t = %1 s:").arg(centerT, 0, 'f', 2),
            QLineEdit::Normal, {}, &ok);
        if (ok && !name.isEmpty()) {
            addEventMarker(centerT, name);
        }
    });

    auto *clearMarkersBtn = new QPushButton(u"Clear Markers"_s, m_actionGroup);
    clearMarkersBtn->setObjectName(u"chartToolbarBtn"_s);
    clearMarkersBtn->setToolTip(u"Remove all event markers"_s);
    actionGroupLay->addWidget(clearMarkersBtn);
    connect(clearMarkersBtn, &QPushButton::clicked, this, &DashboardPage::clearEventMarkers);

    auto *copyChartBtn = new QPushButton(u"Copy"_s, m_actionGroup);
    copyChartBtn->setObjectName(u"chartToolbarBtn"_s);
    copyChartBtn->setToolTip(u"Copy chart image to clipboard"_s);
    actionGroupLay->addWidget(copyChartBtn);
    connect(copyChartBtn, &QPushButton::clicked, this, [this]() {
        if (m_chartView) {
            QPixmap pixmap = m_chartView->grab();
            QApplication::clipboard()->setPixmap(pixmap);
        }
    });

    auto *copyDataBtn = new QPushButton(u"Copy Data"_s, m_actionGroup);
    copyDataBtn->setObjectName(u"chartToolbarBtn"_s);
    copyDataBtn->setToolTip(u"Copy current sample data as text (time, all metrics, GPS)"_s);
    actionGroupLay->addWidget(copyDataBtn);
    connect(copyDataBtn, &QPushButton::clicked, this, [this]() {
        const FlightSample *sample = nullptr;
        if (m_session && !m_session->samples.empty()) {
            int idx = std::min(m_lastReplayTrailLength, static_cast<int>(m_session->samples.size())) - 1;
            if (idx >= 0) sample = &m_session->samples[static_cast<std::size_t>(idx)];
        } else if (!m_liveSamples.empty()) {
            sample = &m_liveSamples.back();
        }
        if (!sample) return;
        QString text;
        text += QStringLiteral("Time: %1 ms\n").arg(sample->timestamp);
        const auto appendDisplayMetric = [this, sample, &text](
                                             const QString &label,
                                             int metricIndex) {
            const double siValue = sampleValueForMetric(*sample, metricIndex);
            text += QStringLiteral("%1: %2")
                        .arg(label, formatMetricDisplayValue(
                                        metricIndex, siValue, m_imperialUnits));
            if (std::isfinite(siValue)) {
                const QString unit = metricDisplayUnitShort(
                    metricIndex, m_imperialUnits);
                if (!unit.isEmpty()) {
                    text += u' ' + unit;
                }
            }
            text += u'\n';
        };
        appendDisplayMetric(u"Altitude"_s, 0);
        appendDisplayMetric(u"Temperature"_s, 1);
        appendDisplayMetric(u"Pressure"_s, 2);
        appendDisplayMetric(u"Acceleration"_s, 3);
        text += QStringLiteral("Battery: %1 V\n").arg(sample->batteryVoltage, 0, 'f', 2);
        if (std::isfinite(sample->coordinates.latitude) && std::abs(sample->coordinates.latitude) > 1e-9) {
            text += QStringLiteral("Latitude: %1\n").arg(sample->coordinates.latitude, 0, 'f', 8);
            text += QStringLiteral("Longitude: %1\n").arg(sample->coordinates.longitude, 0, 'f', 8);
        }
        QApplication::clipboard()->setText(text);
    });
    chartToolbar->addWidget(m_actionGroup);

    auto *helpBtn = new QToolButton(chartHeader);
    helpBtn->setObjectName(u"chartHelpBtn"_s);
    helpBtn->setText(u"?"_s);
    helpBtn->setToolTip(
        u"Chart controls:\n"
        u"  Drag: pan  ·  Ctrl+drag: zoom rectangle\n"
        u"  Scroll wheel / trackpad: zoom at pointer\n"
        u"  ⌘/Ctrl + two-finger swipe: zoom at pointer\n"
        u"  − / + keys: zoom out / in  ·  F: fit to data\n"
        u"  M: toggle markers  ·  V: toggle point values\n"
        u"  Hover cursor: floating readout + crosshair + snap dot"_s);
    m_chartHelpBtn = helpBtn;
    chartToolbar->addWidget(m_chartHelpBtn);

    chartHeaderLay->addLayout(chartToolbar);
}

QString DashboardPage::buildDashboardQss() {
    const auto fontMono    = QString::fromUtf8(Theme::kFontMono);
    const auto borderPanel = Theme::kBorderPanel();
    const auto borderLight = Theme::kBorderLight();
    const auto bgButton    = Theme::kBgButton();
    const auto textPri     = Theme::kTextPrimary();
    const auto textMuted   = Theme::kTextMuted();
    const auto bgPanel     = Theme::kBgPanel();
    const auto btnHov      = Theme::kBtnHover();
    const auto btnPressed  = Theme::kBtnPressed();
    const auto accent      = Theme::kAccentLink();
    const auto bgInput     = Theme::kBgInput();
    const auto checkedText = Theme::kBgBase();

    auto ss = QString(uR"(
        #dashboardPage { background-color: transparent; color: %1; }
        QFrame#chartFrame { background-color: %2; border: 1px solid %3; border-radius: %4px; padding: 0px; }
        #telemetryChartView { border: none; padding: 0px; margin: 0px; background-color: transparent; }
        QToolButton#chartToggleBtn {
            border: 1px solid %5;
            border-radius: %6px;
            padding: 6px 14px;
            min-height: 32px;
            background-color: %7;
            color: %1;
            font-size: 11px;
            font-weight: 500;
        }
        QToolButton#chartToggleBtn:hover { background-color: %8; color: %1; border-color: %5; }
    )"_s)
        .arg(textPri).arg(bgPanel).arg(borderPanel)
        .arg(Theme::kRadiusMd).arg(borderLight)
        .arg(Theme::kRadiusSm).arg(bgButton)
        .arg(btnHov);

    ss += QString(uR"(
        QToolButton#chartToggleBtn:checked {
            background-color: %1;
            border-color: %1;
            color: %10;
            font-weight: 600;
        }
        QToolButton#chartToggleBtn:checked:hover {
            background-color: %1;
            border-color: %1;
            color: %10;
        }
        QToolButton#chartToggleBtn:pressed {
            background-color: %9;
        }
        QPushButton#chartZoomBtn {
            min-width: 32px;
            max-width: 32px;
            min-height: 32px;
            max-height: 32px;
            padding: 0px;
            font-weight: 600;
            font-size: 16px;
            border: 1px solid %2;
            border-radius: %3px;
            background-color: %4;
            color: %5;
        }
        QPushButton#chartZoomBtn:hover { background-color: %6; border-color: %7; }
        QPushButton#chartZoomBtn:pressed { background-color: %8; }
    )"_s)
        .arg(accent).arg(borderPanel).arg(Theme::kRadiusSm)
        .arg(bgButton).arg(textPri).arg(btnHov)
        .arg(borderLight).arg(btnPressed).arg(btnPressed)
        .arg(checkedText);  // %10 - validated against the accent background

    ss += QString(uR"(
        QDoubleSpinBox { background-color: %1; color: %2; border: 1px solid %3; border-radius: %4px; padding: 4px 8px; min-height: 22px; }
        QSlider::groove:horizontal { height: 6px; background: %5; border-radius: 3px; }
        QSlider::handle:horizontal { width: 14px; margin: -5px 0; background: %6; border: 1px solid %3; border-radius: %4px; }
        QPushButton#chartToolbarBtn {
            border: 1px solid %7;
            border-radius: %4px;
            padding: 6px 14px;
            min-height: 32px;
            background-color: %8;
            color: %2;
            font-size: 11px;
            font-weight: 500;
        }
    )"_s)
        .arg(bgInput).arg(textPri).arg(Theme::kBorderDefault())
        .arg(Theme::kRadiusSm).arg(bgPanel).arg(borderLight)
        .arg(borderPanel).arg(bgButton);

    ss += QString(uR"(
        QPushButton#chartToolbarBtn:hover {
            background-color: %1;
            border-color: %2;
        }
        QPushButton#chartToolbarBtn:pressed {
            background-color: %3;
        }
        QWidget#viewSwitchGroup { background: %4; border: 1px solid %4; border-radius: %5px; }
        QPushButton#viewSwitchBtn { border: none; border-radius: %5px; padding: 6px 16px; min-height: 32px; background-color: transparent; color: %6; font-size: %7px; font-weight: 500; }
        QPushButton#viewSwitchBtn:checked { background-color: %8; color: %10; font-weight: 600; }
        QPushButton#viewSwitchBtn:hover:!checked { background-color: %1; color: %9; }
        QPushButton#viewSwitchBtn:pressed:!checked { background-color: %3; }
    )"_s)
        .arg(btnHov).arg(borderLight).arg(btnPressed)
        .arg(borderPanel).arg(Theme::kRadiusMd).arg(textMuted)
        .arg(Theme::kFontSizeSm).arg(accent).arg(textPri)
        .arg(checkedText);  // %10 - validated against the accent background

    ss += QString(uR"(
        QToolButton#chartHelpBtn { font-weight: 700; font-size: %1px; min-width: 30px; max-width: 30px; min-height: 30px; max-height: 30px; border: 1px solid %2; border-radius: 15px; background: %3; color: %4; padding: 0px; }
        QToolButton#chartHelpBtn:hover { color: %5; border-color: %6; background: %7; }
    )"_s)
        .arg(Theme::kFontSizeMd).arg(borderPanel).arg(bgButton)
        .arg(textMuted).arg(textPri).arg(borderLight)
        .arg(btnHov);

    ss += QString(uR"(
        QLabel#chartEmptyState {
            color: %1;
            font-size: 14px;
            padding: 40px;
            background: transparent;
            border: none;
        }
        QProgressBar#chartLoadingBar {
            background: transparent;
            border: none;
        }
        QProgressBar#chartLoadingBar::chunk {
            background: %2;
        }
    )"_s).arg(textMuted).arg(accent);

    ss += QString(uR"(
        QToolButton#chartToggleBtn:focus,
        QPushButton#chartZoomBtn:focus,
        QPushButton#chartToolbarBtn:focus,
        QPushButton#viewSwitchBtn:focus,
        QToolButton#chartHelpBtn:focus,
        QDoubleSpinBox:focus,
        QSlider:focus,
        #telemetryChartView:focus {
            border: 2px solid %1;
        }
    )"_s).arg(Theme::kFocusRing());

    return ss;
}

void DashboardPage::refreshPageStyleSheet() {
    setStyleSheet(buildDashboardQss());

    if (m_sessionInfoLabel) {
        const auto textMuted = Theme::kTextMuted();
        const auto bgPanel = Theme::kBgPanel();
        const auto borderPanel = Theme::kBorderPanel();
        m_sessionInfoLabel->setStyleSheet(
            QString(u"color: %1; font-size: 11px; background: %2; "
            u"border: 1px solid %3; border-radius: 8px; padding: 8px 12px;"_s)
                .arg(textMuted).arg(bgPanel).arg(borderPanel));
    }

    // Refresh toolbar group backgrounds
    const auto toolGroupStyle = QString(u"background: %1; border-radius: 6px; border: none;"_s).arg(Theme::kBgPanel());
    if (m_zoomGroup) {
        m_zoomGroup->setStyleSheet(toolGroupStyle);
    }
    if (m_toggleGroup) {
        m_toggleGroup->setStyleSheet(toolGroupStyle);
    }
    if (m_actionGroup) {
        m_actionGroup->setStyleSheet(toolGroupStyle);
    }

    // Force button style updates for checkable buttons
    auto forceButtonStyleUpdate = [](QWidget *widget) {
        if (widget) {
            widget->style()->unpolish(widget);
            widget->style()->polish(widget);
            widget->update();
        }
    };

    forceButtonStyleUpdate(m_graphViewBtn);
    forceButtonStyleUpdate(m_mapViewBtn);
    forceButtonStyleUpdate(m_zoomOutBtn);
    forceButtonStyleUpdate(m_zoomInBtn);
    forceButtonStyleUpdate(m_zoomResetBtn);
    forceButtonStyleUpdate(m_tracesToggleBtn);
    forceButtonStyleUpdate(m_showMarkersToggle);
    forceButtonStyleUpdate(m_showPointValuesToggle);
    forceButtonStyleUpdate(m_followToggle);
}

void DashboardPage::updateToolbarForView() {
    const bool graphMode = m_viewStack && m_viewStack->currentIndex() == 0;
    if (m_zoomGroup)      m_zoomGroup->setVisible(graphMode);
    if (m_toggleGroup)    m_toggleGroup->setVisible(graphMode);
    if (m_actionGroup)    m_actionGroup->setVisible(graphMode);
    if (m_chartHelpBtn)   m_chartHelpBtn->setVisible(graphMode);
    if (m_tracesToggleBtn) m_tracesToggleBtn->setVisible(graphMode);
    for (auto *shortcut : m_chartShortcuts) {
        if (shortcut) {
            shortcut->setEnabled(graphMode);
        }
    }
}

int DashboardPage::countEnabledMetrics() const {
    int n = 0;
    for (int i = 0; i < kMetricCount; ++i) {
        if (m_metricEnabled[static_cast<std::size_t>(i)]) {
            ++n;
        }
    }
    return n;
}

void DashboardPage::onResetChartZoom() {
    refreshAllSeriesFromData();
}

void DashboardPage::onChartVisualOptionsToggled() {
    const int nEn = countEnabledMetrics();
    for (int mi = 0; mi < kMetricCount; ++mi) {
        auto *s = m_lineSeries[static_cast<std::size_t>(mi)];
        if (s && m_metricEnabled[static_cast<std::size_t>(mi)]) {
            applySeriesPointDisplay(s, s->count(), nEn);
        }
    }
}

void DashboardPage::applySeriesPointDisplay(QLineSeries *series, int pointCount, int nEnabledMetrics) const {
    if (!series) {
        return;
    }

    const bool markersOn = !m_showMarkersToggle || m_showMarkersToggle->isChecked();
    const bool showVertices = markersOn && pointCount > 0 && pointCount <= 400;
    series->setPointsVisible(showVertices);

    const bool wantLabels = m_showPointValuesToggle && m_showPointValuesToggle->isChecked();
    const bool showLabels = wantLabels && nEnabledMetrics == 1 && pointCount > 0 && pointCount <= 100;
    series->setPointLabelsVisible(showLabels);
    series->setPointLabelsFormat(showLabels ? u"@yPoint"_s : QString());
    if (showLabels) {
        series->setPointLabelsClipping(true);
        series->setPointLabelsColor(QColor(Theme::kTextPrimary()));
    }

    // Qt Charts' OpenGL series backend can create an invalid QOpenGLWidget
    // context under Qt 6's default Metal composition on macOS.
    series->setUseOpenGL(false);
}

/**
 * Builds the multi-line text shown in the floating hover overlay.
 *
 * @p displayPointIndex1Based is the 1-based index into the *decimated* series
 * (i.e. the index Qt Charts reports from the hover position).  It is first
 * mapped through m_hoverSampleIndexMap to the original logical sample index so
 * the values reflect the actual sample, not the interpolated display point.
 */
QString DashboardPage::formatMultiMetricHover(double tSec, int displayPointIndex1Based) const {
    if (displayPointIndex1Based < 1) {
        return {};
    }
    const int di = displayPointIndex1Based - 1;
    int si = di;
    if (!m_hoverSampleIndexMap.empty()) {
        if (di < 0 || di >= static_cast<int>(m_hoverSampleIndexMap.size())) {
            return {};
        }
        si = m_hoverSampleIndexMap[static_cast<size_t>(di)];
    }
    const FlightSample *sp = nullptr;
    if (m_model && m_model->replayMode()) {
        if (m_session && si >= 0 && si < static_cast<int>(m_session->samples.size())) {
            sp = &m_session->samples[static_cast<std::size_t>(si)];
        }
    } else {
        // Hover indices belong to the immutable snapshot used for the most
        // recent chart build. The live deque may have shifted since then.
        if (si >= 0 && si < static_cast<int>(m_liveChartScratch.size())) {
            sp = &m_liveChartScratch[static_cast<std::size_t>(si)];
        }
    }
    if (!sp) {
        return {};
    }
    const int totalLogical = m_hoverLogicalSampleCount > 0 ? m_hoverLogicalSampleCount : static_cast<int>(m_liveSamples.size());
    QStringList lines;
    lines << QStringLiteral("Time %1 s  ·  row %2 / %3")
                 .arg(tSec, 0, 'f', 3)
                 .arg(si + 1)
                 .arg(std::max(1, totalLogical));
    for (int mi = 0; mi < kMetricCount; ++mi) {
        if (!m_metricEnabled[static_cast<std::size_t>(mi)]) {
            continue;
        }
        const double siValue = sampleValueForMetric(*sp, mi);
        const QString valueText = std::isfinite(siValue)
            ? formatMetricDisplayValue(mi, siValue, m_imperialUnits)
            : u"—"_s;
        lines << QStringLiteral("  • %1: %2 %3")
                     .arg(metricQuantityName(mi), valueText,
                          metricDisplayUnitShort(mi, m_imperialUnits));
    }
    return lines.join(u"\n"_s);
}

void DashboardPage::setReplaySession(
    std::shared_ptr<const FlightSession> session,
    std::shared_ptr<const cosmo::preview::FlightPreviewCache> preview) {
    m_preserveChartAxes = false;
    if (m_renderSchedulerTimer) m_renderSchedulerTimer->stop();

    m_session = std::move(session);
    const bool previewMatchesSession = m_session && preview
        && preview->wasBuiltFor(*m_session)
        && preview->displaySeconds().size() == m_session->samples.size();
    m_preview = previewMatchesSession ? std::move(preview) : nullptr;
    m_liveChartDirty = false;
    m_replayChartDirty = true;
    if (m_tracesPanel) {
        m_tracesPanel->setMetricsOffered(traceOfferMaskForSession(m_session.get()));
    }
    const int n = m_session ? static_cast<int>(m_session->samples.size()) : 0;

    if (m_replayBar) m_replayBar->setSession(m_session, m_preview);

    if (m_mapWidget) m_mapWidget->setReplaySession(m_session, m_preview);

    if (m_sessionInfoLabel) {
        QStringList notes;
        if (m_preview && m_preview->correctedTimelineUsed()) {
            notes << QStringLiteral("Corrected %1 timestamp jump(s) for preview timing.")
                         .arg(m_preview->timestampDiscontinuityCount());
        }
        if (m_preview && m_preview->droppedGpsRows() > 0) {
            notes << QStringLiteral("Ignored %1 invalid or placeholder GPS row(s) in map preview.")
                         .arg(m_preview->droppedGpsRows());
        }
        m_sessionInfoLabel->setText(notes.join(u"  "_s));
        m_sessionInfoLabel->setVisible(!notes.isEmpty());
    }

    if (n > 0 && m_viewStack && m_viewStack->currentIndex() == 2) {
        m_viewStack->setCurrentIndex(0);
        if (m_graphViewBtn) m_graphViewBtn->setChecked(true);
        if (m_mapViewBtn) m_mapViewBtn->setChecked(false);
    }


    if (n > 0) {
        m_lastReplayTrailLength = n;
        m_replayChartBuiltTrailLength = -1;
        m_replayChartBuiltBucket = -1;
        if (m_replay) m_replay->setPosition(n);
    } else {
        m_lastReplayTrailLength = 0;
        m_replayChartBuiltTrailLength = -1;
        m_replayChartBuiltBucket = -1;
    }
    processScheduledUpdates(true);
    scheduleRenderPass();
}

/**
 * Applies a new replay trail length coming from the controller or slider.
 *
 * During active playback the chart rebuild is coalesced (scheduled) rather than
 * immediate so fast controller ticks don't flood Qt Charts.  When paused or
 * stopped the rebuild is synchronous.  If the trail length hasn't changed and
 * the chart was already built for it, only the UI labels are updated to avoid a
 * redundant and expensive series rebuild.
 */
void DashboardPage::applyReplayControllerPosition(int trailLength) {
    if (!m_followToggle || m_followToggle->isChecked())
        m_preserveChartAxes = false;
    m_lastReplayTrailLength = trailLength;
    if (m_mapWidget) m_mapWidget->setReplayTrailLength(trailLength);
    if (m_replay && m_replay->isPlaying()) {
        if (m_preview) {
            const int bucket = m_preview->replayBucket(trailLength, chartPointBudget());
            if (bucket == m_replayChartBuiltBucket) {
                if (!isVisible() || !graphViewIsActive()) {
                    m_replayChartDirty = true;
                }
                scheduleRenderPass();
                return;
            }
            m_replayChartBuiltBucket = bucket;
        }
        scheduleReplayChartRebuild();
        return;
    }
    if (m_model && m_model->replayMode() && trailLength == m_replayChartBuiltTrailLength) {
        scheduleRenderPass();
        return;  // chart already correct; ReplayBar updates its own labels via positionChanged
    }
    scheduleReplayChartRebuild();
}

/** Marks replay data dirty; the shared scheduler enforces the 30 Hz cap. */
void DashboardPage::scheduleReplayChartRebuild() {
    m_replayChartDirty = true;
    if (graphViewIsActive() && isVisible()) showChartLoadingIndicator();
    scheduleRenderPass();
}

/**
 * Cancels any pending coalesced rebuild and forces an immediate one.
 *
 * Called on playback pause/stop so the chart snaps to the exact final position
 * without waiting for the timer to fire.
 */
void DashboardPage::flushReplayChartRebuild() {
    if ((m_replay && m_replay->isPlaying())
        || !m_model || !m_model->replayMode()
        || (!m_replayChartDirty
            && m_replayChartBuiltTrailLength == m_lastReplayTrailLength)) {
        return;
    }
    m_replayChartDirty = true;
    processScheduledUpdates(true);
}

void DashboardPage::setReplayTrailLength(int trailLength) {
    if (m_replayBar) m_replayBar->setTrailLength(trailLength);
    applyReplayControllerPosition(trailLength);
}

void DashboardPage::refreshAllSeriesFromData() {
    m_preserveChartAxes = false;
    if (m_model && m_model->replayMode()) {
        m_replayChartDirty = true;
    } else {
        m_liveChartDirty = true;
    }
    processScheduledUpdates(true);
    scheduleRenderPass();
    if (m_followToggle && !m_followToggle->isChecked())
        m_preserveChartAxes = true;
}

bool DashboardPage::graphViewIsActive() const {
    return m_viewStack && m_viewStack->currentIndex() == 0;
}

void DashboardPage::scheduleRenderPass() {
    if (!m_renderSchedulerTimer) {
        return;
    }
    if (!isVisible()) {
        m_renderSchedulerTimer->stop();
        return;
    }

    const qint64 now = m_renderClock.elapsed();
    qint64 delayMs = std::numeric_limits<qint64>::max();
    const auto consider = [&](bool dirty, qint64 lastRenderMs, int intervalMs) {
        if (!dirty) {
            return;
        }
        const qint64 elapsed = std::max<qint64>(0, now - lastRenderMs);
        delayMs = std::min(delayMs, std::max<qint64>(0, intervalMs - elapsed));
    };

    if (graphViewIsActive()) {
        const bool replayMode = m_model && m_model->replayMode();
        consider(replayMode ? m_replayChartDirty : m_liveChartDirty,
                 replayMode ? m_lastReplayChartRenderMs : m_lastLiveChartRenderMs,
                 replayMode ? kInteractiveIntervalMs : kLiveChartIntervalMs);
    }
    if (delayMs == std::numeric_limits<qint64>::max()) {
        m_renderSchedulerTimer->stop();
        return;
    }
    const int boundedDelay = static_cast<int>(std::min<qint64>(
        delayMs, std::numeric_limits<int>::max()));
    if (m_renderSchedulerTimer->isActive()
        && m_renderSchedulerTimer->remainingTime() <= boundedDelay) {
        return;
    }
    m_renderSchedulerTimer->start(boundedDelay);
}

void DashboardPage::processScheduledUpdates(bool forceImmediate) {
    if (m_renderSchedulerTimer) {
        m_renderSchedulerTimer->stop();
    }
    if (!isVisible()) {
        return;
    }

    const qint64 now = m_renderClock.elapsed();
    if (graphViewIsActive()) {
        const bool replayMode = m_model && m_model->replayMode();
        const qint64 lastRender = replayMode
            ? m_lastReplayChartRenderMs
            : m_lastLiveChartRenderMs;
        const int interval = replayMode
            ? kInteractiveIntervalMs
            : kLiveChartIntervalMs;
        const bool due = forceImmediate || now - lastRender >= interval;
        bool &dirty = replayMode ? m_replayChartDirty : m_liveChartDirty;
        if (dirty && due) {
            dirty = false;
            if (replayMode) {
                rebuildReplayCharts(m_lastReplayTrailLength);
                m_lastReplayChartRenderMs = m_renderClock.elapsed();
            } else {
                rebuildLiveSeriesFromHistory();
                m_lastLiveChartRenderMs = m_renderClock.elapsed();
            }
            hideChartLoadingIndicator();
        }
    }

    scheduleRenderPass();
}

/**
 * Rebuilds all chart series from the first @p trailLength samples of m_session.
 *
 * Design notes:
 * - Reduction: if trailLength > kMaxChartDisplayPoints, LTTB keeps a bounded
 *   representative set. The original indices remain in m_hoverSampleIndexMap.
 * - Normalization: with three or more enabled metrics, each series is scaled to
 *   [0, 1]. One metric uses its raw axis and two metrics use separate raw axes.
 * - m_replayChartBuiltTrailLength guards against redundant rebuilds during
 *   playback: if the trail hasn't changed since the last build the function is
 *   a no-op (only labels are updated).
 * - m_preserveChartAxes suppresses automatic axis rescaling after the user has
 *   manually panned or zoomed.
 */
void DashboardPage::rebuildReplayCharts(int trailLength) {
    if (!m_axisX || !m_axisY) {
        return;
    }

    if (m_chartView) m_chartView->invalidateHoverSeriesCache();

    if (!m_session || trailLength <= 0 || m_session->samples.empty()) {
        for (QLineSeries *series : m_lineSeries) {
            if (series) {
                series->clear();
                series->setVisible(false);
            }
        }
        m_preserveChartAxes = false;
        m_hoverSampleIndexMap.clear();
        m_hoverLogicalSampleCount = 0;
        if (m_chartView) m_chartView->setHoverXValues({});
        m_replayChartBuiltTrailLength = 0;
        m_replayChartBuiltBucket = 0;
        m_axisX->setRange(0, 10);
        m_axisY->setRange(-1, 1);
        m_chart->setTitle(u"Flight data"_s);
        return;
    }

    const auto &samples = m_session->samples;
    const int n = static_cast<int>(samples.size());
    const int end = std::min(trailLength, n);
    if (end <= 0) {
        m_hoverSampleIndexMap.clear();
        m_hoverLogicalSampleCount = 0;
        if (m_chartView) m_chartView->setHoverXValues({});
        m_replayChartBuiltTrailLength = 0;
        m_replayChartBuiltBucket = 0;
        return;
    }

    m_replayChartBuiltTrailLength = end;
    if (m_preview) {
        int begin = 0;
        int rangeEnd = end;
        if (m_preserveChartAxes && m_axisX) {
            const auto range = m_preview->visibleRange(m_axisX->min(), m_axisX->max(), end);
            begin = range.begin;
            rangeEnd = range.end;
        }
        const int budget = chartPointBudget();
        const auto indices = m_preview->chartIndices(*m_session, begin, rangeEnd, budget, m_metricEnabled);
        m_replayChartBuiltBucket = m_preview->replayBucket(end, budget);
        buildChartFromSampleIndices(samples, indices, n);
    } else {
        m_replayChartBuiltBucket = end;
        buildChartFromSamples(samples, end);
    }
}

void DashboardPage::onSessionReset() {
    m_preserveChartAxes = false;
    if (m_renderSchedulerTimer) m_renderSchedulerTimer->stop();
    m_liveChartDirty = false;
    m_replayChartDirty = false;
    m_hoverSampleIndexMap.clear();
    m_hoverLogicalSampleCount = 0;
    m_replayChartBuiltTrailLength = -1;
    m_replayChartBuiltBucket = -1;
    m_liveSamples.clear();
    m_liveChartScratch.clear();
    for (int mi = 0; mi < kMetricCount; ++mi) {
        if (m_lineSeries[static_cast<std::size_t>(mi)]) {
            m_lineSeries[static_cast<std::size_t>(mi)]->clear();
            m_lineSeries[static_cast<std::size_t>(mi)]->setVisible(false);
        }
    }
    if (m_chartView) {
        m_chartView->setHoverXValues({});
        m_chartView->invalidateHoverSeriesCache();
    }
    if (m_axisX) {
        m_axisX->setRange(0, 10);
    }
    if (m_axisY) {
        m_axisY->setRange(-1, 1);
    }
    if (m_chart) {
        m_chart->setTitle(u"Flight data"_s);
    }
    if (m_replayBar) m_replayBar->setLiveSampleCount(0);
    if (m_model && !m_model->replayMode()) m_liveChartDirty = true;
    if (m_viewStack && m_liveSamples.empty() && (!m_session || m_session->samples.empty())) {
        m_viewStack->setCurrentIndex(2);
    }
    scheduleRenderPass();
}

/**
 * Rebuilds all chart series from m_liveSamples (live telemetry mode).
 *
 * Same decimation and normalization logic as rebuildReplayCharts(). Called by
 * the shared scheduler at no more than 20 Hz.
 */
void DashboardPage::rebuildLiveSeriesFromHistory() {
    if (!m_axisX || !m_axisY) {
        return;
    }

    if (m_chartView) m_chartView->invalidateHoverSeriesCache();

    if (m_liveSamples.empty()) {
        for (QLineSeries *series : m_lineSeries) {
            if (series) {
                series->clear();
                series->setVisible(false);
            }
        }
        m_preserveChartAxes = false;
        m_hoverSampleIndexMap.clear();
        m_hoverLogicalSampleCount = 0;
        if (m_chartView) m_chartView->setHoverXValues({});
        m_axisX->setRange(0, 10);
        m_axisY->setRange(-1, 1);
        m_chart->setTitle(u"Flight data"_s);
        return;
    }

    m_liveChartScratch.assign(m_liveSamples.begin(), m_liveSamples.end());
    buildChartFromSamples(m_liveChartScratch, static_cast<int>(m_liveChartScratch.size()));
}

int DashboardPage::chartPointBudget() const {
    const int width = m_chartView ? m_chartView->width() : 0;
    return std::min(kMaxChartDisplayPoints, std::max(1000, width * 2));
}

void DashboardPage::buildChartFromSamples(const std::vector<FlightSample> &samples, int end) {
    if (samples.empty() || end <= 0) {
        return;
    }
    const int boundedSampleCount = static_cast<int>(std::min<std::size_t>(
        samples.size(), static_cast<std::size_t>(std::numeric_limits<int>::max())));
    const int boundedEnd = std::clamp(end, 0, boundedSampleCount);
    if (boundedEnd <= 0) {
        return;
    }
    const long tRef = samples.front().timestamp;
    const bool sessionElapsed = useSessionElapsedTimeAxis(
        samples.front().timestamp,
        samples[static_cast<std::size_t>(boundedEnd - 1)].timestamp);
    const std::vector<int> plotIdx = lttbIndicesForChartDisplay(
        samples, boundedEnd, chartPointBudget(), m_metricEnabled, tRef, sessionElapsed);
    buildChartFromSampleIndices(samples, plotIdx, boundedEnd);
}

void DashboardPage::buildChartFromSampleIndices(
    const std::vector<FlightSample> &samples,
    const std::vector<int> &sampleIndices,
    int logicalSampleCount) {
    if (samples.empty()) {
        return;
    }

    const bool usePreviewTime = m_model && m_model->replayMode() && m_preview
        && m_preview->displaySeconds().size() == samples.size();
    const long tRef = samples.front().timestamp;
    const bool sessionElapsed = useSessionElapsedTimeAxis(samples.front().timestamp, samples.back().timestamp);
    auto xForIndex = [&](int sampleIndex) {
        if (usePreviewTime) {
            return m_preview->displaySecondAt(sampleIndex);
        }
        return chartXSeconds(tRef, samples[static_cast<std::size_t>(sampleIndex)].timestamp, sessionElapsed);
    };

    struct PlotSample {
        double x = 0.0;
        int sampleIndex = 0;
    };
    std::vector<PlotSample> plotSamples;
    plotSamples.reserve(sampleIndices.size());
    for (const int sampleIndex : sampleIndices) {
        if (sampleIndex < 0
            || static_cast<std::size_t>(sampleIndex) >= samples.size()) {
            continue;
        }
        const double x = xForIndex(sampleIndex);
        if (std::isfinite(x)) {
            plotSamples.push_back({x, sampleIndex});
        }
    }
    std::stable_sort(
        plotSamples.begin(), plotSamples.end(),
        [](const PlotSample &left, const PlotSample &right) {
            return left.x < right.x;
        });

    m_axisX->setTitleText(usePreviewTime || sessionElapsed
        ? u"Session time (s)"_s
        : u"Flight time (s)"_s);
    m_hoverSampleIndexMap.clear();
    m_hoverSampleIndexMap.reserve(plotSamples.size());
    QVector<double> hoverXValues;
    hoverXValues.reserve(static_cast<qsizetype>(std::min<std::size_t>(
        plotSamples.size(), static_cast<std::size_t>(std::numeric_limits<qsizetype>::max()))));
    for (const PlotSample &plotSample : plotSamples) {
        m_hoverSampleIndexMap.push_back(plotSample.sampleIndex);
        hoverXValues.append(plotSample.x);
    }
    m_hoverLogicalSampleCount = std::max(0, logicalSampleCount);
    if (m_chartView) m_chartView->setHoverXValues(std::move(hoverXValues));

    if (plotSamples.empty()) {
        for (QLineSeries *series : m_lineSeries) {
            if (series) {
                series->clear();
                series->setVisible(false);
            }
        }
        if (!m_preserveChartAxes) {
            m_axisX->setRange(0.0, 10.0);
            m_axisY->setRange(-1.0, 1.0);
        }
        if (m_axisY2) m_axisY2->setVisible(false);
        if (m_chartView) m_chartView->invalidateHoverSeriesCache();
        return;
    }

    const int nEn = countEnabledMetrics();
    const double xMin = plotSamples.front().x;
    const double xMax = plotSamples.back().x;

    std::array<double, kMetricCount> yMin{};
    std::array<double, kMetricCount> yMax{};
    for (int mi = 0; mi < kMetricCount; ++mi) {
        yMin[static_cast<std::size_t>(mi)] = std::numeric_limits<double>::infinity();
        yMax[static_cast<std::size_t>(mi)] = -std::numeric_limits<double>::infinity();
    }
    for (const PlotSample &plotSample : plotSamples) {
        const FlightSample &s = samples[static_cast<std::size_t>(plotSample.sampleIndex)];
        for (int mi = 0; mi < kMetricCount; ++mi) {
            if (!m_metricEnabled[static_cast<std::size_t>(mi)]) {
                continue;
            }
            const double y = metricDisplayValue(
                mi, sampleValueForMetric(s, mi), m_imperialUnits);
            if (!std::isfinite(y)) {
                continue;
            }
            auto &lo = yMin[static_cast<std::size_t>(mi)];
            auto &hi = yMax[static_cast<std::size_t>(mi)];
            lo = std::min(lo, y);
            hi = std::max(hi, y);
        }
    }

    int onlyMi = -1;
    int dualMi[2] = {-1, -1};
    if (nEn == 1) {
        for (int mi = 0; mi < kMetricCount; ++mi) {
            if (m_metricEnabled[static_cast<std::size_t>(mi)]) { onlyMi = mi; break; }
        }
    } else if (nEn == 2) {
        int idx = 0;
        for (int mi = 0; mi < kMetricCount && idx < 2; ++mi) {
            if (m_metricEnabled[static_cast<std::size_t>(mi)]) dualMi[idx++] = mi;
        }
    }

    const qsizetype plotN = static_cast<qsizetype>(std::min<std::size_t>(
        plotSamples.size(), static_cast<std::size_t>(std::numeric_limits<qsizetype>::max())));
    for (int mi = 0; mi < kMetricCount; ++mi) {
        auto *series = m_lineSeries[static_cast<std::size_t>(mi)];
        if (!series) {
            continue;
        }
        const bool enabled = m_metricEnabled[static_cast<std::size_t>(mi)];
        if (!enabled) {
            series->clear();
            series->setVisible(false);
            series->setPointLabelsVisible(false);
            series->setPointsVisible(false);
            continue;
        }

        const double lo = yMin[static_cast<std::size_t>(mi)];
        const double hi = yMax[static_cast<std::size_t>(mi)];
        const bool hasFiniteRange = std::isfinite(lo) && std::isfinite(hi);
        const double scale = hasFiniteRange
            ? std::max({std::abs(lo), std::abs(hi), 1.0})
            : 1.0;
        const double scaledLo = lo / scale;
        const double scaledHi = hi / scale;
        const double scaledSpan = scaledHi - scaledLo;

        QList<QPointF> pts;
        pts.reserve(plotN);
        for (const PlotSample &plotSample : plotSamples) {
            const FlightSample &s = samples[static_cast<std::size_t>(plotSample.sampleIndex)];
            double y = metricDisplayValue(
                mi, sampleValueForMetric(s, mi), m_imperialUnits);
            if (!std::isfinite(y) || !hasFiniteRange) {
                continue;
            }
            if (nEn > 2) {
                y = std::abs(scaledSpan) > 1e-15
                    ? ((y / scale) - scaledLo) / scaledSpan
                    : 0.5;
            }
            if (std::isfinite(y)) {
                pts.append(QPointF(plotSample.x, y));
            }
        }
        series->replace(pts);
        series->setVisible(!pts.isEmpty());
        if (!pts.isEmpty()) {
            applySeriesPointDisplay(series, series->count(), nEn);
            series->setName(metricDisplayTitle(mi, m_imperialUnits));
        }
    }

    if (m_axisY2) m_axisY2->setVisible(false);
    if (nEn == 2 && dualMi[0] >= 0 && dualMi[1] >= 0) {
        if (m_axisY2) m_axisY2->setVisible(true);
    }
    for (int mi = 0; mi < kMetricCount; ++mi) {
        auto *s = m_lineSeries[static_cast<std::size_t>(mi)];
        if (!s) continue;
        const bool isSecondDual = (nEn == 2 && mi == dualMi[1]);
        const auto attached = s->attachedAxes();
        if (isSecondDual) {
            if (attached.contains(m_axisY)) s->detachAxis(m_axisY);
            if (!s->attachedAxes().contains(m_axisY2)) s->attachAxis(m_axisY2);
        } else {
            if (attached.contains(m_axisY2)) s->detachAxis(m_axisY2);
            if (!s->attachedAxes().contains(m_axisY)) s->attachAxis(m_axisY);
        }
    }

    const double rawSpanX = xMax - xMin;
    const double spanX = std::isfinite(rawSpanX) ? std::max(rawSpanX, 1e-9) : 1.0;
    const double xPad = std::max(spanX * 0.02, 0.05);

    const auto applyFiniteAxisRange = [&](QValueAxis *axis, int metricIndex) {
        if (!axis || metricIndex < 0 || metricIndex >= kMetricCount) {
            return;
        }
        const double rangeLow = yMin[static_cast<std::size_t>(metricIndex)];
        const double rangeHigh = yMax[static_cast<std::size_t>(metricIndex)];
        if (!std::isfinite(rangeLow) || !std::isfinite(rangeHigh)) {
            axis->setRange(-1.0, 1.0);
            return;
        }
        const double magnitude = std::max({std::abs(rangeLow), std::abs(rangeHigh), 1.0});
        const double scaledSpan = (rangeHigh / magnitude) - (rangeLow / magnitude);
        const double padding = std::max(scaledSpan * magnitude * 0.08, magnitude * 1e-6);
        const double axisLow = rangeLow - padding;
        const double axisHigh = rangeHigh + padding;
        if (std::isfinite(axisLow) && std::isfinite(axisHigh) && axisLow < axisHigh) {
            axis->setRange(axisLow, axisHigh);
        } else {
            axis->setRange(-1.0, 1.0);
        }
    };

    if (nEn == 1 && onlyMi >= 0) {
        m_axisY->setTitleText(metricDisplayUnitShort(onlyMi, m_imperialUnits));
        m_chart->setTitle(metricDisplayTitle(onlyMi, m_imperialUnits));
        if (!m_preserveChartAxes) {
            applyFiniteAxisRange(m_axisY, onlyMi);
        }
    } else if (nEn == 2 && dualMi[0] >= 0 && dualMi[1] >= 0) {
        m_axisY->setTitleText(metricDisplayUnitShort(dualMi[0], m_imperialUnits));
        if (m_axisY2) {
            m_axisY2->setTitleText(metricDisplayUnitShort(dualMi[1], m_imperialUnits));
        }
        m_chart->setTitle(
            QStringLiteral("%1 vs %2")
                .arg(metricDisplayTitle(dualMi[0], m_imperialUnits),
                     metricDisplayTitle(dualMi[1], m_imperialUnits)));
        if (!m_preserveChartAxes) {
            for (int d = 0; d < 2; ++d) {
                auto *ax = (d == 0) ? m_axisY : m_axisY2;
                if (!ax) continue;
                applyFiniteAxisRange(ax, dualMi[d]);
            }
        }
    } else if (nEn == 0) {
        m_axisY->setTitleText({});
        m_chart->setTitle(u"Flight data"_s);
        if (!m_preserveChartAxes) m_axisY->setRange(-1.0, 1.0);
    } else {
        m_axisY->setTitleText(u"Normalized"_s);
        m_chart->setTitle(u"Multi-trace overlay"_s);
        if (!m_preserveChartAxes) {
            m_axisY->setRange(-0.05, 1.05);
        }
    }
    if (!m_preserveChartAxes) {
        const double axisXMin = xMin - xPad;
        const double axisXMax = xMax + xPad;
        if (std::isfinite(axisXMin) && std::isfinite(axisXMax) && axisXMin < axisXMax) {
            m_axisX->setRange(axisXMin, axisXMax);
        } else {
            m_axisX->setRange(0.0, 10.0);
        }
    }

    if (m_tracesPanel) {
        std::array<bool, kMetricCount> hasData{};
        for (int i = 0; i < kMetricCount; ++i) {
            const bool enabled = m_metricEnabled[static_cast<std::size_t>(i)];
            hasData[static_cast<std::size_t>(i)] = !enabled
                || (m_lineSeries[static_cast<std::size_t>(i)]
                    && m_lineSeries[static_cast<std::size_t>(i)]->count() > 0);
        }
        m_tracesPanel->setMetricDataStates(hasData);
    }
    if (m_chartView) m_chartView->invalidateHoverSeriesCache();
}

void DashboardPage::scheduleLiveChartRebuild() {
    m_liveChartDirty = true;
    if (graphViewIsActive() && isVisible()) showChartLoadingIndicator();
    scheduleRenderPass();
}

void DashboardPage::zoomChartAxesAtCenter(bool zoomIn) {
    if (!m_axisX || !m_axisY) {
        return;
    }
    const double f = zoomIn ? 0.5 : 2.0;

    const double xMid = (m_axisX->min() + m_axisX->max()) * 0.5;
    const double hx = (m_axisX->max() - m_axisX->min()) * 0.5;
    m_axisX->setRange(xMid - hx * f, xMid + hx * f);

    const double yMid = (m_axisY->min() + m_axisY->max()) * 0.5;
    const double hy = (m_axisY->max() - m_axisY->min()) * 0.5;
    m_axisY->setRange(yMid - hy * f, yMid + hy * f);

    if (m_axisY2 && m_axisY2->isVisible()) {
        const double y2Mid = (m_axisY2->min() + m_axisY2->max()) * 0.5;
        const double hy2 = (m_axisY2->max() - m_axisY2->min()) * 0.5;
        m_axisY2->setRange(y2Mid - hy2 * f, y2Mid + hy2 * f);
    }

    m_preserveChartAxes = true;
}

void DashboardPage::onDisplayedSampleChanged(const FlightSample &sample) {
    if (!isVisible()) {
        return;
    }
    if (m_tracesPanel) m_tracesPanel->updateLiveValues(sample);
}

void DashboardPage::onLiveSamplesReceived(const QVector<FlightSample> &samples) {
    if (!m_model || m_model->replayMode()) {
        return;
    }

    for (const auto &sample : samples) {
        m_liveSamples.push_back(sample);
    }
    while (static_cast<int>(m_liveSamples.size()) > kMaxLiveBufferSamples) {
        m_liveSamples.pop_front();
    }
    if (m_replayBar) m_replayBar->setLiveSampleCount(static_cast<int>(m_liveSamples.size()));
    if (!samples.isEmpty()) {
        scheduleLiveChartRebuild();
    }
}

void DashboardPage::showChartLoadingIndicator() {
    if (m_chartLoadingBar) m_chartLoadingBar->setVisible(true);
}

void DashboardPage::hideChartLoadingIndicator() {
    if (m_chartLoadingBar) m_chartLoadingBar->setVisible(false);
}

void DashboardPage::addEventMarker(double timeSec, const QString &name) {
    const EventMarker marker{timeSec, name};
    m_eventMarkers.push_back(marker);

    if (!m_chart || !m_axisX || !m_axisY) {
        return;
    }

    auto *line = new QLineSeries();
    line->setName(marker.name);
    line->append(marker.timeSec, m_axisY->min());
    line->append(marker.timeSec, m_axisY->max());
    applyEventMarkerTheme(line);
    m_chart->addSeries(line);
    line->attachAxis(m_axisX);
    line->attachAxis(m_axisY);
    m_markerSeries.push_back(line);
    if (m_chartView) m_chartView->invalidateHoverSeriesCache();
}

void DashboardPage::clearEventMarkers() {
    for (auto *s : m_markerSeries) {
        if (m_chart) m_chart->removeSeries(s);
        delete s;
    }
    m_markerSeries.clear();
    m_eventMarkers.clear();
    if (m_chartView) m_chartView->invalidateHoverSeriesCache();
}

void DashboardPage::applyEventMarkerTheme(QLineSeries *series) const {
    if (!series) {
        return;
    }
    const QColor markerColor(Theme::kAccentLink());
    series->setPen(QPen(markerColor, 2, Qt::DashLine));
}

void DashboardPage::refreshEventMarkerTheme() {
    for (QLineSeries *series : m_markerSeries) {
        applyEventMarkerTheme(series);
    }
}

void DashboardPage::updateEventMarkerGeometry() {
    if (!m_axisY) {
        return;
    }

    const std::size_t markerCount = std::min(
        m_eventMarkers.size(), m_markerSeries.size());
    for (std::size_t index = 0; index < markerCount; ++index) {
        QLineSeries *series = m_markerSeries[index];
        if (!series) {
            continue;
        }
        const double timeSec = m_eventMarkers[index].timeSec;
        series->replace(QList<QPointF>{
            QPointF(timeSec, m_axisY->min()),
            QPointF(timeSec, m_axisY->max())});
    }
    if (markerCount > 0 && m_chartView) {
        m_chartView->invalidateHoverSeriesCache();
    }
}

void DashboardPage::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    if (m_model && m_tracesPanel) {
        m_tracesPanel->updateLiveValues(m_model->latestSample());
    }
    processScheduledUpdates(true);
}

void DashboardPage::paintEvent(QPaintEvent *event) {
    QPainter painter(this);
    painter.setClipRegion(event->region());

    painter.fillRect(rect(), QColor(Theme::kBgBase()));

    constexpr int dotSpacing = 28;
    constexpr qreal dotRadius = 1.5;
    const int offset = dotSpacing / 2;

    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(Theme::kBorderDefault()).lighter(120));

    const QRectF clip = event->region().boundingRect();
    const int xStart = std::max(offset, static_cast<int>((clip.left() - offset) / dotSpacing) * dotSpacing + offset);
    const int yStart = std::max(offset, static_cast<int>((clip.top() - offset) / dotSpacing) * dotSpacing + offset);
    const int xEnd = std::min(width(), static_cast<int>(clip.right()) + dotSpacing);
    const int yEnd = std::min(height(), static_cast<int>(clip.bottom()) + dotSpacing);

    for (int y = yStart; y < yEnd; y += dotSpacing)
        for (int x = xStart; x < xEnd; x += dotSpacing)
            painter.drawEllipse(QPointF(x, y), dotRadius, dotRadius);
}
