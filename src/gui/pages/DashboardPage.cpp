#include "gui/pages/DashboardPage.h"

#include "domain/FlightSession.h"
#include "gui/FlightDataModel.h"
#include "gui/FlightReplayController.h"

#include <QChart>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QList>
#include <QSignalBlocker>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPointF>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

#include <algorithm>
#include <cmath>
#include <functional>

using namespace Qt::StringLiterals;

namespace {

constexpr int kMetricCount = 9;

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

QColor metricColor(int idx) {
    static const QColor colors[] = {
        QColor("#e8554e"),
        QColor("#4ecdc4"),
        QColor("#f7dc6f"),
        QColor("#bb86fc"),
        QColor("#69db7c"),
        QColor("#74c0fc"),
        QColor("#ff922b"),
        QColor("#a5d8ff"),
        QColor("#ffc9c9"),
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

/** Short name for hover / readout (not the chart title). */
QString metricQuantityName(int idx) {
    static const QString names[] = {
        u"Altitude"_s,
        u"Temperature"_s,
        u"Pressure"_s,
        u"|Acceleration|"_s,
        u"Battery"_s,
        u"RSSI"_s,
        u"|Gyro|"_s,
        u"Latitude"_s,
        u"Longitude"_s,
    };
    if (idx < 0 || idx >= kMetricCount) {
        return {};
    }
    return names[idx];
}

/** Y-axis title only — avoids Qt Charts truncating long titles to one letter (e.g. "t"). */
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
    std::function<QString(double tSec, double value, int sampleIndex, int totalSamples)> hoverFormatter;

    explicit TelemetryChartView(QChart *c, QWidget *parent = nullptr)
        : QChartView(c, parent),
          m_chartPtr(c) {
        setRubberBand(QChartView::RectangleRubberBand);
        setRenderHint(QPainter::Antialiasing);
        setMinimumHeight(300);
        setMouseTracking(true);
    }

    void setHoverSeries(QLineSeries *s) { m_series = s; }

protected:
    void mouseMoveEvent(QMouseEvent *event) override {
        QChartView::mouseMoveEvent(event);
        if (!hoverReadout || !m_series || m_chartPtr == nullptr) {
            return;
        }
        const QList<QPointF> pts = m_series->points();
        if (pts.isEmpty()) {
            hoverReadout(u"—"_s);
            return;
        }
        const QPointF scenePos = mapToScene(event->pos());
        const QPointF chartPos = m_chartPtr->mapFromScene(scenePos);
        const QPointF plotVals = m_chartPtr->mapToValue(chartPos, m_series);
        const int idx = nearestIndexByX(pts, plotVals.x());
        if (idx < 0) {
            return;
        }
        const QPointF &p = pts[idx];
        if (hoverFormatter) {
            hoverReadout(hoverFormatter(p.x(), p.y(), idx + 1, pts.size()));
        } else {
            hoverReadout(QStringLiteral("Elapsed %1 s · value %2 · #%3 / %4")
                             .arg(p.x(), 0, 'f', 3)
                             .arg(p.y(), 0, 'f', 4)
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
    QLineSeries *m_series = nullptr;
};

QFrame *createStatTile(const QString &label, const QString &value, QWidget *parent, QLabel **valueLabelOut) {
    auto *tile = new QFrame(parent);
    tile->setProperty("kind", u"statTile"_s);
    auto *layout = new QVBoxLayout(tile);
    layout->setContentsMargins(12, 8, 12, 8);
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

double DashboardPage::elapsedSeconds(long t0Ms, long tMs) {
    return static_cast<double>(tMs - t0Ms) / 1000.0;
}

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
            padding: 8px;
        }
        QFrame#chartWrapper {
            background-color: #101114;
            border: 1px solid #3b3b45;
            border-radius: 8px;
            padding: 12px;
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
        QComboBox#chartMetricCombo {
            background-color: #1a1a1a;
            color: #f5f5f5;
            border: 1px solid #4d4d4d;
            border-radius: 4px;
            padding: 4px 8px;
            min-height: 26px;
            min-width: 200px;
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
        QPushButton#chartCarouselBtn, QPushButton#chartResetZoomBtn {
            border: 1px solid #6a6a6a;
            border-radius: 4px;
            padding: 6px 12px;
            background-color: #3d3f47;
            color: #f0f0f0;
            min-width: 36px;
        }
        QLabel#chartHoverReadout {
            color: #c8d4e0;
            font-size: 13px;
            padding: 6px 8px;
            background-color: rgba(10, 11, 14, 0.9);
            border: 1px solid #3b3b45;
            border-radius: 6px;
        }
        QLabel#chartStatsLabel {
            color: #8fa0b0;
            font-size: 12px;
        }
    )"_s);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setSpacing(16);
    rootLayout->setContentsMargins(24, 24, 24, 24);

