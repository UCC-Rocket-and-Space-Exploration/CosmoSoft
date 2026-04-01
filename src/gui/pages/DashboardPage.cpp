#include "gui/pages/DashboardPage.h"

#include "domain/FlightSession.h"
#include "gui/FlightDataModel.h"
#include "gui/FlightReplayController.h"

#include <QChart>
#include <QCheckBox>
#include <QColor>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFont>
#include <QFrame>
#include <QGroupBox>
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
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QVBoxLayout>
#include <QtCharts/QAbstractSeries>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

using namespace Qt::StringLiterals;

namespace {

constexpr int kMetricCount = DashboardPage::kMetricCount;

// Values ≥ this magnitude (ms) are treated as Unix epoch ms; chart uses elapsed time from the first sample.
constexpr long long kUnixEpochMsThreshold = 100'000'000'000LL;

[[nodiscard]] bool timestampLooksLikeUnixMs(long tMs) {
    return std::fabs(static_cast<double>(tMs)) >= static_cast<double>(kUnixEpochMsThreshold);
}

[[nodiscard]] bool useSessionElapsedTimeAxis(long tFirstMs, long tLastMs) {
    return timestampLooksLikeUnixMs(tFirstMs) || timestampLooksLikeUnixMs(tLastMs);
}

[[nodiscard]] double chartXSeconds(long refFirstMs, long tMs, bool sessionElapsed) {
    if (sessionElapsed) {
        return static_cast<double>(tMs - refFirstMs) / 1000.0;
    }
    return static_cast<double>(tMs) / 1000.0;
}

QString metricTitle(int idx) {
    static const QString titles[] = {
        u"Altitude (m)"_s,
        u"Temperature (°C)"_s,
        u"Pressure"_s,
        u"|Acceleration| (m/s²)"_s,
        u"Battery (V)"_s,
        u"RSSI"_s,
        u"|Gyro| (rad/s)"_s,
        u"Latitude (°)"_s,
        u"Longitude (°)"_s,
    };
    if (idx < 0 || idx >= kMetricCount) {
        return {};
    }
    return titles[idx];
}

QString metricTraceShortName(int idx) {
    static const QString names[] = {
        u"Alt"_s,
        u"Temp"_s,
        u"Press"_s,
        u"|a|"_s,
        u"Batt"_s,
        u"RSSI"_s,
        u"Gyro"_s,
        u"Lat"_s,
        u"Lon"_s,
    };
    if (idx < 0 || idx >= kMetricCount) {
        return {};
    }
    return names[idx];
}

QColor metricColor(int idx) {
    static const QColor colors[] = {
        QColor("#5b9bd5"),
        QColor("#70c1a5"),
        QColor("#f0b429"),
        QColor("#c084fc"),
        QColor("#7dd36f"),
        QColor("#67b8ff"),
        QColor("#ff9f6b"),
        QColor("#8ec5ff"),
        QColor("#f5a3b8"),
    };
    return colors[(idx + kMetricCount * 10) % kMetricCount];
}

double sampleValueForMetric(const FlightSample &s, int idx) {
    switch (idx) {
    case 0:
        return s.altitude;
    case 1:
        return s.temperature;
    case 2:
        return s.pressure;
    case 3: {
        const double x = s.acceleration.x;
        const double y = s.acceleration.y;
        const double z = s.acceleration.z;
        return std::sqrt(x * x + y * y + z * z);
    }
    case 4:
        return s.batteryVoltage;
    case 5:
        return s.rssi;
    case 6: {
        const double x = s.angularVelocity.x;
        const double y = s.angularVelocity.y;
        const double z = s.angularVelocity.z;
        return std::sqrt(x * x + y * y + z * z);
    }
    case 7:
        return s.coordinates.latitude;
    case 8:
        return s.coordinates.longitude;
    default:
        return 0;
    }
}

QString metricQuantityName(int idx) {
    static const QString names[] = {
        u"Altitude"_s,
        u"Temperature"_s,
        u"Pressure"_s,
        u"|a|"_s,
        u"Battery"_s,
        u"RSSI"_s,
        u"|ω|"_s,
        u"Latitude"_s,
        u"Longitude"_s,
    };
    if (idx < 0 || idx >= kMetricCount) {
        return {};
    }
    return names[idx];
}

QString metricAxisUnitShort(int idx) {
    static const QString units[] = {
        u"m"_s,
        u"°C"_s,
        u"(Pa)"_s,
        u"m/s²"_s,
        u"V"_s,
        u"dBm"_s,
        u"rad/s"_s,
        u"°"_s,
        u"°"_s,
    };
    if (idx < 0 || idx >= kMetricCount) {
        return {};
    }
    return units[idx];
}

QString formatMetricValuePretty(int idx, double v) {
    switch (idx) {
    case 7:
    case 8:
        return QString::number(v, 'f', 6);
    case 0:
    case 1:
    case 4:
        return QString::number(v, 'f', 2);
    case 5:
        return QString::number(v, 'f', 1);
    default:
        return QString::number(v, 'g', 6);
    }
}

int nearestIndexByX(const QList<QPointF> &pts, double tx) {
    const int n = pts.size();
    if (n <= 0) {
        return -1;
    }
    if (n == 1) {
        return 0;
    }
    int lo = 0;
    int hi = n - 1;
    while (lo < hi - 1) {
        const int mid = (lo + hi) / 2;
        if (pts[mid].x() <= tx) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    const double dLo = std::abs(pts[lo].x() - tx);
    const double dHi = std::abs(pts[hi].x() - tx);
    return dLo <= dHi ? lo : hi;
}

class TelemetryChartView : public QChartView {
public:
    std::function<void(const QString &)> hoverReadout;
    std::function<QString(double tSec, int sampleIndex1Based, int totalSamples)> hoverDetail;

    explicit TelemetryChartView(QChart *c, QWidget *parent = nullptr)
        : QChartView(c, parent),
          m_chartPtr(c) {
        setRubberBand(QChartView::RectangleRubberBand);
        setRenderHint(QPainter::Antialiasing);
        setMinimumHeight(420);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMouseTracking(true);
    }

    void setChart(QChart *c) { m_chartPtr = c; }

protected:
    void mouseMoveEvent(QMouseEvent *event) override {
        QChartView::mouseMoveEvent(event);
        if (!hoverReadout || m_chartPtr == nullptr) {
            return;
        }
        QLineSeries *ref = nullptr;
        for (QAbstractSeries *s : m_chartPtr->series()) {
            auto *ls = qobject_cast<QLineSeries *>(s);
            if (ls && ls->isVisible() && !ls->points().isEmpty()) {
                ref = ls;
                break;
            }
        }
        if (!ref) {
            hoverReadout(u"—"_s);
            return;
        }
        const QList<QPointF> pts = ref->points();
        const QPointF scenePos = mapToScene(event->pos());
        const QPointF chartPos = m_chartPtr->mapFromScene(scenePos);
        const QPointF plotVals = m_chartPtr->mapToValue(chartPos, ref);
        const int idx = nearestIndexByX(pts, plotVals.x());
        if (idx < 0) {
            return;
        }
        const QPointF &p = pts[idx];
        if (hoverDetail) {
            hoverReadout(hoverDetail(p.x(), idx + 1, pts.size()));
        } else {
            hoverReadout(QStringLiteral("t=%1 s · #%2 / %3")
                             .arg(p.x(), 0, 'f', 3)
                             .arg(idx + 1)
                             .arg(pts.size()));
        }
    }

    void leaveEvent(QEvent *event) override {
        if (hoverReadout) {
            hoverReadout({});
        }
        QChartView::leaveEvent(event);
    }

private:
    QChart *m_chartPtr = nullptr;
};

QFrame *createStatTile(const QString &label, const QString &value, QWidget *parent, QLabel **valueLabelOut) {
    auto *tile = new QFrame(parent);
    tile->setProperty("kind", u"statTile"_s);
    auto *layout = new QVBoxLayout(tile);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setSpacing(2);

    auto *title = new QLabel(label, tile);
    title->setProperty("kind", u"statLabel"_s);
    layout->addWidget(title);

    auto *valueLabel = new QLabel(value, tile);
    valueLabel->setProperty("kind", u"statValue"_s);
    layout->addWidget(valueLabel);
    if (valueLabelOut) {
        *valueLabelOut = valueLabel;
    }

    return tile;
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
        QFrame[kind="statTile"] {
            background-color: rgba(21, 22, 25, 0.88);
            border: 1px solid #3b3b45;
            border-radius: 8px;
        }
        QLabel[kind="statLabel"] {
            font-size: 12px;
            text-transform: uppercase;
            color: #9aa7b8;
            letter-spacing: 1px;
        }
        QLabel[kind="statValue"] {
            font-size: 20px;
            font-weight: 700;
            color: #f0f0f0;
        }
        QFrame#chartFrame {
            background-color: rgba(21, 22, 25, 0.80);
            border: 1px solid #3b3b45;
            border-radius: 8px;
            padding: 4px;
        }
        QFrame#chartWrapper {
            background-color: #0a0c10;
            border: 1px solid #2a2d36;
            border-radius: 8px;
            padding: 2px;
        }
        QFrame#tracesPanel {
            background-color: rgba(14, 15, 18, 0.95);
            border: 1px solid #3b3b45;
            border-radius: 8px;
        }
        QGroupBox#traceSelectorGroup {
            border: 1px solid #3b3b45;
            border-radius: 8px;
            margin-top: 6px;
            padding: 6px 4px 8px 4px;
            font-weight: 600;
            color: #a8b4c0;
            font-size: 11px;
            background-color: transparent;
        }
        QGroupBox#traceSelectorGroup::title {
            subcontrol-origin: margin;
            left: 8px;
            padding: 0 3px;
        }
        QScrollArea#traceScroll {
            background: transparent;
            border: none;
        }
        QCheckBox#traceCheck {
            color: #d8dee8;
            spacing: 6px;
            font-size: 11px;
        }
        QCheckBox#chartOptionCheck {
            color: #c4ccd6;
            font-size: 11px;
            spacing: 6px;
        }
        QCheckBox#traceCheck::indicator {
            width: 16px;
            height: 16px;
        }
        QFrame#replayBar {
            background-color: rgba(21, 22, 25, 0.88);
            border: 1px solid #3b3b45;
            border-radius: 8px;
            padding: 8px;
        }
        QPushButton#replayBtn {
            border: 1px solid #6a6a6a;
            border-radius: 4px;
            padding: 6px 14px;
            background-color: #3d3f47;
            color: #f0f0f0;
        }
        QPushButton#replayJumpBtn {
            border: 1px solid #5a5a62;
            border-radius: 4px;
            padding: 6px 10px;
            background-color: #2e3038;
            color: #d0d8e0;
            font-size: 11px;
        }
        QLabel#replayInfoLabel {
            color: #b8c4d0;
            font-size: 12px;
            padding: 4px 2px 0 2px;
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
            padding: 6px 12px;
            background-color: #3d3f47;
            color: #f0f0f0;
        }
        QLabel#chartHoverReadout {
            color: #c8d4e0;
            font-size: 11px;
            padding: 6px 8px;
            background-color: rgba(8, 9, 12, 0.92);
            border: 1px solid #3b3b45;
            border-radius: 6px;
        }
        QLabel#chartStatsLabel {
            color: #8fa0b0;
            font-size: 11px;
        }
    )"_s);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setSpacing(10);
    rootLayout->setContentsMargins(12, 12, 12, 12);

    auto *replayBar = new QFrame(this);
    replayBar->setObjectName(u"replayBar"_s);
    auto *replayOuter = new QVBoxLayout(replayBar);
    replayOuter->setContentsMargins(8, 8, 8, 8);
    replayOuter->setSpacing(6);

    auto *replayRow = new QHBoxLayout();
    replayRow->setSpacing(8);

    m_playBtn = new QPushButton(u"Play"_s, replayBar);
    m_playBtn->setObjectName(u"replayBtn"_s);
    m_playBtn->setToolTip(u"Play forward from the scrubber position"_s);
    m_pauseBtn = new QPushButton(u"Pause"_s, replayBar);
    m_pauseBtn->setObjectName(u"replayBtn"_s);
    m_stopBtn = new QPushButton(u"Stop"_s, replayBar);
    m_stopBtn->setObjectName(u"replayBtn"_s);
    m_stopBtn->setToolTip(u"Stop and jump scrubber to the beginning"_s);

    m_jumpStartBtn = new QPushButton(u"|◀ Start"_s, replayBar);
    m_jumpStartBtn->setObjectName(u"replayJumpBtn"_s);
    m_jumpStartBtn->setToolTip(u"Show from first sample (scrubber left)"_s);
    m_jumpEndBtn = new QPushButton(u"End ▶|"_s, replayBar);
    m_jumpEndBtn->setObjectName(u"replayJumpBtn"_s);
    m_jumpEndBtn->setToolTip(u"Show through last sample"_s);

    replayRow->addWidget(m_playBtn);
    replayRow->addWidget(m_pauseBtn);
    replayRow->addWidget(m_stopBtn);
    replayRow->addWidget(m_jumpStartBtn);
    replayRow->addWidget(m_jumpEndBtn);

    m_replaySlider = new QSlider(Qt::Horizontal, replayBar);
    m_replaySlider->setRange(0, 0);
    m_replaySlider->setEnabled(false);
    m_replaySlider->setSingleStep(1);
    m_replaySlider->setPageStep(10);
    m_replaySlider->setToolTip(u"How many samples are visible on the chart (prefix of the log)"_s);
    replayRow->addWidget(m_replaySlider, 1);

    m_speedSpin = new QDoubleSpinBox(replayBar);
    m_speedSpin->setRange(0.25, 4.0);
    m_speedSpin->setSingleStep(0.25);
    m_speedSpin->setValue(1.0);
    m_speedSpin->setPrefix(u"Speed "_s);
    m_speedSpin->setSuffix(u"x"_s);
    m_speedSpin->setToolTip(u"Replay clock multiplier (uses timestamps between samples)"_s);
    replayRow->addWidget(m_speedSpin);

    replayOuter->addLayout(replayRow);

    m_replayInfoLabel = new QLabel(replayBar);
    m_replayInfoLabel->setObjectName(u"replayInfoLabel"_s);
    m_replayInfoLabel->setWordWrap(true);
    m_replayActivityText = u"Ready"_s;
    replayOuter->addWidget(m_replayInfoLabel);

    if (m_replay) {
        connect(m_playBtn, &QPushButton::clicked, m_replay, &FlightReplayController::play);
        connect(m_pauseBtn, &QPushButton::clicked, m_replay, &FlightReplayController::pause);
        connect(m_stopBtn, &QPushButton::clicked, m_replay, &FlightReplayController::stop);
        connect(m_speedSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), m_replay, &FlightReplayController::setSpeed);
        connect(m_speedSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) {
            updateReplayPanel();
        });
        connect(m_replaySlider, &QSlider::valueChanged, m_replay, &FlightReplayController::setPosition);
        connect(m_jumpStartBtn, &QPushButton::clicked, this, [this]() {
            if (m_replaySlider) {
                m_replaySlider->setValue(0);
            }
        });
        connect(m_jumpEndBtn, &QPushButton::clicked, this, [this]() {
            if (m_replaySlider) {
                m_replaySlider->setValue(m_replaySlider->maximum());
            }
        });
        connect(m_replay, &FlightReplayController::positionChanged, this, [this](int len) {
            if (m_replaySlider) {
                m_replaySlider->blockSignals(true);
                m_replaySlider->setValue(len);
                m_replaySlider->blockSignals(false);
            }
            setReplayTrailLength(len);
        });
        connect(m_replay, &FlightReplayController::playbackStarted, this, [this]() {
            m_replayActivityText = u"Playing"_s;
            updateReplayPanel();
        });
        connect(m_replay, &FlightReplayController::playbackPaused, this, [this]() {
            m_replayActivityText = u"Paused"_s;
            updateReplayPanel();
        });
        connect(m_replay, &FlightReplayController::playbackStopped, this, [this]() {
            m_replayActivityText = u"Stopped"_s;
            updateReplayPanel();
        });
        connect(m_replay, &FlightReplayController::playbackFinished, this, [this]() {
            m_replayActivityText = u"Finished"_s;
            updateReplayPanel();
        });
        connect(m_replay, &FlightReplayController::errorOccurred, this, [this](const QString &msg) {
            m_replayActivityText = msg;
            updateReplayPanel();
        });
    }

    auto *statsRowWidget = new QWidget(this);
    auto *statsLayout = new QHBoxLayout(statsRowWidget);
    statsLayout->setContentsMargins(0, 0, 0, 0);
    statsLayout->setSpacing(12);
    statsLayout->addWidget(createStatTile(u"|ACCEL|"_s, u"—"_s, statsRowWidget, &m_velValue), 1);
    statsLayout->addWidget(createStatTile(u"ALTITUDE"_s, u"—"_s, statsRowWidget, &m_altValue), 1);
    statsLayout->addWidget(createStatTile(u"TEMP"_s, u"—"_s, statsRowWidget, &m_tempValue), 1);
    statsLayout->addWidget(createStatTile(u"PRESSURE"_s, u"—"_s, statsRowWidget, &m_pressValue), 1);
    rootLayout->addWidget(statsRowWidget);

    auto *contentLayout = new QHBoxLayout();
    contentLayout->setSpacing(6);
    contentLayout->setContentsMargins(0, 0, 0, 0);

    auto *tracesPanel = new QFrame(this);
    tracesPanel->setObjectName(u"tracesPanel"_s);
    tracesPanel->setFixedWidth(168);
    auto *tracesOuter = new QVBoxLayout(tracesPanel);
    tracesOuter->setContentsMargins(8, 8, 8, 8);
    tracesOuter->setSpacing(6);

    auto *traceGroup = new QGroupBox(u"Traces"_s, tracesPanel);
    traceGroup->setObjectName(u"traceSelectorGroup"_s);
    auto *traceGroupLay = new QVBoxLayout(traceGroup);
    traceGroupLay->setContentsMargins(4, 10, 4, 6);
    traceGroupLay->setSpacing(4);

    auto *tracesHint = new QLabel(
        u"2+ traces: Y normalized · hover = SI values"_s,
        traceGroup);
    tracesHint->setWordWrap(true);
    tracesHint->setStyleSheet(u"color: #7a8796; font-size: 10px;"_s);
    traceGroupLay->addWidget(tracesHint);

    auto *traceScroll = new QScrollArea(traceGroup);
    traceScroll->setObjectName(u"traceScroll"_s);
    traceScroll->setWidgetResizable(true);
    traceScroll->setFrameShape(QFrame::NoFrame);
    traceScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    traceScroll->setMinimumHeight(140);
    traceScroll->setMaximumHeight(320);

    auto *traceScrollInner = new QWidget(traceScroll);
    traceScrollInner->setObjectName(u"traceScrollInner"_s);
    auto *traceListLay = new QVBoxLayout(traceScrollInner);
    traceListLay->setContentsMargins(0, 0, 2, 0);
    traceListLay->setSpacing(3);

    for (int i = 0; i < kMetricCount; ++i) {
        auto *row = new QWidget(traceScrollInner);
        auto *rowLay = new QHBoxLayout(row);
        rowLay->setContentsMargins(0, 0, 0, 0);
        rowLay->setSpacing(6);
        const QColor col = metricColor(i);
        auto *swatch = new QLabel(row);
        swatch->setFixedSize(10, 10);
        swatch->setStyleSheet(
            QStringLiteral("QLabel { background-color: %1; border-radius: 5px; min-width:10px; min-height:10px; }")
                .arg(col.name(QColor::HexRgb)));
        auto *cb = new QCheckBox(metricTraceShortName(i), row);
        cb->setObjectName(u"traceCheck"_s);
        cb->setToolTip(
            QStringLiteral("%1 — same color as on chart").arg(metricTitle(i)));
        m_metricChecks[static_cast<std::size_t>(i)] = cb;
        rowLay->addWidget(swatch, 0, Qt::AlignVCenter);
        rowLay->addWidget(cb, 1, Qt::AlignVCenter);
        traceListLay->addWidget(row);
        connect(cb, &QCheckBox::toggled, this, &DashboardPage::onAnyMetricToggled);
    }
    traceScroll->setWidget(traceScrollInner);
    traceGroupLay->addWidget(traceScroll, 1);

    m_metricEnabled = {true, true, true, false, false, false, false, false, false};
    syncCheckboxStatesFromFlags();

    m_tracePresetBtn = new QPushButton(u"Alt · Temp · Press"_s, traceGroup);
    m_tracePresetBtn->setObjectName(u"chartToolbarBtn"_s);
    m_tracePresetBtn->setToolTip(u"Enable only altitude, temperature, and pressure"_s);
    connect(m_tracePresetBtn, &QPushButton::clicked, this, &DashboardPage::onTracePresetAltTempPress);
    traceGroupLay->addWidget(m_tracePresetBtn);

    tracesOuter->addWidget(traceGroup, 1);

    auto *chartColumn = new QVBoxLayout();
    chartColumn->setSpacing(6);
    chartColumn->setContentsMargins(0, 0, 0, 0);

    auto *chartFrame = new QFrame(this);
    chartFrame->setObjectName(u"chartFrame"_s);
    auto *chartFrameLayout = new QVBoxLayout(chartFrame);
    chartFrameLayout->setContentsMargins(6, 6, 6, 6);
    chartFrameLayout->setSpacing(6);

    auto *chartToolbar = new QHBoxLayout();
    chartToolbar->setSpacing(8);
    m_zoomResetBtn = new QPushButton(u"Reset zoom"_s, chartFrame);
    m_zoomResetBtn->setObjectName(u"chartToolbarBtn"_s);
    m_zoomResetBtn->setToolTip(u"Fit full time range"_s);
    chartToolbar->addWidget(m_zoomResetBtn);

    m_showMarkersCheck = new QCheckBox(u"Markers"_s, chartFrame);
    m_showMarkersCheck->setObjectName(u"chartOptionCheck"_s);
    m_showMarkersCheck->setChecked(true);
    m_showMarkersCheck->setToolTip(
        u"Draw a dot on each sample (auto-disabled above 400 points/trace for clarity)."_s);
    chartToolbar->addWidget(m_showMarkersCheck);

    m_showPointValuesCheck = new QCheckBox(u"Point values"_s, chartFrame);
    m_showPointValuesCheck->setObjectName(u"chartOptionCheck"_s);
    m_showPointValuesCheck->setChecked(false);
    m_showPointValuesCheck->setToolTip(
        u"Show Y value next to each point — single trace only, max 100 points (engineering Y, not normalized)."_s);
    chartToolbar->addWidget(m_showPointValuesCheck);

    connect(m_showMarkersCheck, &QCheckBox::toggled, this, &DashboardPage::onChartVisualOptionsToggled);
    connect(m_showPointValuesCheck, &QCheckBox::toggled, this, &DashboardPage::onChartVisualOptionsToggled);

    chartToolbar->addStretch(1);
    connect(m_zoomResetBtn, &QPushButton::clicked, this, &DashboardPage::onResetChartZoom);
    chartFrameLayout->addLayout(chartToolbar);

    m_hoverReadoutLabel = new QLabel(chartFrame);
    m_hoverReadoutLabel->setObjectName(u"chartHoverReadout"_s);
    m_hoverReadoutLabel->setWordWrap(true);
    m_hoverReadoutLabel->setMinimumHeight(48);
    updateHoverReadoutDefault();
    chartFrameLayout->addWidget(m_hoverReadoutLabel);

    m_chartStatsLabel = new QLabel(chartFrame);
    m_chartStatsLabel->setObjectName(u"chartStatsLabel"_s);
    chartFrameLayout->addWidget(m_chartStatsLabel);

    auto *chartWrapper = new QFrame(chartFrame);
    chartWrapper->setObjectName(u"chartWrapper"_s);
    auto *chartWrapperLayout = new QVBoxLayout(chartWrapper);
    chartWrapperLayout->setContentsMargins(2, 2, 2, 2);
    chartWrapperLayout->setSpacing(0);

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

    auto *tcv = new TelemetryChartView(m_chart, chartWrapper);
    m_chartView = tcv;
    tcv->setChart(m_chart);
    tcv->hoverDetail = [this](double tSec, int sampleIndex1Based, int totalSamples) {
        return formatMultiMetricHover(tSec, sampleIndex1Based, totalSamples);
    };
    tcv->hoverReadout = [this](const QString &s) {
        if (!m_hoverReadoutLabel) {
            return;
        }
        if (s.isEmpty()) {
            updateHoverReadoutDefault();
        } else if (s == u"—"_s) {
            m_hoverReadoutLabel->setText(u"No traces — enable metrics on the left or load data."_s);
        } else {
            m_hoverReadoutLabel->setText(s);
        }
    };

    chartWrapperLayout->addWidget(m_chartView, 1);
    chartFrameLayout->addWidget(chartWrapper, 1);

    chartColumn->addWidget(chartFrame, 1);

    contentLayout->addWidget(tracesPanel, 0);
    contentLayout->addLayout(chartColumn, 1);

    rootLayout->addLayout(contentLayout, 1);
    rootLayout->addWidget(replayBar);

    if (m_model) {
        connect(m_model, &FlightDataModel::sampleUpdated, this, &DashboardPage::onSampleUpdated);
        connect(m_model, &FlightDataModel::sessionReset, this, &DashboardPage::onSessionReset);
        connect(m_model, &FlightDataModel::replayModeChanged, this, [this](bool) {
            updateReplayPanel();
        });
    }

    refreshAllSeriesFromData();
    updateChartStatsLabel();
    updateReplayPanel();
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
    m_chart->setMargins(QMargins(6, 4, 6, 8));
}

