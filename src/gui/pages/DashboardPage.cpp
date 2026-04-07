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
 *  - Live mode  — samples arrive via onSampleUpdated() from the serial worker.
 *    They are stored in m_liveSamples (bounded to kMaxLiveBufferSamples) and
 *    chart redraws are coalesced by m_liveChartCoalesceTimer (50 ms).
 *  - Replay mode — a FlightSession is set via setReplaySession(); the trail
 *    length (number of samples to show) is updated via setReplayTrailLength()
 *    on every controller tick. Chart redraws are coalesced by
 *    m_replayChartCoalesceTimer (33 ms) so scrolling the scrubber stays smooth.
 *
 * When more than kMaxChartDisplayPoints samples are present the chart uses
 * uniform decimation (sampleIndicesForChartDisplay) to keep rendering fast.
 * m_hoverSampleIndexMap maps each decimated display-point index back to its
 * original logical sample index so hover readouts always show accurate values.
 */

#include "gui/pages/DashboardPage.h"

#include "domain/FlightSession.h"
#include "gui/FlightDataModel.h"
#include "gui/FlightReplayController.h"
#include "gui/widgets/MetricDefs.h"
#include "gui/widgets/ReplayBar.h"
#include "gui/widgets/StatTileWidget.h"
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
#include <QLabel>
#include <QList>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QPointF>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
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
            mMin[m] = std::min(mMin[m], v);
            mMax[m] = std::max(mMax[m], v);
        }
    }
    std::vector<double> mScale(nEmi);
    for (std::size_t m = 0; m < nEmi; ++m) {
        const double range = mMax[m] - mMin[m];
        mScale[m] = (range > 1e-15) ? (1.0 / range) : 1.0;
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
        for (int i = nbStart; i < nbEnd; ++i) {
            const FlightSample &s = samples[static_cast<std::size_t>(i)];
            avgX += chartXSeconds(tRef, s.timestamp, sessionElapsed);
            for (std::size_t m = 0; m < nEmi; ++m)
                avgY[m] += (sampleValueForMetric(s, emi[m]) - mMin[m]) * mScale[m];
        }
        avgX /= nbCount;
        for (auto &v : avgY) v /= nbCount;

        const double prevX = chartXSeconds(tRef, samples[static_cast<std::size_t>(prevSelected)].timestamp, sessionElapsed);

        int bestIdx = bStart;
        double bestArea = -1.0;
        for (int i = bStart; i < bEnd; ++i) {
            const FlightSample &s = samples[static_cast<std::size_t>(i)];
            const double curX = chartXSeconds(tRef, s.timestamp, sessionElapsed);
            double maxArea = 0.0;
            for (std::size_t m = 0; m < nEmi; ++m) {
                const double prevY = (sampleValueForMetric(samples[static_cast<std::size_t>(prevSelected)], emi[m]) - mMin[m]) * mScale[m];
                const double curY  = (sampleValueForMetric(s, emi[m]) - mMin[m]) * mScale[m];
                const double area  = std::abs(
                    (prevX - avgX) * (curY - prevY)
                    - (prevX - curX) * (avgY[m] - prevY));
                maxArea = std::max(maxArea, area);
            }
            if (maxArea > bestArea) {
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
    setObjectName(u"dashboardPage"_s);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAutoFillBackground(false);
    setStyleSheet(uR"(
        #dashboardPage {
            background-color: transparent;
            color: #e8e8e8;
            font-family: "Red Hat Mono", "Courier New", "Roboto Mono", monospace;
        }
        #dashboardPage QWidget {
            font-family: "Red Hat Mono", "Courier New", "Roboto Mono", monospace;
        }
        QFrame#chartFrame {
            background-color: rgba(21, 22, 25, 0.80);
            border: 1px solid #3b3b45;
            border-radius: 8px;
            padding: 0px;
        }
        #telemetryChartView {
            border: none;
            padding: 0px;
            margin: 0px;
            background-color: transparent;
        }
        QToolButton#chartToggleBtn {
            border: 1px solid #5a5d68;
            border-radius: 4px;
            padding: 4px 10px;
            min-height: 28px;
            background-color: #2a2d34;
            color: #b8c4d0;
            font-size: 11px;
        }
        QToolButton#chartToggleBtn:hover {
            background-color: #343842;
            border-color: #6a6e78;
            color: #e8ecf0;
        }
        QToolButton#chartToggleBtn:checked {
            background-color: #2d3d52;
            border-color: #5a8ac0;
            color: #f0f4f8;
        }
        QToolButton#chartToggleBtn:checked:hover {
            background-color: #354a62;
            border-color: #6a9ad0;
        }
        QPushButton#chartZoomBtn {
            min-width: 28px;
            max-width: 28px;
            min-height: 28px;
            max-height: 28px;
            padding: 0px;
            font-weight: 700;
            font-size: 16px;
            border: 1px solid #6a6a6a;
            border-radius: 4px;
            background-color: #3d3f47;
            color: #f0f0f0;
        }
        QPushButton#chartZoomBtn:hover {
            background-color: #4a4d56;
            border-color: #7a7a82;
        }
        QPushButton#chartZoomBtn:pressed {
            background-color: #2e3038;
        }
        QDoubleSpinBox {
            background-color: #1a1a1a;
            color: #f5f5f5;
            border: 1px solid #4d4d4d;
            border-radius: 4px;
            padding: 4px 8px;
            min-height: 22px;
        }
        QSlider::groove:horizontal {
            height: 6px;
            background: #2a2c32;
            border-radius: 3px;
        }
        QSlider::handle:horizontal {
            width: 14px;
            margin: -5px 0;
            background: #6a6e78;
            border: 1px solid #8a8e98;
            border-radius: 4px;
        }
        QPushButton#chartToolbarBtn {
            border: 1px solid #6a6a6a;
            border-radius: 4px;
            padding: 4px 10px;
            min-height: 28px;
            background-color: #3d3f47;
            color: #f0f0f0;
            font-size: 11px;
        }
        QPushButton#chartToolbarBtn:hover {
            background-color: #4a4d56;
            border-color: #7a7a82;
        }
        QPushButton#chartToolbarBtn:pressed {
            background-color: #2e3038;
        }
        QToolButton#chartHelpBtn {
            font-weight: 700;
            font-size: 13px;
            min-width: 28px;
            max-width: 28px;
            min-height: 28px;
            max-height: 28px;
            border: 1px solid #5a5d68;
            border-radius: 14px;
            background: #2a2d34;
            color: #7a8796;
            padding: 0px;
        }
        QToolButton#chartHelpBtn:hover {
            color: #a8b4c0;
            border-color: #8090a0;
            background: #353840;
        }
        QLabel#chartHoverReadout {
            color: #c8d4e0;
            font-size: 11px;
            padding: 4px 6px;
            margin: 0px;
            background-color: rgba(8, 9, 12, 0.92);
            border: 1px solid #3b3b45;
            border-radius: 4px;
        }
        QLabel#chartStatsLabel {
            color: #8fa0b0;
            font-size: 11px;
            margin: 0px;
            padding: 0px;
        }
        QLabel#chartInteractionHint {
            color: #7a8796;
            font-size: 10px;
            margin: 0px;
            padding: 0px 2px;
        }
    )"_s);

    // ── Root layout ───────────────────────────────────────────────────────────
    // The page has two rows: the splitter (traces + chart, stretchy) and the
    // replay/transport bar (fixed height, docked at the bottom).
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setSpacing(8);
    rootLayout->setContentsMargins(8, 8, 8, 8);

    // ── Replay transport bar ─────────────────────────────────────────────────
    m_replayBar = new ReplayBar(m_replay, m_model, this);
    if (m_replay) {
        connect(m_replay, &FlightReplayController::playbackPaused,   this, &DashboardPage::flushReplayChartRebuild);
        connect(m_replay, &FlightReplayController::playbackStopped,  this, &DashboardPage::flushReplayChartRebuild);
        connect(m_replay, &FlightReplayController::playbackFinished, this, &DashboardPage::flushReplayChartRebuild);
        connect(m_replay, &FlightReplayController::positionChanged,  this, [this](int len) {
            applyReplayControllerPosition(len);
        });
    }
    connect(m_replayBar, &ReplayBar::trailLengthChanged, this, [this](int len) {
        applyReplayControllerPosition(len);
    });

    // ── Stat tiles (top row) ──────────────────────────────────────────────────
    auto *statsRowWidget = new QWidget(this);
    auto *statsLayout = new QHBoxLayout(statsRowWidget);
    statsLayout->setContentsMargins(0, 0, 0, 0);
    statsLayout->setSpacing(12);
    m_accelTile = new StatTileWidget(u"|ACCEL|"_s, u"—"_s, statsRowWidget);
    m_altTile   = new StatTileWidget(u"ALTITUDE"_s, u"—"_s, statsRowWidget);
    m_tempTile  = new StatTileWidget(u"TEMP"_s,     u"—"_s, statsRowWidget);
    m_pressTile = new StatTileWidget(u"PRESSURE"_s, u"—"_s, statsRowWidget);
    statsLayout->addWidget(m_accelTile, 1);
    statsLayout->addWidget(m_altTile,   1);
    statsLayout->addWidget(m_tempTile,  1);
    statsLayout->addWidget(m_pressTile, 1);
    rootLayout->addWidget(statsRowWidget);

    // ── Traces panel (left side of splitter) ─────────────────────────────────
    m_tracesPanel = new TracesPanel(this);
    m_metricEnabled = m_tracesPanel->enabledMetrics();
    connect(m_tracesPanel, &TracesPanel::enabledMetricsChanged, this,
            [this](const std::array<bool, 9> &enabled) {
        m_metricEnabled = enabled;
        m_preserveChartAxes = false;
        refreshAllSeriesFromData();
        updateChartStatsLabel();
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
    chartHeaderLay->setContentsMargins(6, 6, 6, 4);
    chartHeaderLay->setSpacing(4);

    auto *chartToolbar = new QHBoxLayout();
    chartToolbar->setSpacing(6);

    m_zoomOutBtn = new QPushButton(u"−"_s, chartHeader);
    m_zoomOutBtn->setObjectName(u"chartZoomBtn"_s);
    m_zoomOutBtn->setToolTip(u"Zoom out ×2  [− key · scroll down]"_s);
    chartToolbar->addWidget(m_zoomOutBtn);

    m_zoomInBtn = new QPushButton(u"+"_s, chartHeader);
    m_zoomInBtn->setObjectName(u"chartZoomBtn"_s);
    m_zoomInBtn->setToolTip(u"Zoom in ×2  [+ key · scroll up]"_s);
    chartToolbar->addWidget(m_zoomInBtn);

    m_zoomResetBtn = new QPushButton(u"Fit"_s, chartHeader);
    m_zoomResetBtn->setObjectName(u"chartToolbarBtn"_s);
    m_zoomResetBtn->setToolTip(u"Fit chart to full data range  [F key]"_s);
    chartToolbar->addWidget(m_zoomResetBtn);
    connect(m_zoomResetBtn, &QPushButton::clicked, this, &DashboardPage::onResetChartZoom);

    auto *toolbarSep = new QFrame(chartHeader);
    toolbarSep->setFixedSize(1, 22);
    toolbarSep->setStyleSheet(u"background-color: #4a4d56; border: none;"_s);
    chartToolbar->addWidget(toolbarSep);

    m_showMarkersToggle = new QToolButton(chartHeader);
    m_showMarkersToggle->setObjectName(u"chartToggleBtn"_s);
    m_showMarkersToggle->setText(u"Markers"_s);
    m_showMarkersToggle->setCheckable(true);
    m_showMarkersToggle->setChecked(true);
    m_showMarkersToggle->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_showMarkersToggle->setToolTip(
        u"Draw a dot on each sample — auto-disabled above 400 pts/trace.  [M key]"_s);
    chartToolbar->addWidget(m_showMarkersToggle);

    m_showPointValuesToggle = new QToolButton(chartHeader);
    m_showPointValuesToggle->setObjectName(u"chartToggleBtn"_s);
    m_showPointValuesToggle->setText(u"Values"_s);
    m_showPointValuesToggle->setCheckable(true);
    m_showPointValuesToggle->setChecked(false);
    m_showPointValuesToggle->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_showPointValuesToggle->setToolTip(
        u"Show Y value next to each point — single trace only, max 100 points.  [V key]"_s);
    chartToolbar->addWidget(m_showPointValuesToggle);

    connect(m_showMarkersToggle, &QToolButton::toggled, this, &DashboardPage::onChartVisualOptionsToggled);
    connect(m_showPointValuesToggle, &QToolButton::toggled, this, &DashboardPage::onChartVisualOptionsToggled);

    chartToolbar->addStretch(1);

    m_followToggle = new QToolButton(chartHeader);
    m_followToggle->setObjectName(u"chartToggleBtn"_s);
    m_followToggle->setText(u"Follow"_s);
    m_followToggle->setCheckable(true);
    m_followToggle->setChecked(true);
    m_followToggle->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_followToggle->setToolTip(
        u"Auto-fit chart axes to data on each update.\n"
        u"Turn off to preserve your zoom level during replay.\n"
        u"Pan or zoom the chart to disable automatically."_s);
    chartToolbar->addWidget(m_followToggle);
    connect(m_followToggle, &QToolButton::toggled, this, [this](bool checked) {
        if (checked) {
            m_preserveChartAxes = false;
            refreshAllSeriesFromData();
        } else {
            m_preserveChartAxes = true;
        }
    });

    auto *copyChartBtn = new QPushButton(u"Copy"_s, chartHeader);
    copyChartBtn->setObjectName(u"chartToolbarBtn"_s);
    copyChartBtn->setToolTip(u"Copy chart image to clipboard"_s);
    chartToolbar->addWidget(copyChartBtn);
    connect(copyChartBtn, &QPushButton::clicked, this, [this]() {
        if (m_chartView) {
            QPixmap pixmap = m_chartView->grab();
            QApplication::clipboard()->setPixmap(pixmap);
        }
    });

    // "?" help button at the far right of the toolbar — interaction hints as tooltip.
    auto *chartHelpBtn = new QToolButton(chartHeader);
    chartHelpBtn->setObjectName(u"chartHelpBtn"_s);
    chartHelpBtn->setText(u"?"_s);
    chartHelpBtn->setToolTip(
        u"Chart controls:\n"
        u"  Drag: pan  ·  Ctrl+drag: zoom rectangle\n"
        u"  Scroll wheel / trackpad: zoom at pointer\n"
        u"  ⌘/Ctrl + two-finger swipe: zoom at pointer\n"
        u"  − / + keys: zoom out / in  ·  F: fit to data\n"
        u"  M: toggle markers  ·  V: toggle point values\n"
        u"  Hover cursor: floating readout + crosshair + snap dot"_s);
    chartToolbar->addWidget(chartHelpBtn);

    chartHeaderLay->addLayout(chartToolbar);

    m_chartStatsLabel = new QLabel(chartHeader);
    m_chartStatsLabel->setObjectName(u"chartStatsLabel"_s);
    chartHeaderLay->addWidget(m_chartStatsLabel);

    chartFrameLayout->addWidget(chartHeader, 0);

    m_chart = new QChart();
    m_chart->setBackgroundRoundness(0);

    for (int i = 0; i < kMetricCount; ++i) {
        auto *series = new QLineSeries();
        series->setName(metricTitle(i));
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

    for (int i = 0; i < kMetricCount; ++i) {
        m_lineSeries[static_cast<std::size_t>(i)]->attachAxis(m_axisX);
        m_lineSeries[static_cast<std::size_t>(i)]->attachAxis(m_axisY);
    }

    applyChartTheme();

    connect(m_zoomInBtn, &QPushButton::clicked, this, [this]() {
        zoomChartAxesAtCenter(true);
    });
    connect(m_zoomOutBtn, &QPushButton::clicked, this, [this]() {
        zoomChartAxesAtCenter(false);
    });

    // Keyboard shortcuts for chart interactions.
    auto *scZoomIn  = new QShortcut(QKeySequence(Qt::Key_Plus),  this);
    auto *scZoomIn2 = new QShortcut(QKeySequence(Qt::Key_Equal), this);
    auto *scZoomOut = new QShortcut(QKeySequence(Qt::Key_Minus), this);
    auto *scFit     = new QShortcut(QKeySequence(Qt::Key_F),     this);
    auto *scMarkers = new QShortcut(QKeySequence(Qt::Key_M),     this);
    auto *scValues  = new QShortcut(QKeySequence(Qt::Key_V),     this);
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

    // Live-mode coalesce timer: samples may arrive faster than the display can
    // keep up.  scheduleLiveChartRebuild() restarts this single-shot timer on
    // every incoming sample; the chart only redraws when the timer fires (i.e.
    // after a 50 ms quiet period), keeping the GUI thread free.
    m_liveChartCoalesceTimer = new QTimer(this);
    m_liveChartCoalesceTimer->setSingleShot(true);
    connect(m_liveChartCoalesceTimer, &QTimer::timeout, this, [this]() {
        rebuildLiveSeriesFromHistory();
        updateChartStatsLabel();
    });

    // Replay coalesce timer: scrubbing the timeline fires setReplayTrailLength()
    // at high frequency.  At ~33 ms (≈ 30 fps) redraws are fast enough to feel
    // live but cheap enough not to saturate Qt Charts.  flushReplayChartRebuild()
    // bypasses the timer for immediate updates on pause/stop.
    m_replayChartCoalesceTimer = new QTimer(this);
    m_replayChartCoalesceTimer->setSingleShot(true);
    m_replayChartCoalesceTimer->setInterval(33);
    connect(m_replayChartCoalesceTimer, &QTimer::timeout, this, [this]() {
        if (m_model && m_model->replayMode() && m_session && !m_session->samples.empty()) {
            rebuildReplayCharts(m_lastReplayTrailLength);
        }
    });

    auto *tcv = new TelemetryChartView(m_chart, chartFrame);
    tcv->setObjectName(u"telemetryChartView"_s);
    tcv->onUserAdjustedAxes = [this]() {
        m_preserveChartAxes = true;
        if (m_followToggle) m_followToggle->setChecked(false);
    };
    m_chartView = tcv;
    tcv->setChart(m_chart);
    tcv->hoverDetail = [this](double tSec, int sampleIndex1Based, int /*totalSamples*/) {
        return formatMultiMetricHover(tSec, sampleIndex1Based);
    };
    // hoverReadout text is displayed by TelemetryChartView's own floating overlay;
    // no secondary label is needed in DashboardPage.
    tcv->hoverReadout = nullptr;

    chartFrameLayout->addWidget(m_chartView, 1);

    chartColumn->addWidget(chartFrame, 1);

    // ── Splitter — traces panel | chart host ─────────────────────────────────
    // State (column widths) is persisted in QSettings so the user's layout
    // survives app restarts.  childrenCollapsible = false prevents the user from
    // accidentally shrinking either pane to zero.
    auto *dashSplitter = new QSplitter(Qt::Horizontal, this);
    dashSplitter->setChildrenCollapsible(false);
    dashSplitter->addWidget(m_tracesPanel);
    dashSplitter->addWidget(chartHost);
    dashSplitter->setStretchFactor(0, 0);
    dashSplitter->setStretchFactor(1, 1);
    {
        QSettings dashSettings(u"CosmoSoft"_s, u"cosmo-soft"_s);
        const QByteArray st = dashSettings.value(u"ui/dashboardSplitterState"_s).toByteArray();
        if (!st.isEmpty()) {
            dashSplitter->restoreState(st);
        } else {
            dashSplitter->setSizes({168, 1000});
        }
    }
    connect(dashSplitter, &QSplitter::splitterMoved, this, [dashSplitter]() {
        QSettings s(u"CosmoSoft"_s, u"cosmo-soft"_s);
        s.setValue(u"ui/dashboardSplitterState"_s, dashSplitter->saveState());
    });
    rootLayout->addWidget(dashSplitter, 1);
    rootLayout->addWidget(m_replayBar);

    if (m_model) {
        connect(m_model, &FlightDataModel::sampleUpdated, this, &DashboardPage::onSampleUpdated);
        connect(m_model, &FlightDataModel::sessionReset, this, &DashboardPage::onSessionReset);
    }

    refreshAllSeriesFromData();
    updateChartStatsLabel();
}

void DashboardPage::applyChartTheme() {
    if (!m_chart || !m_axisX || !m_axisY) {
        return;
    }
    const QColor bg(13, 15, 20);
    const QColor plotBg(10, 12, 16);
    const QColor labelCol(200, 208, 218);
    const QColor gridCol(255, 255, 255, 28);
    const QPen gridPen(gridCol, 1, Qt::DotLine);

    m_chart->setBackgroundBrush(bg);
    m_chart->setBackgroundPen(Qt::NoPen);
    m_chart->setPlotAreaBackgroundBrush(plotBg);
    m_chart->setPlotAreaBackgroundVisible(true);

    QFont axisFont(u"Red Hat Mono"_s, 9);
    QFont titleFont(u"Red Hat Mono"_s, 12);
    titleFont.setBold(true);

    m_chart->setTitleFont(titleFont);
    m_chart->setTitleBrush(labelCol);

    for (auto *ax : {m_axisX, m_axisY}) {
        ax->setLabelsFont(axisFont);
        ax->setTitleFont(axisFont);
        ax->setLabelsColor(labelCol);
        ax->setTitleBrush(QColor(160, 172, 188));
        ax->setLinePenColor(QColor(120, 128, 140));
        ax->setGridLinePen(gridPen);
        ax->setMinorGridLineVisible(false);
    }

    m_chart->legend()->setVisible(true);
    m_chart->legend()->setAlignment(Qt::AlignBottom);
    m_chart->legend()->setLabelColor(labelCol);
    m_chart->legend()->setBackgroundVisible(true);
    m_chart->legend()->setBrush(QColor(0, 0, 0, 140));
    m_chart->legend()->setPen(QPen(QColor(60, 64, 72), 1));
    m_chart->setMargins(QMargins(2, 2, 2, 6));
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
        if (s && m_metricEnabled[static_cast<std::size_t>(mi)])
            applySeriesPointDisplay(s, s->count(), nEn);
    }
    updateChartStatsLabel();
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
        series->setPointLabelsColor(QColor(210, 218, 230));
    }

    series->setUseOpenGL(nEnabledMetrics == 1 && pointCount > 800 && !showVertices);
}