    auto *replayBar = new QFrame(this);
    replayBar->setObjectName(u"replayBar"_s);
    auto *replayOuter = new QVBoxLayout(replayBar);
    replayOuter->setContentsMargins(10, 10, 10, 10);
    replayOuter->setSpacing(8);

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

    rootLayout->addWidget(replayBar);

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
    contentLayout->setSpacing(0);
    rootLayout->addLayout(contentLayout);

    auto *leftColumn = new QVBoxLayout();
    leftColumn->setSpacing(12);
    auto *chartFrame = new QFrame(this);
    chartFrame->setObjectName(u"chartFrame"_s);
    auto *chartFrameLayout = new QVBoxLayout(chartFrame);
    chartFrameLayout->setContentsMargins(10, 10, 10, 10);
    chartFrameLayout->setSpacing(8);

    auto *chartToolbar = new QHBoxLayout();
    chartToolbar->setSpacing(10);

    m_metricPrevBtn = new QPushButton(u"◀"_s, chartFrame);
    m_metricPrevBtn->setObjectName(u"chartCarouselBtn"_s);
    m_metricPrevBtn->setToolTip(u"Previous metric"_s);
    m_metricNextBtn = new QPushButton(u"▶"_s, chartFrame);
    m_metricNextBtn->setObjectName(u"chartCarouselBtn"_s);
    m_metricNextBtn->setToolTip(u"Next metric"_s);

    m_metricCombo = new QComboBox(chartFrame);
    m_metricCombo->setObjectName(u"chartMetricCombo"_s);
    for (int i = 0; i < kMetricCount; ++i) {
        m_metricCombo->addItem(metricTitle(i));
    }
    m_metricCombo->setCurrentIndex(0);

    m_zoomResetBtn = new QPushButton(u"Reset zoom"_s, chartFrame);
    m_zoomResetBtn->setObjectName(u"chartResetZoomBtn"_s);
    m_zoomResetBtn->setToolTip(u"Show full time range again"_s);

    chartToolbar->addWidget(m_metricPrevBtn);
    chartToolbar->addWidget(m_metricCombo, 1);
    chartToolbar->addWidget(m_metricNextBtn);
    chartToolbar->addWidget(m_zoomResetBtn);
    chartFrameLayout->addLayout(chartToolbar);

    connect(m_metricPrevBtn, &QPushButton::clicked, this, &DashboardPage::onPrevMetric);
    connect(m_metricNextBtn, &QPushButton::clicked, this, &DashboardPage::onNextMetric);
    connect(m_metricCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DashboardPage::onMetricComboChanged);
    connect(m_zoomResetBtn, &QPushButton::clicked, this, &DashboardPage::onResetChartZoom);

    m_hoverReadoutLabel = new QLabel(chartFrame);
    m_hoverReadoutLabel->setObjectName(u"chartHoverReadout"_s);
    m_hoverReadoutLabel->setWordWrap(true);
    updateHoverReadoutDefault();
    chartFrameLayout->addWidget(m_hoverReadoutLabel);

    m_chartStatsLabel = new QLabel(chartFrame);
    m_chartStatsLabel->setObjectName(u"chartStatsLabel"_s);
    m_chartStatsLabel->setText(u""_s);
    chartFrameLayout->addWidget(m_chartStatsLabel);

    auto *chartWrapper = new QFrame(chartFrame);
    chartWrapper->setObjectName(u"chartWrapper"_s);
    auto *chartWrapperLayout = new QVBoxLayout(chartWrapper);
    chartWrapperLayout->setContentsMargins(8, 8, 8, 8);
    chartWrapperLayout->setSpacing(0);