void DashboardPage::syncCheckboxStatesFromFlags() {
    for (int i = 0; i < kMetricCount; ++i) {
        if (m_metricChecks[static_cast<std::size_t>(i)]) {
            const QSignalBlocker b(m_metricChecks[static_cast<std::size_t>(i)]);
            m_metricChecks[static_cast<std::size_t>(i)]->setChecked(m_metricEnabled[static_cast<std::size_t>(i)]);
        }
    }
}

void DashboardPage::ensureAtLeastOneMetricEnabled() {
    int n = 0;
    for (int i = 0; i < kMetricCount; ++i) {
        if (m_metricEnabled[static_cast<std::size_t>(i)]) {
            ++n;
        }
    }
    if (n == 0) {
        m_metricEnabled[0] = true;
        syncCheckboxStatesFromFlags();
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

void DashboardPage::onAnyMetricToggled() {
    for (int i = 0; i < kMetricCount; ++i) {
        if (m_metricChecks[static_cast<std::size_t>(i)]) {
            m_metricEnabled[static_cast<std::size_t>(i)] = m_metricChecks[static_cast<std::size_t>(i)]->isChecked();
        }
    }
    ensureAtLeastOneMetricEnabled();
    refreshAllSeriesFromData();
    if (m_chart) {
        m_chart->zoomReset();
    }
    updateChartStatsLabel();
}

void DashboardPage::onTracePresetAltTempPress() {
    m_metricEnabled.fill(false);
    m_metricEnabled[0] = true;
    m_metricEnabled[1] = true;
    m_metricEnabled[2] = true;
    syncCheckboxStatesFromFlags();
    refreshAllSeriesFromData();
    if (m_chart) {
        m_chart->zoomReset();
    }
    updateChartStatsLabel();
}

void DashboardPage::updateHoverReadoutDefault() {
    if (m_hoverReadoutLabel) {
        m_hoverReadoutLabel->setText(
            u"Hover a point: flight/session time, sample #, and values for every enabled trace (engineering units). Drag to zoom."_s);
    }
}

void DashboardPage::onResetChartZoom() {
    if (m_chart) {
        m_chart->zoomReset();
    }
}

void DashboardPage::onChartVisualOptionsToggled() {
    refreshAllSeriesFromData();
    updateChartStatsLabel();
}

void DashboardPage::applySeriesPointDisplay(QLineSeries *series, int pointCount, int nEnabledMetrics) const {
    if (!series) {
        return;
    }

    const bool markersOn = !m_showMarkersCheck || m_showMarkersCheck->isChecked();
    const bool showVertices = markersOn && pointCount > 0 && pointCount <= 400;
    series->setPointsVisible(showVertices);

    const bool wantLabels = m_showPointValuesCheck && m_showPointValuesCheck->isChecked();
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
    if (m_showMarkersCheck) {
        opts += m_showMarkersCheck->isChecked() ? u" · markers ≤400"_s : u" · markers off"_s;
    }
    if (m_showPointValuesCheck && m_showPointValuesCheck->isChecked()) {
        opts += u" · point labels ≤100 (1 trace)"_s;
    }
    m_chartStatsLabel->setText(
        QStringLiteral("%1 · %2 traces · up to %3 points%4%5")
            .arg(mode)
            .arg(nTr)
            .arg(maxPts)
            .arg(norm)
            .arg(opts));
}

QString DashboardPage::formatMultiMetricHover(double tSec, int sampleIndex, int totalSamples) const {
    if (sampleIndex < 1 || sampleIndex > totalSamples || totalSamples <= 0) {
        return {};
    }
    const int i = sampleIndex - 1;
    const FlightSample *sp = nullptr;
    if (m_model && m_model->replayMode()) {
        if (m_session && i >= 0 && i < static_cast<int>(m_session->samples.size())) {
            sp = &m_session->samples[static_cast<std::size_t>(i)];
        }
    } else {
        if (i >= 0 && i < static_cast<int>(m_liveSamples.size())) {
            sp = &m_liveSamples[static_cast<std::size_t>(i)];
        }
    }
    if (!sp) {
        return {};
    }
    QStringList lines;
    lines << QStringLiteral("Time %1 s  ·  row %2 / %3")
                 .arg(tSec, 0, 'f', 3)
                 .arg(sampleIndex)
                 .arg(totalSamples);
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

void DashboardPage::updateReplayPanel() {
    if (!m_replayInfoLabel) {
        return;
    }
    if (!m_model) {
        m_replayInfoLabel->setText(u""_s);
        return;
    }
    if (!m_model->replayMode()) {
        const int n = static_cast<int>(m_liveSamples.size());
        m_replayInfoLabel->setText(
            QStringLiteral("Live · %1 samples buffered · connect serial on Monitoring").arg(n));
        if (m_jumpStartBtn) {
            m_jumpStartBtn->setEnabled(false);
            m_jumpEndBtn->setEnabled(false);
        }
        return;
    }

    if (!m_session || m_session->samples.empty()) {
        m_replayInfoLabel->setText(
            u"Replay · open a flight log from the connection bar (or switch to Monitoring for live data)."_s);
        if (m_jumpStartBtn) {
            m_jumpStartBtn->setEnabled(false);
            m_jumpEndBtn->setEnabled(false);
        }
        return;
    }

    const auto &samples = m_session->samples;
    const int n = static_cast<int>(samples.size());
    const int head = std::clamp(m_lastReplayTrailLength, 0, n);
    const long tRef = samples.front().timestamp;
    const bool sessionElapsed = useSessionElapsedTimeAxis(samples.front().timestamp, samples.back().timestamp);
    const double tLogStart = chartXSeconds(tRef, samples.front().timestamp, sessionElapsed);
    const double tLogEnd = chartXSeconds(tRef, samples.back().timestamp, sessionElapsed);

    if (m_jumpStartBtn) {
        m_jumpStartBtn->setEnabled(n > 0);
        m_jumpEndBtn->setEnabled(n > 0);
    }

    const QString speedStr =
        m_speedSpin ? QStringLiteral("%1×").arg(m_speedSpin->value(), 0, 'f', 2) : u"1×"_s;
    const QString line1 =
        QStringLiteral("%1 · speed %2").arg(m_replayActivityText.isEmpty() ? u"Ready"_s : m_replayActivityText).arg(speedStr);

    QString line2;
    if (head <= 0) {
        line2 = u"Scrubber at 0 — move right to show samples on the chart."_s;
    } else {
        const double tVisEnd =
            chartXSeconds(tRef, samples[static_cast<std::size_t>(head - 1)].timestamp, sessionElapsed);
        line2 = QStringLiteral(
                   "Showing samples 1–%1 of %2 · chart %3 → %4 s · full log %5 → %6 s")
                    .arg(head)
                    .arg(n)
                    .arg(tLogStart, 0, 'f', 2)
                    .arg(tVisEnd, 0, 'f', 2)
                    .arg(tLogStart, 0, 'f', 2)
                    .arg(tLogEnd, 0, 'f', 2);
    }

    m_replayInfoLabel->setText(line1 + u"\n"_s + line2);
}

void DashboardPage::setReplaySession(const FlightSession *session) {
    m_session = session;
    const int n = session ? static_cast<int>(session->samples.size()) : 0;
    if (m_replaySlider) {
        m_replaySlider->setMaximum(std::max(0, n));
        m_replaySlider->setEnabled(n > 0);
    }
    m_replayActivityText = n > 0 ? u"Ready"_s : u"Idle"_s;
    if (n > 0) {
        m_lastReplayTrailLength = n;
        if (m_replaySlider) {
            const QSignalBlocker blocker(m_replaySlider);
            m_replaySlider->setValue(n);
        }
        rebuildReplayCharts(n);
        if (m_replay) {
            m_replay->setPosition(n);
        }
    } else {
        m_lastReplayTrailLength = 0;
        if (m_replaySlider) {
            const QSignalBlocker blocker(m_replaySlider);
            m_replaySlider->setValue(0);
        }
        rebuildReplayCharts(0);
    }
    updateChartStatsLabel();
    updateReplayPanel();
}

void DashboardPage::setReplayTrailLength(int trailLength) {
    m_lastReplayTrailLength = trailLength;
    rebuildReplayCharts(trailLength);
}

void DashboardPage::refreshAllSeriesFromData() {
    if (m_model && m_model->replayMode() && m_session && !m_session->samples.empty()) {
        rebuildReplayCharts(m_lastReplayTrailLength);
    } else {
        rebuildLiveSeriesFromHistory();
    }
}

void DashboardPage::rebuildReplayCharts(int trailLength) {
    ensureAtLeastOneMetricEnabled();
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
        m_axisX->setRange(0, 10);
        m_axisY->setRange(-1, 1);
        m_chart->setTitle(u"Flight data"_s);
        updateChartStatsLabel();
        updateReplayPanel();
        return;
    }

    const auto &samples = m_session->samples;
    const int n = static_cast<int>(samples.size());
    const int end = std::min(trailLength, n);
    if (end <= 0) {
        updateChartStatsLabel();
        updateReplayPanel();
        return;
    }

    const int nEn = countEnabledMetrics();
    const long tRef = samples.front().timestamp;
    const bool sessionElapsed = useSessionElapsedTimeAxis(samples.front().timestamp, samples.back().timestamp);
    m_axisX->setTitleText(sessionElapsed ? u"Session time (s)"_s : u"Flight time (s)"_s);
    double xMin = chartXSeconds(tRef, samples.front().timestamp, sessionElapsed);
    double xMax = chartXSeconds(tRef, samples[static_cast<std::size_t>(end - 1)].timestamp, sessionElapsed);

    std::array<double, kMetricCount> yMin{};
    std::array<double, kMetricCount> yMax{};
    for (int mi = 0; mi < kMetricCount; ++mi) {
        yMin[static_cast<std::size_t>(mi)] = std::numeric_limits<double>::infinity();
        yMax[static_cast<std::size_t>(mi)] = -std::numeric_limits<double>::infinity();
    }

    for (int i = 0; i < end; ++i) {
        const FlightSample &s = samples[static_cast<std::size_t>(i)];
        for (int mi = 0; mi < kMetricCount; ++mi) {
            if (!m_metricEnabled[static_cast<std::size_t>(mi)]) {
                continue;
            }
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

    for (int mi = 0; mi < kMetricCount; ++mi) {
        if (!m_metricEnabled[static_cast<std::size_t>(mi)]) {
            continue;
        }
        auto *series = m_lineSeries[static_cast<std::size_t>(mi)];
        const double lo = yMin[static_cast<std::size_t>(mi)];
        const double hi = yMax[static_cast<std::size_t>(mi)];
        const double span = std::max(hi - lo, 1e-12);

        QList<QPointF> pts;
        pts.reserve(end);
        for (int i = 0; i < end; ++i) {
            const FlightSample &s = samples[static_cast<std::size_t>(i)];
            const double x = chartXSeconds(tRef, s.timestamp, sessionElapsed);
            double y = sampleValueForMetric(s, mi);
            if (nEn > 1) {
                y = (y - lo) / span;
            }
            pts.append(QPointF(x, y));
        }
        series->replace(pts);
        series->setVisible(true);
        applySeriesPointDisplay(series, pts.size(), nEn);
        if (nEn > 1) {
            series->setName(metricTitle(mi) + u" (norm)"_s);
        } else {
            series->setName(metricTitle(mi));
        }
    }

    const double spanX = std::max(xMax - xMin, 1e-9);
    const double xPad = std::max(spanX * 0.02, 0.05);
    m_axisX->setRange(xMin - xPad, xMax + xPad);

    if (nEn == 1 && onlyMi >= 0) {
        const double lo = yMin[static_cast<std::size_t>(onlyMi)];
        const double hi = yMax[static_cast<std::size_t>(onlyMi)];
        const double span = std::max(hi - lo, 1e-9);
        const double p = span * 0.08 + std::max(std::abs(hi) * 1e-6, 1e-3);
        m_axisY->setRange(lo - p, hi + p);
        m_axisY->setTitleText(metricAxisUnitShort(onlyMi));
        m_chart->setTitle(metricTitle(onlyMi));
    } else {
        m_axisY->setRange(-0.05, 1.05);
        m_axisY->setTitleText(u"Normalized"_s);
        m_chart->setTitle(u"Multi-trace overlay"_s);
    }

    applyChartTheme();
    updateChartStatsLabel();
    updateReplayPanel();
}

void DashboardPage::onSessionReset() {
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
    updateChartStatsLabel();
    updateReplayPanel();
}

void DashboardPage::rebuildLiveSeriesFromHistory() {
    ensureAtLeastOneMetricEnabled();
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
        m_axisX->setRange(0, 10);
        m_axisY->setRange(-1, 1);
        m_chart->setTitle(u"Flight data"_s);
        updateChartStatsLabel();
        updateReplayPanel();
        return;
    }

    const int end = static_cast<int>(m_liveSamples.size());
    const long tRef = m_liveSamples.front().timestamp;
    const bool sessionElapsed =
        useSessionElapsedTimeAxis(m_liveSamples.front().timestamp, m_liveSamples.back().timestamp);
    m_axisX->setTitleText(sessionElapsed ? u"Session time (s)"_s : u"Flight time (s)"_s);
    double xMin = chartXSeconds(tRef, m_liveSamples.front().timestamp, sessionElapsed);
    double xMax = chartXSeconds(tRef, m_liveSamples.back().timestamp, sessionElapsed);

    const int nEn = countEnabledMetrics();
    std::array<double, kMetricCount> yMin{};
    std::array<double, kMetricCount> yMax{};
    for (int mi = 0; mi < kMetricCount; ++mi) {
        yMin[static_cast<std::size_t>(mi)] = std::numeric_limits<double>::infinity();
        yMax[static_cast<std::size_t>(mi)] = -std::numeric_limits<double>::infinity();
    }

    for (int i = 0; i < end; ++i) {
        const FlightSample &s = m_liveSamples[static_cast<std::size_t>(i)];
        for (int mi = 0; mi < kMetricCount; ++mi) {
            if (!m_metricEnabled[static_cast<std::size_t>(mi)]) {
                continue;
            }
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

    for (int mi = 0; mi < kMetricCount; ++mi) {
        if (!m_metricEnabled[static_cast<std::size_t>(mi)]) {
            continue;
        }
        auto *series = m_lineSeries[static_cast<std::size_t>(mi)];
        const double lo = yMin[static_cast<std::size_t>(mi)];
        const double hi = yMax[static_cast<std::size_t>(mi)];
        const double span = std::max(hi - lo, 1e-12);

        QList<QPointF> pts;
        pts.reserve(end);
        for (int i = 0; i < end; ++i) {
            const FlightSample &s = m_liveSamples[static_cast<std::size_t>(i)];
            const double x = chartXSeconds(tRef, s.timestamp, sessionElapsed);
            double y = sampleValueForMetric(s, mi);
            if (nEn > 1) {
                y = (y - lo) / span;
            }
            pts.append(QPointF(x, y));
        }
        series->replace(pts);
        series->setVisible(true);
        applySeriesPointDisplay(series, pts.size(), nEn);
        if (nEn > 1) {
            series->setName(metricTitle(mi) + u" (norm)"_s);
        } else {
            series->setName(metricTitle(mi));
        }
    }

    const double spanX = std::max(xMax - xMin, 1e-9);
    const double xPad = std::max(spanX * 0.02, 0.05);
    m_axisX->setRange(xMin - xPad, xMax + xPad);

    if (nEn == 1 && onlyMi >= 0) {
        const double lo = yMin[static_cast<std::size_t>(onlyMi)];
        const double hi = yMax[static_cast<std::size_t>(onlyMi)];
        const double span = std::max(hi - lo, 1e-9);
        const double p = span * 0.08 + std::max(std::abs(hi) * 1e-6, 1e-3);
        m_axisY->setRange(lo - p, hi + p);
        m_axisY->setTitleText(metricAxisUnitShort(onlyMi));
        m_chart->setTitle(metricTitle(onlyMi));
    } else {
        m_axisY->setRange(-0.05, 1.05);
        m_axisY->setTitleText(u"Normalized"_s);
        m_chart->setTitle(u"Multi-trace overlay"_s);
    }

    applyChartTheme();
    updateChartStatsLabel();
    updateReplayPanel();
}

void DashboardPage::appendLiveChartPoint(const FlightSample &sample) {
    Q_UNUSED(sample);
    rebuildLiveSeriesFromHistory();
    updateChartStatsLabel();
}

void DashboardPage::onSampleUpdated(const FlightSample &sample) {
    const double accelMag = std::sqrt(
        sample.acceleration.x * sample.acceleration.x
        + sample.acceleration.y * sample.acceleration.y
        + sample.acceleration.z * sample.acceleration.z);
    if (m_velValue) {
        m_velValue->setText(QStringLiteral("%1 m/s²").arg(accelMag, 0, 'f', 2));
    }
    if (m_altValue) {
        m_altValue->setText(QStringLiteral("%1 m").arg(sample.altitude, 0, 'f', 1));
    }
    if (m_tempValue) {
        m_tempValue->setText(QStringLiteral("%1 °C").arg(sample.temperature, 0, 'f', 1));
    }
    if (m_pressValue) {
        m_pressValue->setText(QStringLiteral("%1").arg(sample.pressure, 0, 'f', 1));
    }

    if (!m_model || m_model->replayMode()) {
        return;
    }
    m_liveSamples.push_back(sample);
    appendLiveChartPoint(sample);
    updateReplayPanel();
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