void DashboardPage::updateChartStatsLabel() {
    if (!m_chartStatsLabel) {
        return;
    }
    int maxPts = 0;
    int nTr = 0;
    for (int i = 0; i < kMetricCount; ++i) {
        if (!m_metricEnabled[static_cast<std::size_t>(i)]) {
            continue;
        }
        ++nTr;
        auto *s = m_lineSeries[static_cast<std::size_t>(i)];
        if (s) {
            maxPts = std::max(maxPts, s->count());
        }
    }
    const QString mode = (m_model && m_model->replayMode()) ? u"Replay"_s : u"Live"_s;
    const QString norm = (nTr > 1) ? u" · normalized Y overlay"_s : u""_s;
    QString opts;
    if (m_showMarkersToggle) {
        opts += m_showMarkersToggle->isChecked() ? u" · markers ≤400"_s : u" · markers off"_s;
    }
    if (m_showPointValuesToggle && m_showPointValuesToggle->isChecked()) {
        opts += u" · point labels ≤100 (1 trace)"_s;
    }
    if (!m_hoverSampleIndexMap.empty() && m_hoverLogicalSampleCount > static_cast<int>(m_hoverSampleIndexMap.size())) {
        opts += u" · LTTB decimation"_s;
    }
    m_chartStatsLabel->setText(
        QStringLiteral("%1 · %2 traces · up to %3 points%4%5")
            .arg(mode)
            .arg(nTr)
            .arg(maxPts)
            .arg(norm)
            .arg(opts));
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
        if (si >= 0 && si < static_cast<int>(m_liveSamples.size())) {
            sp = &m_liveSamples[static_cast<std::size_t>(si)];
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
        const double v = sampleValueForMetric(*sp, mi);
        lines << QStringLiteral("  • %1: %2 %3")
                     .arg(metricQuantityName(mi), formatMetricValuePretty(mi, v), metricAxisUnitShort(mi));
    }
    return lines.join(u"\n"_s);
}