    m_chart = new QChart();
    m_chart->setTitle(metricTitle(0));
    m_chart->setTitleBrush(QColor("#e8e8e8"));
    m_chart->legend()->setVisible(false);
    m_chart->setMargins(QMargins(12, 12, 16, 12));
    m_chart->setBackgroundBrush(QColor(21, 22, 25));
    m_chart->setBackgroundPen(Qt::NoPen);

    m_series = new QLineSeries();
    m_series->setName(metricTitle(0));
    m_series->setColor(metricColor(0));
    m_series->setPen(QPen(metricColor(0), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    m_series->setUseOpenGL(true);
    m_chart->addSeries(m_series);

    m_axisX = new QValueAxis();
    m_axisX->setTitleText(u"Elapsed time (s)"_s);
    m_axisX->setRange(0, 10);
    m_axisX->setTickCount(8);
    m_axisX->setLabelFormat(u"%.2f"_s);
    m_axisX->setLabelsColor(QColor("#e0e0e0"));
    m_axisX->setTitleBrush(QColor("#b0b8c4"));
    m_axisX->setLinePenColor(QColor("#c8c8c8"));
    m_axisX->setGridLineColor(QColor(255, 255, 255, 45));
    m_chart->addAxis(m_axisX, Qt::AlignBottom);
    m_series->attachAxis(m_axisX);

    m_axisY = new QValueAxis();
    m_axisY->setTitleText(metricTitle(0));
    m_axisY->setRange(-1, 1);
    m_axisY->setTickCount(7);
    m_axisY->setLabelFormat(u"%.4g"_s);
    m_axisY->setLabelsColor(QColor("#e0e0e0"));
    m_axisY->setTitleBrush(QColor("#b0b8c4"));
    m_axisY->setLinePenColor(QColor("#c8c8c8"));
    m_axisY->setGridLineColor(QColor(255, 255, 255, 45));
    m_chart->addAxis(m_axisY, Qt::AlignLeft);
    m_series->attachAxis(m_axisY);

    auto *tcv = new TelemetryChartView(m_chart, chartWrapper);
    m_chartView = tcv;
    tcv->setHoverSeries(m_series);
    tcv->hoverFormatter = [this](double tSec, double value, int sampleIndex, int totalSamples) {
        return formatMetricHover(tSec, value, sampleIndex, totalSamples);
    };
    tcv->hoverReadout = [this](const QString &s) {
        if (!m_hoverReadoutLabel) {
            return;
        }
        if (s.isEmpty()) {
            updateHoverReadoutDefault();
        } else if (s == u"—"_s) {
            m_hoverReadoutLabel->setText(u"No curve yet — load a log or connect live (Monitoring)."_s);
        } else {
            m_hoverReadoutLabel->setText(s);
        }
    };

    chartWrapperLayout->addWidget(m_chartView, 1);
    chartFrameLayout->addWidget(chartWrapper, 1);

    leftColumn->addWidget(chartFrame, 1);

    contentLayout->addLayout(leftColumn, 1);

    if (m_model) {
        connect(m_model, &FlightDataModel::sampleUpdated, this, &DashboardPage::onSampleUpdated);
        connect(m_model, &FlightDataModel::sessionReset, this, &DashboardPage::onSessionReset);
        connect(m_model, &FlightDataModel::replayModeChanged, this, [this](bool) {
            updateReplayPanel();
        });
    }

    applyMetricToChartUi();
    updateChartStatsLabel();
    updateReplayPanel();
}

void DashboardPage::updateHoverReadoutDefault() {
    if (m_hoverReadoutLabel) {
        m_hoverReadoutLabel->setText(
            u"Hover: metric value + elapsed time + sample index. Drag on the chart to zoom."_s);
    }
}

void DashboardPage::applyMetricToChartUi() {
    if (!m_series || !m_axisY || !m_chart) {
        return;
    }
    const QString t = metricTitle(m_metricIndex);
    m_series->setName(t);
    const QColor col = metricColor(m_metricIndex);
    m_series->setColor(col);
    m_series->setPen(QPen(col, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    m_axisY->setTitleText(metricAxisUnitShort(m_metricIndex));
    m_chart->setTitle(t);
}

void DashboardPage::onMetricComboChanged(int index) {
    if (index < 0 || index >= kMetricCount) {
        return;
    }
    m_metricIndex = index;
    applyMetricToChartUi();
    if (m_model && m_model->replayMode() && m_session && !m_session->samples.empty()) {
        rebuildReplayCharts(m_lastReplayTrailLength);
    } else {
        rebuildLiveSeriesFromHistory();
    }
    if (m_chart) {
        m_chart->zoomReset();
    }
    updateChartStatsLabel();
}

void DashboardPage::onPrevMetric() {
    const int n = (m_metricIndex - 1 + kMetricCount) % kMetricCount;
    if (m_metricCombo) {
        m_metricCombo->blockSignals(true);
        m_metricCombo->setCurrentIndex(n);
        m_metricCombo->blockSignals(false);
    }
    onMetricComboChanged(n);
}

void DashboardPage::onNextMetric() {
    const int n = (m_metricIndex + 1) % kMetricCount;
    if (m_metricCombo) {
        m_metricCombo->blockSignals(true);
        m_metricCombo->setCurrentIndex(n);
        m_metricCombo->blockSignals(false);
    }
    onMetricComboChanged(n);
}

void DashboardPage::onResetChartZoom() {
    if (m_chart) {
        m_chart->zoomReset();
    }
}

void DashboardPage::updateChartStatsLabel() {
    if (!m_chartStatsLabel) {
        return;
    }
    const int n = m_series ? m_series->count() : 0;
    const QString mode = (m_model && m_model->replayMode()) ? u"Replay"_s : u"Live"_s;
    m_chartStatsLabel->setText(
        QStringLiteral("%1 · %2 points on this curve · rectangle zoom · Reset zoom to fit").arg(mode).arg(n));
}

QString DashboardPage::formatMetricHover(
    double tSec,
    double value,
    int sampleIndex,
    int totalSamples) const {
    const int mi = m_metricIndex;
    const QString name = metricQuantityName(mi);
    const QString unit = metricAxisUnitShort(mi);
    const QString v = formatMetricValuePretty(mi, value);
    return QStringLiteral("%1 = %2 %3   ·   elapsed %4 s   ·   sample %5 / %6")
        .arg(name, v, unit)
        .arg(tSec, 0, 'f', 3)
        .arg(sampleIndex)
        .arg(totalSamples);
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
    const long t0 = samples[0].timestamp;
    const double tFullEnd = elapsedSeconds(t0, samples.back().timestamp);

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
        const double tHead = elapsedSeconds(t0, samples[static_cast<std::size_t>(head - 1)].timestamp);
        line2 = QStringLiteral(
                   "Showing samples 1–%1 of %2 · elapsed 0 → %3 s (log spans %4 s)")
                    .arg(head)
                    .arg(n)
                    .arg(tHead, 0, 'f', 2)
                    .arg(tFullEnd, 0, 'f', 2);
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

void DashboardPage::rebuildReplayCharts(int trailLength) {
    if (!m_series || !m_axisX || !m_axisY) {
        return;
    }
    m_series->clear();

    if (!m_session || trailLength <= 0 || m_session->samples.empty()) {
        m_axisX->setRange(0, 10);
        m_axisY->setRange(-1, 1);
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

    const long t0 = samples[0].timestamp;
    double yMin = sampleValueForMetric(samples[0], m_metricIndex);
    double yMax = yMin;
    double xMax = 0;

    QList<QPointF> pts;
    pts.reserve(end);
    for (int i = 0; i < end; ++i) {
        const FlightSample &s = samples[static_cast<std::size_t>(i)];
        const double x = elapsedSeconds(t0, s.timestamp);
        const double y = sampleValueForMetric(s, m_metricIndex);
        pts.append(QPointF(x, y));
        xMax = x;
        yMin = std::min(yMin, y);
        yMax = std::max(yMax, y);
    }
    m_series->replace(pts);

    const double xPad = std::max((xMax) * 0.02, 0.05);
    m_axisX->setRange(-xPad, xMax + xPad);

    const double span = std::max(yMax - yMin, 1e-9);
    const double p = span * 0.08 + std::max(std::abs(yMax) * 1e-6, 1e-3);
    m_axisY->setRange(yMin - p, yMax + p);

    if (m_chartView) {
        if (auto *tcv = dynamic_cast<TelemetryChartView *>(m_chartView)) {
            tcv->setHoverSeries(m_series);
        }
    }
    updateChartStatsLabel();
    updateReplayPanel();
}

void DashboardPage::onSessionReset() {
    m_haveT0 = false;
    m_liveSamples.clear();
    if (m_series) {
        m_series->clear();
    }
    if (m_axisX) {
        m_axisX->setRange(0, 10);
    }
    if (m_axisY) {
        m_axisY->setRange(-1, 1);
    }
    updateChartStatsLabel();
    updateReplayPanel();
}

void DashboardPage::rebuildLiveSeriesFromHistory() {
    if (!m_series || !m_axisX || !m_axisY) {
        return;
    }
    m_series->clear();
    if (m_liveSamples.empty()) {
        m_axisX->setRange(0, 10);
        m_axisY->setRange(-1, 1);
        updateChartStatsLabel();
        updateReplayPanel();
        return;
    }

    const long t0 = m_liveSamples.front().timestamp;
    double yMin = sampleValueForMetric(m_liveSamples.front(), m_metricIndex);
    double yMax = yMin;
    double xMax = 0;

    QList<QPointF> pts;
    pts.reserve(static_cast<int>(m_liveSamples.size()));
    for (const FlightSample &s : m_liveSamples) {
        const double x = elapsedSeconds(t0, s.timestamp);
        const double y = sampleValueForMetric(s, m_metricIndex);
        pts.append(QPointF(x, y));
        xMax = x;
        yMin = std::min(yMin, y);
        yMax = std::max(yMax, y);
    }
    m_series->replace(pts);

    const double xPad = std::max(xMax * 0.02, 0.05);
    m_axisX->setRange(-xPad, xMax + xPad);

    const double span = std::max(yMax - yMin, 1e-9);
    const double p = span * 0.08 + std::max(std::abs(yMax) * 1e-6, 1e-3);
    m_axisY->setRange(yMin - p, yMax + p);

    if (m_chartView) {
        if (auto *tcv = dynamic_cast<TelemetryChartView *>(m_chartView)) {
            tcv->setHoverSeries(m_series);
        }
    }
    updateChartStatsLabel();
    updateReplayPanel();
}

void DashboardPage::updateLiveAxisRanges() {
    if (!m_series || m_series->count() == 0 || !m_axisX || !m_axisY) {
        return;
    }
    const QList<QPointF> pts = m_series->points();
    double xMin = pts.front().x();
    double xMax = pts.back().x();
    double yMin = pts.front().y();
    double yMax = yMin;
    for (const QPointF &p : pts) {
        yMin = std::min(yMin, p.y());
        yMax = std::max(yMax, p.y());
    }
    const double xPad = std::max((xMax - xMin) * 0.05, 0.05);
    m_axisX->setRange(xMin - xPad, xMax + xPad);
    const double span = std::max(yMax - yMin, 1e-9);
    const double p = span * 0.08 + std::max(std::abs(yMax) * 1e-6, 1e-3);
    m_axisY->setRange(yMin - p, yMax + p);
}

void DashboardPage::appendLiveChartPoint(const FlightSample &sample) {
    if (!m_series || !m_axisX || !m_axisY) {
        return;
    }
    m_liveSamples.push_back(sample);
    if (!m_haveT0) {
        m_t0Ms = sample.timestamp;
        m_haveT0 = true;
    }
    const double x = elapsedSeconds(m_t0Ms, sample.timestamp);
    const double y = sampleValueForMetric(sample, m_metricIndex);
    m_series->append(x, y);
    updateLiveAxisRanges();

    if (m_chartView) {
        if (auto *tcv = dynamic_cast<TelemetryChartView *>(m_chartView)) {
            tcv->setHoverSeries(m_series);
        }
    }
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
