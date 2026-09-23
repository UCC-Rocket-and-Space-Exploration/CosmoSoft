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
 * Peak-preserving selection keeps dense overviews responsive without averaging.
 * m_hoverSampleIndexMap indexes every source sample in the visible time range,
 * independently of the reduced overview used for drawing.
 */

#include "gui/pages/DashboardPage.h"

#include "domain/FlightSession.h"
#include "gui/FlightDataModel.h"
#include "gui/FlightReplayController.h"
#include "gui/Theme.h"
#include "gui/ThemeManager.h"
#include "gui/widgets/ChartSamples.h"
#include "gui/widgets/Map3DWidget.h"
#include "gui/widgets/MetricDefs.h"
#include "gui/widgets/ReplayBar.h"
#include "gui/widgets/TelemetryChartView.h"
#include "gui/widgets/TracesPanel.h"

#include <QAction>
#include <QApplication>
#include <QChart>
#include <QClipboard>
#include <QColor>
#include <QEvent>
#include <QFocusEvent>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMenu>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
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
    refreshPageStyleSheet();

    // ── Root layout ───────────────────────────────────────────────────────────
    // The page has two rows: the splitter (traces + chart, stretchy) and the
    // replay/transport bar (fixed height, docked at the bottom).
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setSpacing(16);
    rootLayout->setContentsMargins(24, 8, 24, 20);

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
    m_sessionInfoLabel->setObjectName(u"chartSessionInfo"_s);
    rootLayout->addWidget(m_sessionInfoLabel);

    // ── Traces panel (left side of splitter) ─────────────────────────────────
    m_tracesPanel = new TracesPanel(this);
    m_tracesPanel->setImperialUnits(m_imperialUnits);
    m_metricEnabled = m_tracesPanel->enabledMetrics();
    connect(m_tracesPanel, &TracesPanel::enabledMetricsChanged, this,
            [this](const std::array<bool, kMetricCount> &enabled) {
        m_metricEnabled = enabled;
        m_preserveChartAxes = false;
        if (m_comparisonActive) {
            exitComparisonView();
        }
        updateComparisonControls();
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
    chartHeaderLay->setContentsMargins(12, 12, 12, 12);
    chartHeaderLay->setSpacing(4);

    buildChartToolbar(chartHeader, chartHeaderLay);

    chartFrameLayout->addWidget(chartHeader, 0);

    m_emptyStateLabel = std::make_unique<QLabel>(chartFrame).release();
    m_emptyStateLabel->setAlignment(Qt::AlignCenter);
    m_emptyStateLabel->setWordWrap(true);
    m_emptyStateLabel->setObjectName(u"chartEmptyState"_s);
    m_emptyStateLabel->setTextFormat(Qt::RichText);
    m_emptyStateLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_emptyStateLabel->setOpenExternalLinks(false);
    m_emptyStateLabel->setFocusPolicy(Qt::StrongFocus);
    connect(m_emptyStateLabel, &QLabel::linkActivated, this, [this](const QString &link) {
        if (link == u"open"_s)
            emit openLogRequested();
        else if (link == u"live"_s)
            emit liveTelemetryRequested();
    });

    m_chart = new QChart();
    m_chart->setBackgroundRoundness(0);
    m_chart->setAnimationOptions(QChart::NoAnimation);

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
    m_chartView = tcv;
    tcv->setChart(m_chart);
    tcv->hoverDetail = [this](double tSec, int sampleIndex1Based, int /*totalSamples*/) {
        return formatMultiMetricHover(tSec, sampleIndex1Based);
    };
    // hoverReadout text is displayed by TelemetryChartView's own floating overlay;
    // no secondary label is needed in DashboardPage.
    tcv->hoverReadout = nullptr;

    m_chartContent = std::make_unique<QWidget>(chartFrame).release();
    m_chartContent->setObjectName(u"chartContent"_s);
    auto *plotsLayout = std::make_unique<QVBoxLayout>(m_chartContent).release();
    plotsLayout->setContentsMargins(0, 0, 0, 0);
    plotsLayout->setSpacing(4);
    m_chartScroll = std::make_unique<QScrollArea>(chartFrame).release();
    m_chartScroll->setObjectName(u"chartScroll"_s);
    m_chartScroll->setFrameShape(QFrame::NoFrame);
    m_chartScroll->setWidgetResizable(true);
    m_chartScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_chartScroll->setWidget(m_chartContent);
    for (int slot = 0; slot < kMetricCount; ++slot) {
        auto *chart = slot == 0 ? m_chart : std::make_unique<QChart>().release();
        auto *x = slot == 0 ? m_axisX : std::make_unique<QValueAxis>().release();
        auto *y = slot == 0 ? m_axisY : std::make_unique<QValueAxis>().release();
        if (slot != 0) {
            chart->addAxis(x, Qt::AlignBottom);
            chart->addAxis(y, Qt::AlignLeft);
        }
        auto *series = std::make_unique<QLineSeries>().release();
        series->setProperty("metricIndex", slot);
        series->setName(metricDisplayTitle(slot, m_imperialUnits));
        chart->addSeries(series);
        series->attachAxis(x);
        series->attachAxis(y);
        m_lineSeries[slot] = series;
        connect(y, &QValueAxis::rangeChanged, this, [this, y](qreal, qreal) {
            if (y == m_axisY) updateEventMarkerGeometry();
        });
        auto *view = slot == 0 ? tcv : std::make_unique<TelemetryChartView>(chart, m_chartContent).release();
        view->setObjectName(u"telemetryChartView"_s);
        view->setMinimumHeight(160);
        view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
        view->hoverDetail = tcv->hoverDetail;
        view->hoverValue = [this](const QLineSeries *series, int index) {
            const auto *sample = hoverSample(index);
            return sample ? metricDisplayValue(series->property("metricIndex").toInt(),
                                               sampleValueForMetric(*sample, series->property("metricIndex").toInt()),
                                               m_imperialUnits)
                          : std::numeric_limits<double>::quiet_NaN();
        };
        view->onUserAdjustedAxes = [this]() {
            m_preserveChartAxes = true;
            if (m_followToggle) m_followToggle->setChecked(false);
            if (m_model && m_model->replayMode())
                scheduleReplayChartRebuild();
            else
                scheduleLiveChartRebuild();
        };
        view->onResetAxes = [this]() { onResetChartZoom(); };
        m_traceCharts[slot] = chart;
        m_traceViews[slot] = view;
        m_traceAxesX[slot] = x;
        m_traceAxesY[slot] = y;
        plotsLayout->addWidget(view, 1);
        view->setVisible(slot == 0);
        connect(x, &QValueAxis::rangeChanged, this, [this, x](qreal low, qreal high) {
            if (m_syncingChartAxes) return;
            m_syncingChartAxes = true;
            for (auto *axis : m_traceAxesX) {
                if (axis && axis != x) axis->setRange(low, high);
            }
            m_syncingChartAxes = false;
        });
    }
    applyChartTheme();

    m_comparisonChart = new QChart();
    m_comparisonChart->setBackgroundRoundness(0);
    m_comparisonChart->setAnimationOptions(QChart::NoAnimation);
    m_comparisonAxisX = new QValueAxis();
    m_comparisonAxisX->setTitleText(u"Time (s)"_s);
    m_comparisonAxisLeft = new QValueAxis();
    m_comparisonAxisRight = new QValueAxis();
    m_comparisonChart->addAxis(m_comparisonAxisX, Qt::AlignBottom);
    m_comparisonChart->addAxis(m_comparisonAxisLeft, Qt::AlignLeft);
    m_comparisonChart->addAxis(m_comparisonAxisRight, Qt::AlignRight);
    m_comparisonView = new TelemetryChartView(m_comparisonChart, chartFrame);
    m_comparisonView->setObjectName(u"telemetryChartView"_s);
    m_comparisonView->setAccessibleName(u"Telemetry comparison"_s);
    m_comparisonView->setAccessibleDescription(
        u"Two telemetry metrics with separate real-value axes and a shared time axis."_s);
    m_comparisonView->hoverDetail = tcv->hoverDetail;
    m_comparisonView->hoverValue = [this](const QLineSeries *series, int index) {
        const auto *sample = hoverSample(index);
        return sample ? metricDisplayValue(series->property("metricIndex").toInt(),
                                           sampleValueForMetric(*sample, series->property("metricIndex").toInt()),
                                           m_imperialUnits)
                      : std::numeric_limits<double>::quiet_NaN();
    };
    m_comparisonView->onUserAdjustedAxes = [this]() {
        if (!m_comparisonAxisX || m_syncingComparisonAxes) return;
        m_preserveChartAxes = true;
        m_syncingComparisonAxes = true;
        int metricPosition = 0;
        for (int metric = 0; metric < kMetricCount; ++metric) {
            if (!m_metricEnabled[metric]) continue;
            auto *axis = m_traceAxesY[metric];
            const auto *source = metricPosition++ == 0 ? m_comparisonAxisLeft : m_comparisonAxisRight;
            if (axis && source) axis->setRange(source->min(), source->max());
        }
        for (auto *axis : m_traceAxesX) {
            if (axis) axis->setRange(m_comparisonAxisX->min(), m_comparisonAxisX->max());
        }
        m_syncingComparisonAxes = false;
        if (m_followToggle) m_followToggle->setChecked(false);
    };
    m_comparisonView->onResetAxes = [this]() {
        m_preserveChartAxes = false;
        refreshAllSeriesFromData();
    };
    connect(m_comparisonAxisX, &QValueAxis::rangeChanged, this, [this](qreal, qreal) {
        if (m_comparisonActive) m_comparisonView->invalidateHoverSeriesCache();
    });
    connect(m_comparisonAxisLeft, &QValueAxis::rangeChanged, this, [this](qreal, qreal) {
        if (!m_comparisonChart) return;
        for (auto *series : m_comparisonChart->series()) {
            auto *line = qobject_cast<QLineSeries *>(series);
            if (line && line->property("eventMarker").toBool()) {
                line->replace(QList<QPointF>{
                    QPointF(line->at(0).x(), m_comparisonAxisLeft->min()),
                    QPointF(line->at(0).x(), m_comparisonAxisLeft->max())});
            }
        }
    });
    for (auto *axis : m_traceAxesX) {
        connect(axis, &QValueAxis::rangeChanged, this, [this](qreal low, qreal high) {
            if (m_comparisonActive && !m_syncingComparisonAxes && m_comparisonAxisX) {
                m_comparisonAxisX->setRange(low, high);
            }
        });
    }

    m_graphStack = new QStackedWidget(chartFrame);
    m_graphStack->addWidget(m_chartScroll);
    m_graphStack->addWidget(m_comparisonView);

    // Keep unmodified chart keys local to the chart surface. Window-scoped
    // shortcuts steal ordinary input from the map and other application pages.
    auto *scZoomIn = new QShortcut(QKeySequence(Qt::Key_Plus), m_chartContent);
    auto *scZoomIn2 = new QShortcut(QKeySequence(Qt::Key_Equal), m_chartContent);
    auto *scZoomOut = new QShortcut(QKeySequence(Qt::Key_Minus), m_chartContent);
    auto *scFit = new QShortcut(QKeySequence(Qt::Key_F), m_chartContent);
    auto *scMarkers = new QShortcut(QKeySequence(Qt::Key_M), m_chartContent);
    auto *scValues = new QShortcut(QKeySequence(Qt::Key_V), m_chartContent);
    auto *scTraces = new QShortcut(QKeySequence(Qt::Key_T), m_chartContent);
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
    m_viewStack->addWidget(m_graphStack); // index 0 — telemetry chart

    m_mapWidget = new Map3DWidget(m_viewStack);
    m_mapWidget->setImperialUnits(m_imperialUnits);
    m_mapWidget->setAccessibleName(u"Flight path map"_s);
    m_mapWidget->setAccessibleDescription(u"Interactive map showing the flight path and GPS coordinates"_s);
    m_viewStack->addWidget(m_mapWidget);   // index 1 — 3D flight path map
    m_viewStack->addWidget(m_emptyStateLabel); // index 2 — empty state
    m_viewStack->setCurrentIndex(2);
    updateToolbarForView();

    chartFrameLayout->addWidget(m_viewStack, 1);

    connect(m_graphViewBtn, &QPushButton::clicked, this, [this](bool checked) {
        if (!checked) {
            m_graphViewBtn->setChecked(true);
            return;
        }
        const bool hasData = !m_liveSamples.empty() || (m_session && !m_session->samples.empty());
        m_viewStack->setCurrentIndex(hasData ? 0 : 2);
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
    dashSplitter->setHandleWidth(12);
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
            dashSplitter->setSizes({260, 1000});
        }
    }
    connect(dashSplitter, &QSplitter::splitterMoved, this, [this, dashSplitter]() {
        if (!m_splitterSavePending) {
            m_splitterSavePending = true;
            QTimer::singleShot(500, this, [this, dashSplitter] {
                m_splitterSavePending = false;
                QSettings s(kSettingsOrg, kSettingsApp);
                s.setValue(kSettingsDashSplitter, dashSplitter->saveState());
            });
        }
    });
    connect(m_tracesToggleBtn, &QToolButton::toggled, this,
            [this, dashSplitter](bool show) {
        if (show) {
            m_tracesPanel->setVisible(true);
            dashSplitter->setSizes({260, dashSplitter->width() - 260});
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
    refreshPageStyleSheet();

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
    QFont axisFont = QApplication::font();
    axisFont.setFamily(u"Red Hat Mono"_s);
    QFont titleFont = axisFont;
    titleFont.setBold(true);
    for (auto *chart : m_traceCharts) {
        if (!chart) continue;
        chart->setAnimationOptions(QChart::NoAnimation);
        chart->setBackgroundRoundness(0);
        chart->setBackgroundBrush(QColor(Theme::kBgPanel()));
        chart->setBackgroundPen(Qt::NoPen);
        chart->setPlotAreaBackgroundBrush(QColor(Theme::kBgBase()));
        chart->setPlotAreaBackgroundVisible(true);
        chart->setTitleFont(titleFont);
        chart->legend()->hide();
        chart->setMargins(QMargins(0, 0, 0, 0));
        for (auto *abstractAxis : chart->axes()) {
            auto *axis = qobject_cast<QValueAxis *>(abstractAxis);
            if (!axis) continue;
            axis->setLabelsFont(axisFont);
            axis->setTitleFont(axisFont);
            axis->setLabelsColor(QColor(Theme::kTextMid()));
            axis->setTitleBrush(QColor(Theme::kTextMid()));
            axis->setLinePenColor(QColor(Theme::kBorderDefault()));
            axis->setGridLinePen(QPen(QColor(Theme::kBorderSubtle()), 1, Qt::SolidLine));
            axis->setMinorGridLineVisible(false);
            axis->setTruncateLabels(false);
        }
        for (auto *abstractSeries : chart->series()) {
            auto *series = qobject_cast<QLineSeries *>(abstractSeries);
            if (!series || !series->property("metricIndex").isValid()) continue;
            series->setPen(QPen(metricColor(series->property("metricIndex").toInt()), 1.25, Qt::SolidLine, Qt::FlatCap,
                                Qt::MiterJoin));
            series->setPointLabelsColor(QColor(Theme::kTextPrimary()));
        }
    }
    for (int metric = 0; metric < kMetricCount; ++metric) {
        if (!m_traceCharts[metric]) continue;
        m_traceCharts[metric]->setTitleBrush(metricColor(metric));
        m_traceAxesY[metric]->setTitleBrush(metricColor(metric));
        m_traceAxesY[metric]->setLabelsColor(metricColor(metric));
    }
    if (m_comparisonChart) {
        m_comparisonChart->setAnimationOptions(QChart::NoAnimation);
        m_comparisonChart->setBackgroundRoundness(0);
        m_comparisonChart->setBackgroundBrush(QColor(Theme::kBgPanel()));
        m_comparisonChart->setBackgroundPen(Qt::NoPen);
        m_comparisonChart->setPlotAreaBackgroundBrush(QColor(Theme::kBgBase()));
        m_comparisonChart->setPlotAreaBackgroundVisible(true);
        m_comparisonChart->setTitleFont(titleFont);
        m_comparisonChart->legend()->hide();
        m_comparisonChart->setMargins(QMargins(0, 0, 0, 0));
        for (auto *abstractAxis : m_comparisonChart->axes()) {
            auto *axis = qobject_cast<QValueAxis *>(abstractAxis);
            if (!axis) continue;
            axis->setLabelsFont(axisFont);
            axis->setTitleFont(axisFont);
            axis->setLinePenColor(QColor(Theme::kBorderDefault()));
            axis->setGridLinePen(QPen(QColor(Theme::kBorderSubtle()), 1, Qt::SolidLine));
            axis->setMinorGridLineVisible(false);
            axis->setTruncateLabels(false);
        }
        m_comparisonAxisX->setLabelsColor(QColor(Theme::kTextMid()));
        m_comparisonAxisX->setTitleBrush(QColor(Theme::kTextMid()));
        for (auto *axis : {m_comparisonAxisLeft, m_comparisonAxisRight}) {
            const QVariant metric = axis->property("metricIndex");
            if (metric.isValid()) {
                axis->setLabelsColor(metricColor(metric.toInt()));
                axis->setTitleBrush(metricColor(metric.toInt()));
            }
        }
        for (auto *abstractSeries : m_comparisonChart->series()) {
            auto *series = qobject_cast<QLineSeries *>(abstractSeries);
            if (!series || !series->property("metricIndex").isValid()) continue;
            series->setPen(QPen(metricColor(series->property("metricIndex").toInt()), 1.25,
                                Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
            series->setPointLabelsColor(QColor(Theme::kTextPrimary()));
        }
    }
    refreshEventMarkerTheme();
}

void DashboardPage::configureTracePlots() {
    for (auto *chart : m_traceCharts) {
        for (auto *series : chart->series()) {
            if (!series->property("gapSegment").toBool()) continue;
            chart->removeSeries(series);
            const std::unique_ptr<QAbstractSeries> removed(series);
        }
    }
    const int enabledCount = countEnabledMetrics();
    const bool focused = m_comparisonActive && enabledCount == 1;
    const bool stackedComparison = m_comparisonActive && enabledCount > 2;
    int primary = -1;
    for (int metric = 0; metric < kMetricCount; ++metric) {
        if (m_metricEnabled[metric] && primary < 0) primary = metric;
        auto *view = m_traceViews[metric];
        view->setVisible(m_metricEnabled[metric]);
        view->setMinimumHeight(focused ? 520 : (stackedComparison ? 260 : (enabledCount > 1 ? 160 : 280)));
        view->setAccessibleName(metricDisplayTitle(metric, m_imperialUnits));
        view->setAccessibleDescription(
            tr("Original samples. Drag to pan, wheel to zoom, left and right arrows to inspect, double-click to fit."));
    }
    if (primary < 0) {
        primary = 0;
        m_traceViews[0]->show();
    }
    const bool primaryChanged = m_chart != m_traceCharts[primary];
    const auto markers = m_eventMarkers;
    if (primaryChanged) clearEventMarkers();
    m_chart = m_traceCharts[primary];
    m_chartView = m_traceViews[primary];
    m_axisX = m_traceAxesX[primary];
    m_axisY = m_traceAxesY[primary];
    if (primaryChanged) {
        for (const auto &marker : markers)
            addEventMarker(marker.timeSec, marker.name);
    }
    updateComparisonControls();
}

void DashboardPage::updateComparisonControls()
{
    if (!m_compareSelectedBtn || !m_exitComparisonBtn) {
        return;
    }

    const int selected = countEnabledMetrics();
    m_compareSelectedBtn->setText(selected == 1 ? tr("Focus selected") : tr("Compare selected"));
    m_compareSelectedBtn->setEnabled(selected > 0 && !m_comparisonActive);
    m_compareSelectedBtn->setVisible(!m_comparisonActive);
    m_exitComparisonBtn->setVisible(m_comparisonActive);
}

void DashboardPage::enterComparisonView()
{
    const int selected = countEnabledMetrics();
    if (selected <= 0 || !m_graphStack) {
        return;
    }

    m_comparisonActive = true;
    if (selected == 2) {
        rebuildComparisonChart();
        m_graphStack->setCurrentWidget(m_comparisonView);
    } else {
        m_graphStack->setCurrentWidget(m_chartScroll);
    }
    configureTracePlots();
}

void DashboardPage::exitComparisonView()
{
    if (!m_comparisonActive || !m_graphStack) {
        return;
    }

    m_comparisonActive = false;
    m_graphStack->setCurrentWidget(m_chartScroll);
    configureTracePlots();
}

void DashboardPage::rebuildComparisonChart()
{
    if (!m_comparisonChart || !m_comparisonAxisX || !m_comparisonAxisLeft
        || !m_comparisonAxisRight || !m_comparisonView || countEnabledMetrics() != 2) {
        return;
    }

    const auto previousSeries = m_comparisonChart->series();
    for (auto *series : previousSeries) {
        m_comparisonChart->removeSeries(series);
        delete series;
    }

    std::array<int, 2> selected{};
    int selectedIndex = 0;
    for (int metric = 0; metric < kMetricCount; ++metric) {
        if (m_metricEnabled[metric]) {
            selected[selectedIndex++] = metric;
        }
    }
    if (selectedIndex != 2) {
        return;
    }

    const auto copyMetric = [this](int metric, QValueAxis *axis) {
        const auto *sourceChart = m_traceCharts[metric];
        const auto *sourceAxis = m_traceAxesY[metric];
        if (!sourceChart || !sourceAxis) {
            return;
        }
        axis->setRange(sourceAxis->min(), sourceAxis->max());
        axis->setProperty("metricIndex", metric);
        axis->setTitleText(metricDisplayUnitShort(metric, m_imperialUnits));
        axis->setLabelsColor(metricColor(metric));
        axis->setTitleBrush(metricColor(metric));
        for (auto *source : sourceChart->series()) {
            auto *sourceLine = qobject_cast<QLineSeries *>(source);
            if (!sourceLine || !sourceLine->property("metricIndex").isValid()
                || sourceLine->property("metricIndex").toInt() != metric) {
                continue;
            }
            auto *line = new QLineSeries();
            line->setProperty("metricIndex", metric);
            line->setProperty("gapSegment", sourceLine->property("gapSegment"));
            line->setName(sourceLine->name());
            line->setPen(sourceLine->pen());
            line->replace(sourceLine->points());
            applySeriesPointDisplay(line, static_cast<int>(m_hoverSampleIndexMap.size()));
            m_comparisonChart->addSeries(line);
            line->attachAxis(m_comparisonAxisX);
            line->attachAxis(axis);
        }
    };

    const auto *sourceX = m_traceAxesX[selected[0]];
    if (sourceX) {
        m_comparisonAxisX->setRange(sourceX->min(), sourceX->max());
    }
    copyMetric(selected[0], m_comparisonAxisLeft);
    copyMetric(selected[1], m_comparisonAxisRight);
    m_comparisonChart->setTitle(
        tr("Comparison: %1 · %2")
            .arg(metricDisplayTitle(selected[0], m_imperialUnits),
                 metricDisplayTitle(selected[1], m_imperialUnits)));
    m_comparisonChart->setTitleBrush(QColor(Theme::kTextPrimary()));

    for (const auto &marker : m_eventMarkers) {
        auto *line = new QLineSeries();
        line->setName(marker.name);
        line->setProperty("eventMarker", true);
        line->append(marker.timeSec, m_comparisonAxisLeft->min());
        line->append(marker.timeSec, m_comparisonAxisLeft->max());
        applyEventMarkerTheme(line);
        m_comparisonChart->addSeries(line);
        line->attachAxis(m_comparisonAxisX);
        line->attachAxis(m_comparisonAxisLeft);
    }

    m_comparisonView->setHoverXValues(m_chartHoverXValues);
    m_comparisonView->invalidateHoverSeriesCache();
    applyChartTheme();
}

void DashboardPage::buildChartToolbar(QWidget *chartHeader, QVBoxLayout *chartHeaderLay) {
    auto *chartToolbar = new QHBoxLayout();
    chartToolbar->setSpacing(6);


    m_tracesToggleBtn = new QToolButton(chartHeader);
    m_tracesToggleBtn->setObjectName(u"chartToggleBtn"_s);
    m_tracesToggleBtn->setText(u"Traces"_s);
    m_tracesToggleBtn->setCheckable(true);
    m_tracesToggleBtn->setChecked(true);
    m_tracesToggleBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_tracesToggleBtn->setCursor(Qt::PointingHandCursor);
    m_tracesToggleBtn->setToolTip(u"Show/hide the traces side panel  [T key]"_s);
    chartToolbar->addWidget(m_tracesToggleBtn);

    m_compareSelectedBtn = new QPushButton(chartHeader);
    m_compareSelectedBtn->setObjectName(u"chartToolbarBtn"_s);
    m_compareSelectedBtn->setCursor(Qt::PointingHandCursor);
    m_compareSelectedBtn->setToolTip(
        u"Focus one trace, compare two with separate axes, or align several traces by time."_s);
    connect(m_compareSelectedBtn, &QPushButton::clicked, this, &DashboardPage::enterComparisonView);
    chartToolbar->addWidget(m_compareSelectedBtn);

    m_exitComparisonBtn = new QPushButton(u"Back"_s, chartHeader);
    m_exitComparisonBtn->setObjectName(u"chartToolbarBtn"_s);
    m_exitComparisonBtn->setCursor(Qt::PointingHandCursor);
    m_exitComparisonBtn->setToolTip(u"Return to separate graphs"_s);
    m_exitComparisonBtn->setVisible(false);
    connect(m_exitComparisonBtn, &QPushButton::clicked, this, &DashboardPage::exitComparisonView);
    chartToolbar->addWidget(m_exitComparisonBtn);

    m_viewSwitchGroup = new QWidget(chartHeader);
    m_viewSwitchGroup->setObjectName(u"viewSwitchGroup"_s);
    auto *viewGroupLay = new QHBoxLayout(m_viewSwitchGroup);
    viewGroupLay->setContentsMargins(3, 3, 3, 3);
    viewGroupLay->setSpacing(2);

    m_graphViewBtn = new QPushButton(u"Graph"_s, m_viewSwitchGroup);
    m_graphViewBtn->setObjectName(u"viewSwitchBtn"_s);
    m_graphViewBtn->setCheckable(true);
    m_graphViewBtn->setChecked(true);
    m_graphViewBtn->setCursor(Qt::PointingHandCursor);
    m_graphViewBtn->setToolTip(u"Show telemetry chart"_s);
    m_graphViewBtn->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    viewGroupLay->addWidget(m_graphViewBtn);

    m_mapViewBtn = new QPushButton(u"Map"_s, m_viewSwitchGroup);
    m_mapViewBtn->setObjectName(u"viewSwitchBtn"_s);
    m_mapViewBtn->setCheckable(true);
    m_mapViewBtn->setChecked(false);
    m_mapViewBtn->setCursor(Qt::PointingHandCursor);
    m_mapViewBtn->setToolTip(u"Show flight path map"_s);
    m_mapViewBtn->setIcon(style()->standardIcon(QStyle::SP_FileDialogContentsView));
    viewGroupLay->addWidget(m_mapViewBtn);
    chartToolbar->insertWidget(0, m_viewSwitchGroup);

    m_zoomGroup = new QWidget(chartHeader);
    m_zoomGroup->setObjectName(u"chartToolGroup"_s);
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
    m_toggleGroup->setObjectName(u"chartToolGroup"_s);
    auto *toggleGroupLay = new QHBoxLayout(m_toggleGroup);
    toggleGroupLay->setContentsMargins(2, 2, 2, 2);
    toggleGroupLay->setSpacing(4);

    m_showMarkersToggle = new QToolButton(m_toggleGroup);
    m_showMarkersToggle->setObjectName(u"chartToggleBtn"_s);
    m_showMarkersToggle->setText(u"Markers"_s);
    m_showMarkersToggle->setCheckable(true);
    m_showMarkersToggle->setChecked(true);
    m_showMarkersToggle->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_showMarkersToggle->setCursor(Qt::PointingHandCursor);
    m_showMarkersToggle->setToolTip(u"Show sample dots when zoomed to 400 or fewer samples per trace.  [M key]"_s);
    toggleGroupLay->addWidget(m_showMarkersToggle);

    m_showPointValuesToggle = new QToolButton(m_toggleGroup);
    m_showPointValuesToggle->setObjectName(u"chartToggleBtn"_s);
    m_showPointValuesToggle->setText(u"Values"_s);
    m_showPointValuesToggle->setCheckable(true);
    m_showPointValuesToggle->setChecked(false);
    m_showPointValuesToggle->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_showPointValuesToggle->setCursor(Qt::PointingHandCursor);
    m_showPointValuesToggle->setToolTip(
        u"Label original values when zoomed to 20 or fewer samples per trace.  [V key]"_s);
    toggleGroupLay->addWidget(m_showPointValuesToggle);

    m_followToggle = new QToolButton(m_toggleGroup);
    m_followToggle->setObjectName(u"chartToggleBtn"_s);
    m_followToggle->setText(u"Follow"_s);
    m_followToggle->setCheckable(true);
    m_followToggle->setChecked(true);
    m_followToggle->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_followToggle->setCursor(Qt::PointingHandCursor);
    m_followToggle->setToolTip(
        u"Auto-fit chart axes to data on each update.\n"
        u"Turn off to preserve your zoom level during replay.\n"
        u"Pan or zoom the chart to disable automatically."_s);
    toggleGroupLay->addWidget(m_followToggle);

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

    auto *toolsButton = std::make_unique<QToolButton>(chartHeader).release();
    m_actionGroup = toolsButton;
    toolsButton->setObjectName(u"chartToggleBtn"_s);
    toolsButton->setText(u"Tools"_s);
    toolsButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    toolsButton->setArrowType(Qt::NoArrow);
    toolsButton->setFixedHeight(34);
    toolsButton->setToolTip(u"Chart tools"_s);
    toolsButton->setAccessibleName(u"Chart tools"_s);
    toolsButton->setFocusPolicy(Qt::TabFocus);
    toolsButton->setPopupMode(QToolButton::InstantPopup);
    auto *toolsMenu = std::make_unique<QMenu>(toolsButton).release();
    toolsButton->setMenu(toolsMenu);
    auto *addMarkerBtn = toolsMenu->addAction(u"Add event marker…"_s);
    connect(addMarkerBtn, &QAction::triggered, this, [this]() {
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

    auto *clearMarkersBtn = toolsMenu->addAction(u"Clear event markers"_s);
    connect(clearMarkersBtn, &QAction::triggered, this, &DashboardPage::clearEventMarkers);
    toolsMenu->addSeparator();
    auto *copyChartBtn = toolsMenu->addAction(u"Copy chart image"_s);
    connect(copyChartBtn, &QAction::triggered, this, [this]() {
        if (m_chartView && m_graphStack) {
            QPixmap pixmap = m_graphStack->currentWidget()->grab();
            QApplication::clipboard()->setPixmap(pixmap);
        }
    });

    auto *copyDataBtn = toolsMenu->addAction(u"Copy current sample"_s);
    connect(copyDataBtn, &QAction::triggered, this, [this]() {
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
    helpBtn->setText(u"Help"_s);
    helpBtn->setCheckable(true);
    helpBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    helpBtn->setFocusPolicy(Qt::TabFocus);
    helpBtn->setToolTip(u"Show chart controls"_s);
    helpBtn->setAccessibleName(u"Show chart controls"_s);
    m_chartHelpBtn = helpBtn;
    chartToolbar->addWidget(m_chartHelpBtn);

    m_chartHelpHint = new QLabel(chartHeader);
    m_chartHelpHint->setObjectName(u"chartHelpHint"_s);
    m_chartHelpHint->setWordWrap(true);
    m_chartHelpHint->setText(
        u"Drag to pan · Ctrl+drag to select an area · Wheel to zoom · Trackpad scroll to pan · "
        u"Use + / − or Fit for time range · Hover or ← / → to inspect original samples · "
        u"Double-click to fit."_s);
    m_chartHelpHint->setVisible(false);
    connect(helpBtn, &QToolButton::toggled, m_chartHelpHint, &QLabel::setVisible);

    chartHeaderLay->addLayout(chartToolbar);
    chartHeaderLay->addWidget(m_chartHelpHint);
    chartHeaderLay->addWidget(m_toggleGroup, 0, Qt::AlignLeft);
    updateCompactToolbar();
}

QString DashboardPage::buildDashboardQss() {
    const auto borderPanel = Theme::kBorderSubtle();
    const auto borderLight = Theme::kBorderLight();
    const auto bgButton    = Theme::kBgButton();
    const auto textPri     = Theme::kTextPrimary();
    const auto textMuted   = Theme::kTextMuted();
    const auto bgPanel     = Theme::kBgPanel();
    const auto btnHov      = Theme::kBtnHover();
    const auto btnPressed  = Theme::kBtnPressed();
    const auto accent      = Theme::kAccentLink();
    const auto bgInput     = Theme::kBgInput();
    const auto checkedText = Theme::kTextPrimary();

    auto ss = QString(uR"(
        QWidget#chartContent, QScrollArea#chartScroll { background: %2; border: none; }
        QWidget#chartToolGroup { background: transparent; border: none; }
        #dashboardPage { background-color: transparent; color: %1; }
        QFrame#chartFrame { background-color: %2; border: 1px solid %3; border-radius: %4px; padding: 0px; }
        #telemetryChartView { border: none; padding: 0px; margin: 0px; background-color: transparent; }
        QToolButton#chartToggleBtn {
            border: 1px solid %5;
            border-radius: %6px;
            padding: 4px 10px;
            min-height: 26px;
            background-color: %7;
            color: %1;
            font-size: 11px;
            font-weight: 500;
        }
        QToolButton#chartToggleBtn::menu-indicator { image: none; width: 0px; }
        QToolButton#chartToggleBtn:hover { background-color: %8; color: %1; border-color: %5; }
    )"_s)
                  .arg(textPri)
                  .arg(bgPanel)
                  .arg(borderPanel)
                  .arg(Theme::kRadiusMd)
                  .arg(borderLight)
                  .arg(Theme::kRadiusSm)
                  .arg(bgButton)
                  .arg(btnHov);

    ss += QString(uR"(
        QToolButton#chartToggleBtn:checked {
            background-color: %4;
            border-color: %1;
            color: %10;
            font-weight: 600;
        }
        QToolButton#chartToggleBtn:checked:hover {
            background-color: %4;
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
              .arg(accent)
              .arg(borderPanel)
              .arg(Theme::kRadiusSm)
              .arg(bgButton)
              .arg(textPri)
              .arg(btnHov)
              .arg(borderLight)
              .arg(btnPressed)
              .arg(btnPressed)
              .arg(checkedText); // %10 - validated against the accent background

    ss += QString(uR"(
        QDoubleSpinBox { background-color: %1; color: %2; border: 1px solid %3; border-radius: %4px; padding: 4px 8px; min-height: 22px; }
        QSlider::groove:horizontal { height: 6px; background: %5; border-radius: 3px; }
        QSlider::handle:horizontal { width: 14px; margin: -5px 0; background: %6; border: 1px solid %3; border-radius: %4px; }
        QPushButton#chartToolbarBtn {
            border: 1px solid %7;
            border-radius: %4px;
            padding: 4px 10px;
            min-height: 26px;
            background-color: %8;
            color: %2;
            font-size: 11px;
            font-weight: 500;
        }
    )"_s)
              .arg(bgInput)
              .arg(textPri)
              .arg(Theme::kBorderDefault())
              .arg(Theme::kRadiusSm)
              .arg(bgPanel)
              .arg(borderLight)
              .arg(borderPanel)
              .arg(bgButton);

    ss += QString(uR"(
        QPushButton#chartToolbarBtn:hover {
            background-color: %1;
            border-color: %2;
        }
        QPushButton#chartToolbarBtn:pressed {
            background-color: %3;
        }
        QWidget#viewSwitchGroup { background: %4; border: 1px solid %4; border-radius: %5px; }
        QPushButton#viewSwitchBtn { border: none; border-radius: %5px; padding: 4px 12px; min-height: 28px; background-color: transparent; color: %6; font-size: %7px; font-weight: 500; }
        QPushButton#viewSwitchBtn:checked { background-color: %1; border: 1px solid %8; color: %10; font-weight: 600; }
        QPushButton#viewSwitchBtn:hover:!checked { background-color: %1; color: %9; }
        QPushButton#viewSwitchBtn:pressed:!checked { background-color: %3; }
        QWidget#viewSwitchGroup[compact="true"] QPushButton#viewSwitchBtn {
            min-width: 32px;
            max-width: 32px;
            min-height: 30px;
            padding: 2px;
        }
    )"_s)
              .arg(btnHov)
              .arg(borderLight)
              .arg(btnPressed)
              .arg(borderPanel)
              .arg(Theme::kRadiusMd)
              .arg(textMuted)
              .arg(Theme::kFontSizeSm)
              .arg(accent)
              .arg(textPri)
              .arg(checkedText); // %10 - validated against the accent background

    ss += QString(uR"(
        QToolButton#chartHelpBtn { font-weight: 600; font-size: %1px; min-height: 30px; border: 1px solid %2; border-radius: %8px; background: %3; color: %4; padding: 4px 10px; }
        QToolButton#chartHelpBtn:hover, QToolButton#chartHelpBtn:checked { color: %5; border-color: %6; background: %7; }
        QLabel#chartHelpHint { color: %4; background: %3; border: 1px solid %2; border-radius: %8px; padding: 7px 9px; font-size: %9px; }
    )"_s)
        .arg(Theme::kFontSizeMd).arg(borderPanel).arg(bgButton)
        .arg(textMuted).arg(textPri).arg(borderLight)
        .arg(btnHov).arg(Theme::kRadiusSm).arg(Theme::kFontSizeSm);

    ss += QString(uR"(
        QLabel#chartSessionInfo { color: %1; background: transparent; border: none; padding: 6px 0; font-size: 11px; }
        QLabel#chartEmptyState {
            color: %1;
            font-size: 14px;
            padding: 40px;
            background: transparent;
            border: none;
        }
    )"_s)
              .arg(textMuted);

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

    if (m_emptyStateLabel) {
        m_emptyStateLabel->setText(QString(u"<b>Your flight, in focus.</b><br><br>"
                                           "Explore telemetry, compare traces, and replay the flight path.<br>"
                                           "Open a recorded session or connect a live device to begin.<br><br>"
                                           "<a href=\"open\" style=\"color:%1\">Open flight log</a>"
                                           " &nbsp;·&nbsp; <a href=\"live\" style=\"color:%1\">Connect a device</a>"_s)
                                       .arg(Theme::kAccentLink()));
    }
    update();

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
    forceButtonStyleUpdate(m_actionGroup);
}

void DashboardPage::updateToolbarForView() {
    const bool graphMode = m_viewStack && m_viewStack->currentIndex() == 0;
    if (m_zoomGroup)      m_zoomGroup->setVisible(graphMode);
    if (m_toggleGroup)    m_toggleGroup->setVisible(graphMode);
    if (m_actionGroup)    m_actionGroup->setVisible(graphMode);
    if (m_chartHelpBtn)   m_chartHelpBtn->setVisible(graphMode);
    if (!graphMode && m_chartHelpBtn) m_chartHelpBtn->setChecked(false);
    if (m_chartHelpHint)  m_chartHelpHint->setVisible(graphMode && m_chartHelpBtn && m_chartHelpBtn->isChecked());
    if (m_tracesToggleBtn) m_tracesToggleBtn->setVisible(graphMode);
    if (m_compareSelectedBtn) m_compareSelectedBtn->setVisible(graphMode && !m_comparisonActive);
    if (m_exitComparisonBtn) m_exitComparisonBtn->setVisible(graphMode && m_comparisonActive);
    if (m_tracesPanel) m_tracesPanel->setVisible(graphMode && m_tracesToggleBtn->isChecked());
    for (auto *shortcut : m_chartShortcuts) {
        if (shortcut) {
            shortcut->setEnabled(graphMode);
        }
    }
}

void DashboardPage::updateCompactToolbar() {
    constexpr int kCompactToolbarWidth = 900;
    const bool compact = width() > 0 && width() < kCompactToolbarWidth;
    if (!m_viewSwitchGroup || !m_graphViewBtn || !m_mapViewBtn) {
        return;
    }

    m_viewSwitchGroup->setProperty("compact", compact);
    m_graphViewBtn->setText(compact ? QString() : tr("Graph"));
    m_mapViewBtn->setText(compact ? QString() : tr("Map"));
    m_graphViewBtn->setAccessibleName(tr("Show telemetry chart"));
    m_mapViewBtn->setAccessibleName(tr("Show flight path map"));

    for (auto *button : {m_graphViewBtn, m_mapViewBtn}) {
        button->style()->unpolish(button);
        button->style()->polish(button);
        button->update();
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
    for (auto *chart : m_traceCharts) {
        for (auto *abstract : chart->series()) {
            auto *series = qobject_cast<QLineSeries *>(abstract);
            if (series && series->property("metricIndex").isValid()) {
                applySeriesPointDisplay(series, static_cast<int>(m_hoverSampleIndexMap.size()));
            }
        }
    }
    if (m_comparisonActive && countEnabledMetrics() == 2) {
        rebuildComparisonChart();
    }
}

void DashboardPage::applySeriesPointDisplay(QLineSeries *series, int pointCount) const {
    if (!series) {
        return;
    }

    const bool markersOn = !m_showMarkersToggle || m_showMarkersToggle->isChecked();
    const bool showVertices = markersOn && pointCount > 0 && pointCount <= 400;
    series->setPointsVisible(showVertices);

    const bool wantLabels = m_showPointValuesToggle && m_showPointValuesToggle->isChecked();
    const bool showLabels = wantLabels && pointCount > 0 && pointCount <= 20;
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
 * The lookup covers every original sample in the visible time range, even when
 * the overview contains fewer display points. No curve interpolation is used.
 */
const FlightSample *DashboardPage::hoverSample(int displayPointIndex1Based) const {
    if (displayPointIndex1Based < 1) {
        return {};
    }
    const int di = displayPointIndex1Based - 1;
    int si = di;
    if (!m_hoverSampleIndexMap.empty()) {
        if (di >= static_cast<int>(m_hoverSampleIndexMap.size())) {
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
    return sp;
}

QString DashboardPage::formatMultiMetricHover(double tSec, int displayPointIndex1Based) const {
    const auto *sp = hoverSample(displayPointIndex1Based);
    if (!sp) return {};
    const int si = m_hoverSampleIndexMap[displayPointIndex1Based - 1];
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
                                      ? QString::number(metricDisplayValue(mi, siValue, m_imperialUnits), 'g', 12)
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
    if (!m_model || !m_model->replayMode()) {
        return;  // not in replay mode; ignore stale positionChanged signals
    }
    if (trailLength == m_replayChartBuiltTrailLength) {
        scheduleRenderPass();
        return;  // chart already correct; ReplayBar updates its own labels via positionChanged
    }
    scheduleReplayChartRebuild();
}

/** Marks replay data dirty; the shared scheduler enforces the 30 Hz cap. */
void DashboardPage::scheduleReplayChartRebuild() {
    m_replayChartDirty = true;
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
        }
    }

    scheduleRenderPass();
}

/**
 * Rebuilds all chart series from the first @p trailLength samples of m_session.
 *
 * Design notes:
 * - Dense ranges retain bucket extrema; inspection uses all original samples.
 * - Each metric has its own plot and retains its measurement units.
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

    configureTracePlots();
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
        for (auto *view : m_traceViews) {
            view->setHoverXValues({});
            view->invalidateHoverSeriesCache();
        }
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
        for (auto *view : m_traceViews) {
            view->setHoverXValues({});
            view->invalidateHoverSeriesCache();
        }
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
        const auto indices = ChartSamples::select(samples, begin, rangeEnd, budget, m_metricEnabled);
        m_replayChartBuiltBucket = m_preview->replayBucket(end, budget);
        buildChartFromSampleIndices(samples, indices, n, begin, rangeEnd);
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
    configureTracePlots();
    for (int mi = 0; mi < kMetricCount; ++mi) {
        if (m_lineSeries[static_cast<std::size_t>(mi)]) {
            m_lineSeries[static_cast<std::size_t>(mi)]->clear();
            m_lineSeries[static_cast<std::size_t>(mi)]->setVisible(false);
        }
    }
    for (auto *view : m_traceViews) {
        view->setHoverXValues({});
        view->invalidateHoverSeriesCache();
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
 * Same raw-value and peak-preserving rendering as rebuildReplayCharts(). Called by
 * the shared scheduler at no more than 20 Hz.
 */
void DashboardPage::rebuildLiveSeriesFromHistory() {
    if (!m_axisX || !m_axisY) {
        return;
    }

    configureTracePlots();
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
        for (auto *view : m_traceViews) {
            view->setHoverXValues({});
            view->invalidateHoverSeriesCache();
        }
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
    int begin = 0;
    int rangeEnd = boundedEnd;
    if (m_preserveChartAxes) {
        // Include neighbours at the viewport edges so line segments remain continuous.
        while (begin + 1 < boundedEnd &&
               chartXSeconds(tRef, samples[begin + 1].timestamp, sessionElapsed) < m_axisX->min())
            ++begin;
        rangeEnd = begin;
        while (rangeEnd < boundedEnd &&
               chartXSeconds(tRef, samples[rangeEnd].timestamp, sessionElapsed) <= m_axisX->max())
            ++rangeEnd;
        rangeEnd = std::min(boundedEnd, rangeEnd + 1);
    }
    const auto plotIdx = ChartSamples::select(samples, begin, rangeEnd, chartPointBudget(), m_metricEnabled);
    buildChartFromSampleIndices(samples, plotIdx, boundedEnd, begin, rangeEnd);
}

void DashboardPage::buildChartFromSampleIndices(const std::vector<FlightSample> &samples,
                                                const std::vector<int> &sampleIndices, int logicalSampleCount,
                                                int begin, int end) {
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

    for (auto *axis : m_traceAxesX) {
        axis->setTitleText({});
    }
    std::vector<PlotSample> hoverSamples;
    for (int i = begin; i < end; ++i) {
        const double x = xForIndex(i);
        if (std::isfinite(x)) hoverSamples.push_back({x, i});
    }
    std::stable_sort(hoverSamples.begin(), hoverSamples.end(),
                     [](const PlotSample &a, const PlotSample &b) { return a.x < b.x; });
    m_hoverSampleIndexMap.clear();
    QVector<double> hoverXValues;
    for (const auto &point : hoverSamples) {
        m_hoverSampleIndexMap.push_back(point.sampleIndex);
        hoverXValues.append(point.x);
    }
    m_chartHoverXValues = hoverXValues;
    m_hoverLogicalSampleCount = std::max(0, logicalSampleCount);
    for (auto *view : m_traceViews)
        view->setHoverXValues(hoverXValues);
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

        for (auto *view : m_traceViews)
            view->invalidateHoverSeriesCache();
        return;
    }

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
        QList<QPointF> pts;
        pts.reserve(plotN);
        bool firstSegment = true;
        const auto flushSegment = [&]() {
            if (pts.isEmpty()) return;
            auto *segment = series;
            if (!firstSegment) {
                segment = std::make_unique<QLineSeries>().release();
                segment->setProperty("metricIndex", mi);
                segment->setProperty("gapSegment", true);
                segment->setPen(series->pen());
                series->chart()->addSeries(segment);
                for (auto *axis : series->attachedAxes())
                    segment->attachAxis(axis);
            }
            segment->replace(pts);
            segment->setVisible(true);
            applySeriesPointDisplay(segment, static_cast<int>(plotSamples.size()));
            firstSegment = false;
            pts.clear();
        };
        series->clear();
        series->setVisible(false);
        for (const PlotSample &plotSample : plotSamples) {
            const auto &sample = samples[plotSample.sampleIndex];
            const double y = metricDisplayValue(mi, sampleValueForMetric(sample, mi), m_imperialUnits);
            if (!std::isfinite(y) || !hasFiniteRange) {
                flushSegment();
                continue;
            }
            pts.append(QPointF(plotSample.x, y));
        }
        flushSegment();
        series->setName(metricDisplayTitle(mi, m_imperialUnits));
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
            axis->applyNiceNumbers();
        } else {
            axis->setRange(-1.0, 1.0);
        }
    };

    int slot = 0;
    for (int mi = 0; mi < kMetricCount; ++mi) {
        if (!m_metricEnabled[mi]) continue;
        auto *axis = m_traceAxesY[mi];
        axis->setTitleText(metricDisplayUnitShort(mi, m_imperialUnits));
        axis->setLabelsColor(metricColor(mi));
        axis->setTitleBrush(metricColor(mi));
        m_traceCharts[mi]->setTitle(metricDisplayTitle(mi, m_imperialUnits));
        m_traceCharts[mi]->setTitleBrush(metricColor(mi));
        if (!m_preserveChartAxes) applyFiniteAxisRange(axis, mi);
        ++slot;
    }
    if (slot == 0) {
        m_chart->setTitle(tr("Select a trace to inspect"));
        m_axisY->setTitleText({});
        m_axisY->setRange(-1, 1);
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
    for (auto *view : m_traceViews)
        view->invalidateHoverSeriesCache();
    if (m_comparisonActive && countEnabledMetrics() == 2) {
        rebuildComparisonChart();
    }
}

void DashboardPage::scheduleLiveChartRebuild() {
    m_liveChartDirty = true;
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

    // Toolbar zoom changes time only; each trace keeps its readable measurement scale.
    m_preserveChartAxes = true;
    if (m_followToggle) m_followToggle->setChecked(false);
    if (m_model && m_model->replayMode())
        scheduleReplayChartRebuild();
    else
        scheduleLiveChartRebuild();
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
    if (m_comparisonActive && countEnabledMetrics() == 2) {
        rebuildComparisonChart();
    }
}

void DashboardPage::clearEventMarkers() {
    for (auto *s : m_markerSeries) {
        if (m_chart) m_chart->removeSeries(s);
        delete s;
    }
    m_markerSeries.clear();
    m_eventMarkers.clear();
    if (m_chartView) m_chartView->invalidateHoverSeriesCache();
    if (m_comparisonActive && countEnabledMetrics() == 2) {
        rebuildComparisonChart();
    }
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

void DashboardPage::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    updateCompactToolbar();
}

void DashboardPage::paintEvent(QPaintEvent *event) {
    QPainter painter(this);
    painter.setClipRegion(event->region());

    painter.fillRect(rect(), QColor(Theme::kBgBase()));

}