void DashboardPage::setReplaySession(const FlightSession *session) {
    m_preserveChartAxes = false;
    if (m_replayChartCoalesceTimer) m_replayChartCoalesceTimer->stop();
    if (m_liveChartCoalesceTimer)   m_liveChartCoalesceTimer->stop();

    m_session = session;
    if (m_tracesPanel) {
        m_tracesPanel->setMetricsOffered(traceOfferMaskForSession(session));
    }
    const int n = session ? static_cast<int>(session->samples.size()) : 0;

    if (m_replayBar) m_replayBar->setSession(session);

    if (n > 0) {
        m_lastReplayTrailLength = n;
        rebuildReplayCharts(n);
        if (m_replay) m_replay->setPosition(n);
    } else {
        m_lastReplayTrailLength = 0;
        rebuildReplayCharts(0);
    }
    updateChartStatsLabel();
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
    if (m_replayBar) m_replayBar->setTrailLength(trailLength);
    if (m_replay && m_replay->isPlaying()) {
        scheduleReplayChartRebuild();
        return;
    }
    if (m_model && m_model->replayMode() && trailLength == m_replayChartBuiltTrailLength) {
        return;  // chart already correct; ReplayBar updates its own labels via positionChanged
    }
    rebuildReplayCharts(trailLength);
}

/** Restarts the 33 ms coalesce timer; the chart rebuilds once it fires. */
void DashboardPage::scheduleReplayChartRebuild() {
    if (m_replayChartCoalesceTimer) {
        m_replayChartCoalesceTimer->start();
    }
}

/**
 * Cancels any pending coalesced rebuild and forces an immediate one.
 *
 * Called on playback pause/stop so the chart snaps to the exact final position
 * without waiting for the timer to fire.
 */
void DashboardPage::flushReplayChartRebuild() {
    if (m_replayChartCoalesceTimer) {
        m_replayChartCoalesceTimer->stop();
    }
    if (m_model && m_model->replayMode()) {
        rebuildReplayCharts(m_lastReplayTrailLength);
    }
}

void DashboardPage::setReplayTrailLength(int trailLength) {
    applyReplayControllerPosition(trailLength);
}

void DashboardPage::refreshAllSeriesFromData() {
    m_preserveChartAxes = false;
    if (m_model && m_model->replayMode() && m_session && !m_session->samples.empty()) {
        rebuildReplayCharts(m_lastReplayTrailLength);
    } else {
        rebuildLiveSeriesFromHistory();
    }
    if (m_followToggle && !m_followToggle->isChecked())
        m_preserveChartAxes = true;
}

/**
 * Rebuilds all chart series from the first @p trailLength samples of m_session.
 *
 * Design notes:
 * - Decimation: if trailLength > kMaxChartDisplayPoints the samples are thinned
 *   to kMaxChartDisplayPoints uniformly spaced indices.  The mapping is stored in
 *   m_hoverSampleIndexMap for accurate hover readouts.
 * - Normalization: when more than one metric is enabled each series is scaled to
 *   [0, 1] using its own min/max so all traces fit the same Y axis.  A single
 *   enabled metric gets the raw SI axis instead.
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

    for (int mi = 0; mi < kMetricCount; ++mi) {
        if (m_lineSeries[static_cast<std::size_t>(mi)]) {
            m_lineSeries[static_cast<std::size_t>(mi)]->clear();
            m_lineSeries[static_cast<std::size_t>(mi)]->setVisible(false);
        }
    }

    if (!m_session || trailLength <= 0 || m_session->samples.empty()) {
        m_preserveChartAxes = false;
        m_hoverSampleIndexMap.clear();
        m_hoverLogicalSampleCount = 0;
        m_replayChartBuiltTrailLength = 0;
        m_axisX->setRange(0, 10);
        m_axisY->setRange(-1, 1);
        m_chart->setTitle(u"Flight data"_s);
        updateChartStatsLabel();
        return;
    }

    const auto &samples = m_session->samples;
    const int n = static_cast<int>(samples.size());
    const int end = std::min(trailLength, n);
    if (end <= 0) {
        m_hoverSampleIndexMap.clear();
        m_hoverLogicalSampleCount = 0;
        m_replayChartBuiltTrailLength = 0;
        updateChartStatsLabel();
        return;
    }

    const long tRef = samples.front().timestamp;
    const bool sessionElapsed = useSessionElapsedTimeAxis(samples.front().timestamp, samples.back().timestamp);
    m_axisX->setTitleText(sessionElapsed ? u"Session time (s)"_s : u"Flight time (s)"_s);
    const int nEn = countEnabledMetrics();

    const std::vector<int> plotIdx = lttbIndicesForChartDisplay(
        samples, end, kMaxChartDisplayPoints, m_metricEnabled, tRef, sessionElapsed);
    if (plotIdx.empty()) {
        m_replayChartBuiltTrailLength = end;
        updateChartStatsLabel();
        return;
    }
    m_hoverSampleIndexMap = plotIdx;
    m_hoverLogicalSampleCount = end;
    m_replayChartBuiltTrailLength = end;
    const int iFirst = plotIdx.front();
    const int iLast = plotIdx.back();
    double xMin = chartXSeconds(tRef, samples[static_cast<std::size_t>(iFirst)].timestamp, sessionElapsed);
    double xMax = chartXSeconds(tRef, samples[static_cast<std::size_t>(iLast)].timestamp, sessionElapsed);

    std::array<double, kMetricCount> yMin{};
    std::array<double, kMetricCount> yMax{};
    for (int mi = 0; mi < kMetricCount; ++mi) {
        yMin[static_cast<std::size_t>(mi)] = std::numeric_limits<double>::infinity();
        yMax[static_cast<std::size_t>(mi)] = -std::numeric_limits<double>::infinity();
    }

    for (int si : plotIdx) {
        const FlightSample &s = samples[static_cast<std::size_t>(si)];
        for (int mi = 0; mi < kMetricCount; ++mi) {
            const double y = sampleValueForMetric(s, mi);
            auto &lo = yMin[static_cast<std::size_t>(mi)];
            auto &hi = yMax[static_cast<std::size_t>(mi)];
            lo = std::min(lo, y);
            hi = std::max(hi, y);
        }
    }

    int onlyMi = -1;
    if (nEn == 1) {
        for (int mi = 0; mi < kMetricCount; ++mi) {
            if (m_metricEnabled[static_cast<std::size_t>(mi)]) {
                onlyMi = mi;
                break;
            }
        }
    }

    const int plotN = static_cast<int>(plotIdx.size());
    for (int mi = 0; mi < kMetricCount; ++mi) {
        auto *series = m_lineSeries[static_cast<std::size_t>(mi)];
        const double lo = yMin[static_cast<std::size_t>(mi)];
        const double hi = yMax[static_cast<std::size_t>(mi)];
        const double span = std::max(hi - lo, 1e-12);

        QList<QPointF> pts;
        pts.reserve(plotN);
        for (int si : plotIdx) {
            const FlightSample &s = samples[static_cast<std::size_t>(si)];
            const double x = chartXSeconds(tRef, s.timestamp, sessionElapsed);
            double y = sampleValueForMetric(s, mi);
            if (nEn > 1) {
                y = (y - lo) / span;
            }
            pts.append(QPointF(x, y));
        }
        series->replace(pts);
        const bool en = m_metricEnabled[static_cast<std::size_t>(mi)];
        series->setVisible(en);
        if (en) {
            applySeriesPointDisplay(series, pts.size(), nEn);
            if (nEn > 1) {
                series->setName(metricTitle(mi) + u" (norm)"_s);
            } else {
                series->setName(metricTitle(mi));
            }
        }
    }

    const double spanX = std::max(xMax - xMin, 1e-9);
    const double xPad = std::max(spanX * 0.02, 0.05);

    if (nEn == 1 && onlyMi >= 0) {
        m_axisY->setTitleText(metricAxisUnitShort(onlyMi));
        m_chart->setTitle(metricTitle(onlyMi));
        if (!m_preserveChartAxes) {
            const double lo = yMin[static_cast<std::size_t>(onlyMi)];
            const double hi = yMax[static_cast<std::size_t>(onlyMi)];
            const double span = std::max(hi - lo, 1e-9);
            const double p = span * 0.08 + std::max(std::abs(hi) * 1e-6, 1e-3);
            m_axisY->setRange(lo - p, hi + p);
        }
    } else {
        m_axisY->setTitleText(u"Normalized"_s);
        m_chart->setTitle(u"Multi-trace overlay"_s);
        if (!m_preserveChartAxes) {
            m_axisY->setRange(-0.05, 1.05);
        }
    }
    if (!m_preserveChartAxes) {
        m_axisX->setRange(xMin - xPad, xMax + xPad);
    }

    applyChartTheme();
    if (m_tracesPanel) {
        std::array<bool, kMetricCount> hasData{};
        for (int i = 0; i < kMetricCount; ++i)
            hasData[static_cast<std::size_t>(i)] = m_lineSeries[static_cast<std::size_t>(i)]
                                                   && m_lineSeries[static_cast<std::size_t>(i)]->count() > 0;
        m_tracesPanel->setMetricDataStates(hasData);
    }
    updateChartStatsLabel();
}

void DashboardPage::onSessionReset() {
    m_preserveChartAxes = false;
    if (m_liveChartCoalesceTimer) {
        m_liveChartCoalesceTimer->stop();
    }
    if (m_replayChartCoalesceTimer) {
        m_replayChartCoalesceTimer->stop();
    }
    m_hoverSampleIndexMap.clear();
    m_hoverLogicalSampleCount = 0;
    m_replayChartBuiltTrailLength = -1;
    m_liveSamples.clear();
    for (int mi = 0; mi < kMetricCount; ++mi) {
        if (m_lineSeries[static_cast<std::size_t>(mi)]) {
            m_lineSeries[static_cast<std::size_t>(mi)]->clear();
            m_lineSeries[static_cast<std::size_t>(mi)]->setVisible(false);
        }
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
    if (m_model && !m_model->replayMode()) {
        rebuildLiveSeriesFromHistory();
    }
    updateChartStatsLabel();
}

/**
 * Rebuilds all chart series from m_liveSamples (live telemetry mode).
 *
 * Same decimation and normalization logic as rebuildReplayCharts().  Called by
 * m_liveChartCoalesceTimer so it fires at most once per 50 ms even when samples
 * arrive faster.
 */
void DashboardPage::rebuildLiveSeriesFromHistory() {
    if (!m_axisX || !m_axisY) {
        return;
    }

    for (int mi = 0; mi < kMetricCount; ++mi) {
        if (m_lineSeries[static_cast<std::size_t>(mi)]) {
            m_lineSeries[static_cast<std::size_t>(mi)]->clear();
            m_lineSeries[static_cast<std::size_t>(mi)]->setVisible(false);
        }
    }

    if (m_liveSamples.empty()) {
        m_preserveChartAxes = false;
        m_hoverSampleIndexMap.clear();
        m_hoverLogicalSampleCount = 0;
        m_axisX->setRange(0, 10);
        m_axisY->setRange(-1, 1);
        m_chart->setTitle(u"Flight data"_s);
        updateChartStatsLabel();
        return;
    }

    const int end = static_cast<int>(m_liveSamples.size());
    const long tRef = m_liveSamples.front().timestamp;
    const bool sessionElapsed =
        useSessionElapsedTimeAxis(m_liveSamples.front().timestamp, m_liveSamples.back().timestamp);
    m_axisX->setTitleText(sessionElapsed ? u"Session time (s)"_s : u"Flight time (s)"_s);
    const int nEn = countEnabledMetrics();

    const std::vector<int> plotIdx = lttbIndicesForChartDisplay(
        m_liveSamples, end, kMaxChartDisplayPoints, m_metricEnabled, tRef, sessionElapsed);
    if (plotIdx.empty()) {
        updateChartStatsLabel();
        return;
    }
    m_hoverSampleIndexMap = plotIdx;
    m_hoverLogicalSampleCount = end;

    const int iFirst = plotIdx.front();
    const int iLast = plotIdx.back();
    double xMin = chartXSeconds(tRef, m_liveSamples[static_cast<std::size_t>(iFirst)].timestamp, sessionElapsed);
    double xMax = chartXSeconds(tRef, m_liveSamples[static_cast<std::size_t>(iLast)].timestamp, sessionElapsed);
    std::array<double, kMetricCount> yMin{};
    std::array<double, kMetricCount> yMax{};
    for (int mi = 0; mi < kMetricCount; ++mi) {
        yMin[static_cast<std::size_t>(mi)] = std::numeric_limits<double>::infinity();
        yMax[static_cast<std::size_t>(mi)] = -std::numeric_limits<double>::infinity();
    }

    for (int si : plotIdx) {
        const FlightSample &s = m_liveSamples[static_cast<std::size_t>(si)];
        for (int mi = 0; mi < kMetricCount; ++mi) {
            const double y = sampleValueForMetric(s, mi);
            auto &lo = yMin[static_cast<std::size_t>(mi)];
            auto &hi = yMax[static_cast<std::size_t>(mi)];
            lo = std::min(lo, y);
            hi = std::max(hi, y);
        }
    }

    int onlyMi = -1;
    if (nEn == 1) {
        for (int mi = 0; mi < kMetricCount; ++mi) {
            if (m_metricEnabled[static_cast<std::size_t>(mi)]) {
                onlyMi = mi;
                break;
            }
        }
    }

    const int plotN = static_cast<int>(plotIdx.size());
    for (int mi = 0; mi < kMetricCount; ++mi) {
        auto *series = m_lineSeries[static_cast<std::size_t>(mi)];
        const double lo = yMin[static_cast<std::size_t>(mi)];
        const double hi = yMax[static_cast<std::size_t>(mi)];
        const double span = std::max(hi - lo, 1e-12);

        QList<QPointF> pts;
        pts.reserve(plotN);
        for (int si : plotIdx) {
            const FlightSample &s = m_liveSamples[static_cast<std::size_t>(si)];
            const double x = chartXSeconds(tRef, s.timestamp, sessionElapsed);
            double y = sampleValueForMetric(s, mi);
            if (nEn > 1) {
                y = (y - lo) / span;
            }
            pts.append(QPointF(x, y));
        }
        series->replace(pts);
        const bool en = m_metricEnabled[static_cast<std::size_t>(mi)];
        series->setVisible(en);
        if (en) {
            applySeriesPointDisplay(series, pts.size(), nEn);
            if (nEn > 1) {
                series->setName(metricTitle(mi) + u" (norm)"_s);
            } else {
                series->setName(metricTitle(mi));
            }
        }
    }

    const double spanX = std::max(xMax - xMin, 1e-9);
    const double xPad = std::max(spanX * 0.02, 0.05);

    if (nEn == 1 && onlyMi >= 0) {
        m_axisY->setTitleText(metricAxisUnitShort(onlyMi));
        m_chart->setTitle(metricTitle(onlyMi));
        if (!m_preserveChartAxes) {
            const double lo = yMin[static_cast<std::size_t>(onlyMi)];
            const double hi = yMax[static_cast<std::size_t>(onlyMi)];
            const double span = std::max(hi - lo, 1e-9);
            const double p = span * 0.08 + std::max(std::abs(hi) * 1e-6, 1e-3);
            m_axisY->setRange(lo - p, hi + p);
        }
    } else {
        m_axisY->setTitleText(u"Normalized"_s);
        m_chart->setTitle(u"Multi-trace overlay"_s);
        if (!m_preserveChartAxes) {
            m_axisY->setRange(-0.05, 1.05);
        }
    }
    if (!m_preserveChartAxes) {
        m_axisX->setRange(xMin - xPad, xMax + xPad);
    }

    applyChartTheme();
    if (m_tracesPanel) {
        std::array<bool, kMetricCount> hasData{};
        for (int i = 0; i < kMetricCount; ++i)
            hasData[static_cast<std::size_t>(i)] = m_lineSeries[static_cast<std::size_t>(i)]
                                                   && m_lineSeries[static_cast<std::size_t>(i)]->count() > 0;
        m_tracesPanel->setMetricDataStates(hasData);
    }
    updateChartStatsLabel();
}

void DashboardPage::scheduleLiveChartRebuild() {
    if (m_liveChartCoalesceTimer) {
        m_liveChartCoalesceTimer->start(50);
    } else {
        rebuildLiveSeriesFromHistory();
        updateChartStatsLabel();
    }
}

void DashboardPage::zoomChartAxesAtCenter(bool zoomIn) {
    if (!m_axisX || !m_axisY) {
        return;
    }
    const double xMid = (m_axisX->min() + m_axisX->max()) * 0.5;
    const double yMid = (m_axisY->min() + m_axisY->max()) * 0.5;
    const double hx = (m_axisX->max() - m_axisX->min()) * 0.5;
    const double hy = (m_axisY->max() - m_axisY->min()) * 0.5;
    const double f = zoomIn ? 0.5 : 2.0;
    m_axisX->setRange(xMid - hx * f, xMid + hx * f);
    m_axisY->setRange(yMid - hy * f, yMid + hy * f);
    m_preserveChartAxes = true;
}

void DashboardPage::onSampleUpdated(const FlightSample &sample) {
    const double accelMag = std::sqrt(
        sample.acceleration.x * sample.acceleration.x
        + sample.acceleration.y * sample.acceleration.y
        + sample.acceleration.z * sample.acceleration.z);

    if (m_accelTile) m_accelTile->setValue(QStringLiteral("%1 m/s²").arg(accelMag, 0, 'f', 2));
    if (m_altTile)   m_altTile->setValue(QStringLiteral("%1 m").arg(sample.altitude, 0, 'f', 1));
    if (m_tempTile)  m_tempTile->setValue(QStringLiteral("%1 °C").arg(sample.temperature, 0, 'f', 1));
    if (m_pressTile) m_pressTile->setValue(QStringLiteral("%1").arg(sample.pressure, 0, 'f', 1));

    if (m_tracesPanel) m_tracesPanel->updateLiveValues(sample);

    if (!m_model || m_model->replayMode()) {
        return;
    }
    m_liveSamples.push_back(sample);
    if (static_cast<int>(m_liveSamples.size()) > kMaxLiveBufferSamples) {
        const int drop = static_cast<int>(m_liveSamples.size()) - kMaxLiveBufferSamples;
        m_liveSamples.erase(m_liveSamples.begin(), m_liveSamples.begin() + drop);
    }
    if (m_replayBar) m_replayBar->setLiveSampleCount(static_cast<int>(m_liveSamples.size()));
    scheduleLiveChartRebuild();
}

void DashboardPage::paintEvent(QPaintEvent *event) {
    QPainter painter(this);
    painter.setClipRegion(event->region());

    const QColor backgroundColor(47, 47, 47);
    painter.fillRect(rect(), backgroundColor);

    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, 50));

    constexpr int dotSpacing = 28;
    constexpr qreal dotDiameter = 3.0;
    const qreal dotRadius = dotDiameter / 2.0;
    const int offset = dotSpacing / 2;

    const int widthLimit = width();
    const int heightLimit = height();

    for (int y = offset; y < heightLimit; y += dotSpacing) {
        for (int x = offset; x < widthLimit; x += dotSpacing) {
            painter.drawEllipse(QPointF(x, y), dotRadius, dotRadius);
        }
    }
}
