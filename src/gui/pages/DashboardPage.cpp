#include "gui/pages/DashboardPage.h"

#include "domain/FlightSession.h"
#include "gui/FlightDataModel.h"
#include "gui/FlightReplayController.h"

#include <QChart>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QEvent>
#include <QFocusEvent>
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
#include <QSignalBlocker>
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

QString formatReplayClockHms(double sec) {
    if (!std::isfinite(sec) || sec < 0.0) {
        return u"—"_s;
    }
    const int total = static_cast<int>(std::floor(sec + 0.5));
    const int h = total / 3600;
    const int m = (total % 3600) / 60;
    const int s = total % 60;
    if (h > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(h)
            .arg(m, 2, 10, QLatin1Char('0'))
            .arg(s, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

QLineSeries *firstVisibleNonEmptyLineSeries(QChart *chart) {
    if (!chart) {
        return nullptr;
    }
    for (QAbstractSeries *s : chart->series()) {
        auto *ls = qobject_cast<QLineSeries *>(s);
        if (ls && ls->isVisible() && !ls->points().isEmpty()) {
            return ls;
        }
    }
    return nullptr;
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
    std::function<void()> onUserAdjustedAxes;

    explicit TelemetryChartView(QChart *c, QWidget *parent = nullptr)
        : QChartView(c, parent),
          m_chartPtr(c) {
        setRubberBand(QChartView::NoRubberBand);
        setRenderHint(QPainter::Antialiasing);
        setFrameShape(QFrame::NoFrame);
        setContentsMargins(0, 0, 0, 0);
        setViewportMargins(0, 0, 0, 0);
        setMinimumHeight(420);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMouseTracking(true);
        setContextMenuPolicy(Qt::NoContextMenu);
        setDragMode(QGraphicsView::NoDrag);
    }

    void setChart(QChart *c) { m_chartPtr = c; }

protected:
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && m_chartPtr != nullptr) {
            if (event->modifiers() & Qt::ControlModifier) {
                setRubberBand(QChartView::RectangleRubberBand);
                m_rubberZoomActive = true;
                QChartView::mousePressEvent(event);
                return;
            }
            setRubberBand(QChartView::NoRubberBand);
            m_panning = true;
            m_lastPanPos = event->pos();
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
        QChartView::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override {
        if (m_panning && m_chartPtr != nullptr && (event->buttons() & Qt::LeftButton)) {
            const QPoint delta = event->pos() - m_lastPanPos;
            m_lastPanPos = event->pos();
            if (delta.x() != 0 || delta.y() != 0) {
                panAxesByPixels(delta);
                if (onUserAdjustedAxes) {
                    onUserAdjustedAxes();
                }
            }
            event->accept();
            updateHoverReadoutAt(event->pos());
            return;
        }
        if (!(event->buttons() & Qt::LeftButton) || (event->modifiers() & Qt::ControlModifier)) {
            QChartView::mouseMoveEvent(event);
        }
        updateHoverReadoutAt(event->pos());
    }

    void mouseReleaseEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && m_panning) {
            endPanningIfActive();
            event->accept();
            return;
        }
        QChartView::mouseReleaseEvent(event);
        if (event->button() == Qt::LeftButton && m_rubberZoomActive) {
            m_rubberZoomActive = false;
            setRubberBand(QChartView::NoRubberBand);
            if (onUserAdjustedAxes) {
                onUserAdjustedAxes();
            }
        } else {
            setRubberBand(QChartView::NoRubberBand);
        }
    }

    void wheelEvent(QWheelEvent *event) override {
        if (m_chartPtr == nullptr) {
            QChartView::wheelEvent(event);
            return;
        }
        const QRectF plot = m_chartPtr->plotArea();
        const QPointF ep = event->position();
        if (!plot.contains(ep)) {
            QChartView::wheelEvent(event);
            return;
        }
        auto *axX = qobject_cast<QValueAxis *>(m_chartPtr->axes(Qt::Horizontal).value(0));
        auto *axY = qobject_cast<QValueAxis *>(m_chartPtr->axes(Qt::Vertical).value(0));
        if (!axX || !axY) {
            QChartView::wheelEvent(event);
            return;
        }
        const double xMin = axX->min();
        const double xMax = axX->max();
        const double yMin = axY->min();
        const double yMax = axY->max();
        const double spanX = xMax - xMin;
        const double spanY = yMax - yMin;
        if (spanX <= 0.0 || spanY <= 0.0) {
            event->accept();
            return;
        }

        const double tx = std::clamp((ep.x() - plot.left()) / plot.width(), 0.0, 1.0);
        const double ty = std::clamp((plot.bottom() - ep.y()) / plot.height(), 0.0, 1.0);
        double ax = xMin + tx * spanX;
        double ay = yMin + ty * spanY;

        if (QLineSeries *ref = firstVisibleNonEmptyLineSeries(m_chartPtr)) {
            const QPointF scenePos = mapToScene(ep.toPoint());
            const QPointF chartPos = m_chartPtr->mapFromScene(scenePos);
            const QPointF v = m_chartPtr->mapToValue(chartPos, ref);
            ax = v.x();
            ay = v.y();
        }

        const QPoint pixelDelta = event->pixelDelta();
        const bool hasPixel = (pixelDelta.x() != 0 || pixelDelta.y() != 0);
        const int angleY = event->angleDelta().y();
        const int angleX = event->angleDelta().x();
        const bool zoomModifier = (event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)) != 0;

        if (zoomModifier) {
            if (pixelDelta.y() != 0) {
                constexpr double kPixelZoom = 0.0021;
                double spanScale = std::exp(-static_cast<double>(pixelDelta.y()) * kPixelZoom);
                spanScale = std::clamp(spanScale, 0.9, 1.11);
                zoomValueAxesAtFocal(axX, axY, ax, ay, spanScale);
                if (onUserAdjustedAxes) {
                    onUserAdjustedAxes();
                }
                event->accept();
                return;
            }
            if (angleY != 0) {
                const int steps = angleY / 120;
                constexpr double kFactorPerStep = 0.88;
                double zoomFactor = 1.0;
                for (int i = 0; i < std::abs(steps); ++i) {
                    zoomFactor *= (steps > 0) ? kFactorPerStep : (1.0 / kFactorPerStep);
                }
                zoomValueAxesAtFocal(axX, axY, ax, ay, zoomFactor);
                if (onUserAdjustedAxes) {
                    onUserAdjustedAxes();
                }
                event->accept();
                return;
            }
            event->accept();
            return;
        }

        if (hasPixel) {
            panAxesByPixels(pixelDelta);
            if (onUserAdjustedAxes) {
                onUserAdjustedAxes();
            }
            event->accept();
            return;
        }

        if (angleY != 0) {
            const int steps = angleY / 120;
            constexpr double kFactorPerStep = 0.88;
            double zoomFactor = 1.0;
            for (int i = 0; i < std::abs(steps); ++i) {
                zoomFactor *= (steps > 0) ? kFactorPerStep : (1.0 / kFactorPerStep);
            }
            zoomValueAxesAtFocal(axX, axY, ax, ay, zoomFactor);
            if (onUserAdjustedAxes) {
                onUserAdjustedAxes();
            }
            event->accept();
            return;
        }

        if (angleX != 0) {
            constexpr double kTiltToPixels = 0.35;
            panAxesByPixels(QPoint(-static_cast<int>(std::lround(angleX * kTiltToPixels)), 0));
            if (onUserAdjustedAxes) {
                onUserAdjustedAxes();
            }
            event->accept();
            return;
        }

        QChartView::wheelEvent(event);
    }

    void leaveEvent(QEvent *event) override {
        if (m_panning) {
            endPanningIfActive();
        } else if (hoverReadout) {
            hoverReadout({});
        }
        QChartView::leaveEvent(event);
    }

    void focusOutEvent(QFocusEvent *event) override {
        endPanningIfActive();
        QChartView::focusOutEvent(event);
    }

private:
    static void zoomValueAxesAtFocal(QValueAxis *axX, QValueAxis *axY, double ax, double ay, double spanScale) {
        const double xMin = axX->min();
        const double xMax = axX->max();
        const double yMin = axY->min();
        const double yMax = axY->max();
        axX->setRange(ax - (ax - xMin) * spanScale, ax + (xMax - ax) * spanScale);
        axY->setRange(ay - (ay - yMin) * spanScale, ay + (yMax - ay) * spanScale);
    }

    void endPanningIfActive() {
        if (!m_panning) {
            return;
        }
        m_panning = false;
        releaseMouse();
        unsetCursor();
        setRubberBand(QChartView::NoRubberBand);
    }

    void panAxesByPixels(const QPoint &delta) {
        if (!m_chartPtr) {
            return;
        }
        const QRectF plot = m_chartPtr->plotArea();
        if (plot.width() <= 1.0 || plot.height() <= 1.0) {
            return;
        }
        auto *axX = qobject_cast<QValueAxis *>(m_chartPtr->axes(Qt::Horizontal).value(0));
        auto *axY = qobject_cast<QValueAxis *>(m_chartPtr->axes(Qt::Vertical).value(0));
        if (!axX || !axY) {
            return;
        }
        const double spanX = axX->max() - axX->min();
        const double spanY = axY->max() - axY->min();
        const double dValX = static_cast<double>(delta.x()) * spanX / plot.width();
        const double dValY = static_cast<double>(delta.y()) * spanY / plot.height();
        axX->setRange(axX->min() - dValX, axX->max() - dValX);
        axY->setRange(axY->min() + dValY, axY->max() + dValY);
    }

    void updateHoverReadoutAt(const QPoint &widgetPos) {
        if (!hoverReadout || m_chartPtr == nullptr) {
            return;
        }
        QLineSeries *ref = firstVisibleNonEmptyLineSeries(m_chartPtr);
        if (!ref) {
            hoverReadout(u"—"_s);
            return;
        }
        const QList<QPointF> pts = ref->points();
        const QPointF scenePos = mapToScene(widgetPos);
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

    QChart *m_chartPtr = nullptr;
    bool m_panning = false;
    bool m_rubberZoomActive = false;
    QPoint m_lastPanPos;
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
            padding: 0px;
        }
        #telemetryChartView {
            border: none;
            padding: 0px;
            margin: 0px;
            background-color: transparent;
        }
        QFrame#tracesPanel {
            background-color: rgba(14, 15, 18, 0.95);
            border: 1px solid #3b3b45;
            border-radius: 8px;
        }
        QLabel#tracesPanelTitle {
            color: #a8b4c0;
            font-size: 11px;
            font-weight: 600;
            letter-spacing: 1px;
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
        QToolButton#chartToggleBtn {
            border: 1px solid #5a5d68;
            border-radius: 4px;
            padding: 5px 12px;
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
            min-width: 30px;
            max-width: 30px;
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
        QCheckBox#traceCheck::indicator {
            width: 18px;
            height: 18px;
            border-radius: 4px;
            border: 1px solid #5a5d68;
            background-color: #1e2026;
        }
        QCheckBox#traceCheck::indicator:hover {
            border-color: #7a8796;
        }
        QCheckBox#traceCheck::indicator:checked {
            background-color: #3d5a80;
            border-color: #6a9fd5;
        }
        QCheckBox#traceCheck::indicator:checked:hover {
            background-color: #4a6a95;
            border-color: #7ab0e8;
        }
        QFrame#replayBar {
            background-color: rgba(28, 28, 30, 0.94);
            border: 1px solid rgba(255, 255, 255, 0.08);
            border-radius: 16px;
        }
        QLabel#replayBarTitle {
            color: rgba(255, 255, 255, 0.45);
            font-size: 11px;
            font-weight: 600;
            letter-spacing: 2px;
            font-family: system-ui, -apple-system, "SF Pro Text", "Helvetica Neue", "Segoe UI", sans-serif;
        }
        QLabel#replayClockLabel {
            color: rgba(255, 255, 255, 0.82);
            font-size: 13px;
            font-weight: 500;
            font-variant-numeric: tabular-nums;
            font-family: system-ui, -apple-system, "SF Mono", "SF Pro Text", "Menlo", monospace;
        }
        QLabel#replayStatusLabel {
            color: rgba(255, 255, 255, 0.42);
            font-size: 11px;
            font-family: system-ui, -apple-system, "SF Pro Text", "Helvetica Neue", "Segoe UI", sans-serif;
        }
        QSlider#replayScrubSlider {
            min-height: 28px;
        }
        QSlider#replayScrubSlider::groove:horizontal {
            height: 4px;
            background: rgba(255, 255, 255, 0.18);
            border-radius: 2px;
        }
        QSlider#replayScrubSlider::sub-page:horizontal {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #0a84ff, stop:1 #64d2ff);
            border-radius: 2px;
            height: 4px;
        }
        QSlider#replayScrubSlider::handle:horizontal {
            width: 16px;
            height: 16px;
            margin: -6px 0;
            background: #ffffff;
            border: none;
            border-radius: 8px;
        }
        QSlider#replayScrubSlider::handle:horizontal:hover {
            background: #f2f2f7;
        }
        QToolButton#replayTransportBtn, QToolButton#replaySkipBtn {
            background: rgba(255, 255, 255, 0.06);
            border: none;
            border-radius: 10px;
            padding: 8px;
            min-width: 40px;
            min-height: 40px;
        }
        QToolButton#replayTransportBtn:hover, QToolButton#replaySkipBtn:hover {
            background: rgba(255, 255, 255, 0.12);
        }
        QToolButton#replayTransportBtn:pressed, QToolButton#replaySkipBtn:pressed {
            background: rgba(255, 255, 255, 0.18);
        }
        QToolButton#replayTransportBtn:disabled, QToolButton#replaySkipBtn:disabled {
            background: rgba(255, 255, 255, 0.03);
        }
        QToolButton#replayPlayPauseBtn {
            background: rgba(255, 255, 255, 0.14);
            border: none;
            border-radius: 28px;
            min-width: 56px;
            max-width: 56px;
            min-height: 56px;
            max-height: 56px;
            padding: 0px;
        }
        QToolButton#replayPlayPauseBtn:hover {
            background: rgba(255, 255, 255, 0.22);
        }
        QToolButton#replayPlayPauseBtn:pressed {
            background: rgba(255, 255, 255, 0.28);
        }
        QToolButton#replayPlayPauseBtn:disabled {
            background: rgba(255, 255, 255, 0.06);
        }
        QComboBox#replaySpeedCombo {
            background-color: rgba(255, 255, 255, 0.08);
            color: rgba(255, 255, 255, 0.9);
            border: 1px solid rgba(255, 255, 255, 0.12);
            border-radius: 10px;
            padding: 6px 28px 6px 12px;
            min-height: 32px;
            font-size: 13px;
            font-family: system-ui, -apple-system, "SF Pro Text", "Helvetica Neue", "Segoe UI", sans-serif;
        }
        QComboBox#replaySpeedCombo:hover {
            background-color: rgba(255, 255, 255, 0.12);
        }
        QComboBox#replaySpeedCombo::drop-down {
            border: none;
            width: 22px;
        }
        QComboBox#replaySpeedCombo QAbstractItemView {
            background-color: #2c2c2e;
            color: #f2f2f7;
            selection-background-color: #0a84ff;
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

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setSpacing(8);
    rootLayout->setContentsMargins(8, 8, 8, 8);

    auto *replayBar = new QFrame(this);
    replayBar->setObjectName(u"replayBar"_s);
    auto *replayOuter = new QVBoxLayout(replayBar);
    replayOuter->setContentsMargins(14, 12, 14, 14);
    replayOuter->setSpacing(10);

    auto *replayHeader = new QHBoxLayout();
    replayHeader->setSpacing(8);
    m_replayBarTitle = new QLabel(replayBar);
    m_replayBarTitle->setObjectName(u"replayBarTitle"_s);
    m_replayBarTitle->setText(u"REPLAY"_s);
    replayHeader->addWidget(m_replayBarTitle, 0, Qt::AlignLeft | Qt::AlignVCenter);
    replayHeader->addStretch(1);
    replayOuter->addLayout(replayHeader);

    m_replaySlider = new QSlider(Qt::Horizontal, replayBar);
    m_replaySlider->setObjectName(u"replayScrubSlider"_s);
    m_replaySlider->setRange(0, 0);
    m_replaySlider->setEnabled(false);
    m_replaySlider->setSingleStep(1);
    m_replaySlider->setPageStep(10);
    m_replaySlider->setTracking(true);
    m_replaySlider->setToolTip(
        u"Timeline scrub — how much of the loaded log is shown on the chart (prefix of samples)."_s);
    replayOuter->addWidget(m_replaySlider);

    auto *replayTimeRow = new QHBoxLayout();
    replayTimeRow->setContentsMargins(2, 0, 2, 0);
    m_replayTimeLeftLabel = new QLabel(u"—"_s, replayBar);
    m_replayTimeLeftLabel->setObjectName(u"replayClockLabel"_s);
    m_replayTimeRightLabel = new QLabel(u"—"_s, replayBar);
    m_replayTimeRightLabel->setObjectName(u"replayClockLabel"_s);
    m_replayTimeRightLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    replayTimeRow->addWidget(m_replayTimeLeftLabel, 0, Qt::AlignLeft | Qt::AlignVCenter);
    replayTimeRow->addStretch(1);
    replayTimeRow->addWidget(m_replayTimeRightLabel, 0, Qt::AlignRight | Qt::AlignVCenter);
    replayOuter->addLayout(replayTimeRow);

    auto *replayTransportRow = new QHBoxLayout();
    replayTransportRow->setSpacing(12);
    replayTransportRow->addStretch(1);

    const auto setupSkipOrStopBtn = [](QToolButton *b, const QString &objName) {
        b->setObjectName(objName);
        b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setIconSize(QSize(22, 22));
    };

    m_jumpStartBtn = new QToolButton(replayBar);
    setupSkipOrStopBtn(m_jumpStartBtn, u"replaySkipBtn"_s);
    m_jumpStartBtn->setToolTip(u"Jump to start of log"_s);
    m_jumpStartBtn->setIcon(style()->standardIcon(QStyle::SP_MediaSkipBackward));

    m_playPauseBtn = new QToolButton(replayBar);
    m_playPauseBtn->setObjectName(u"replayPlayPauseBtn"_s);
    m_playPauseBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_playPauseBtn->setAutoRaise(true);
    m_playPauseBtn->setFocusPolicy(Qt::NoFocus);
    m_playPauseBtn->setIconSize(QSize(28, 28));
    m_playPauseBtn->setToolTip(u"Play or pause"_s);
    m_playPauseBtn->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));

    m_jumpEndBtn = new QToolButton(replayBar);
    setupSkipOrStopBtn(m_jumpEndBtn, u"replaySkipBtn"_s);
    m_jumpEndBtn->setToolTip(u"Jump to end of log"_s);
    m_jumpEndBtn->setIcon(style()->standardIcon(QStyle::SP_MediaSkipForward));

    m_stopBtn = new QToolButton(replayBar);
    setupSkipOrStopBtn(m_stopBtn, u"replayTransportBtn"_s);
    m_stopBtn->setToolTip(u"Stop and return to the beginning"_s);
    m_stopBtn->setIcon(style()->standardIcon(QStyle::SP_MediaStop));

    m_speedCombo = new QComboBox(replayBar);
    m_speedCombo->setObjectName(u"replaySpeedCombo"_s);
    m_speedCombo->setToolTip(u"Playback rate — wall clock vs timestamps between samples"_s);
    const QList<double> speedRates{0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 3.0, 4.0};
    for (double r : speedRates) {
        m_speedCombo->addItem(QStringLiteral("%1×").arg(r, 0, 'g', 3), r);
    }
    m_speedCombo->setCurrentIndex(3);

    replayTransportRow->addWidget(m_jumpStartBtn, 0, Qt::AlignVCenter);
    replayTransportRow->addWidget(m_playPauseBtn, 0, Qt::AlignVCenter);
    replayTransportRow->addWidget(m_jumpEndBtn, 0, Qt::AlignVCenter);
    replayTransportRow->addSpacing(16);
    replayTransportRow->addWidget(m_stopBtn, 0, Qt::AlignVCenter);
    replayTransportRow->addSpacing(8);
    replayTransportRow->addWidget(m_speedCombo, 0, Qt::AlignVCenter);
    replayTransportRow->addStretch(1);
    replayOuter->addLayout(replayTransportRow);

    m_replayInfoLabel = new QLabel(replayBar);
    m_replayInfoLabel->setObjectName(u"replayStatusLabel"_s);
    m_replayInfoLabel->setWordWrap(true);
    m_replayActivityText = u"Ready"_s;
    replayOuter->addWidget(m_replayInfoLabel);

    if (m_replay) {
        connect(m_playPauseBtn, &QToolButton::clicked, this, [this]() {
            if (!m_replay) {
                return;
            }
            if (m_replay->isPlaying()) {
                m_replay->pause();
            } else {
                m_replay->play();
            }
        });
        connect(m_stopBtn, &QToolButton::clicked, m_replay, &FlightReplayController::stop);
        connect(m_speedCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            if (!m_speedCombo || !m_replay) {
                return;
            }
            bool ok = false;
            const double v = m_speedCombo->currentData().toDouble(&ok);
            if (ok) {
                m_replay->setSpeed(v);
            }
            updateReplayPanel();
        });
        connect(m_replaySlider, &QSlider::valueChanged, m_replay, &FlightReplayController::setPosition);
        connect(m_jumpStartBtn, &QToolButton::clicked, this, [this]() {
            if (m_replaySlider) {
                m_replaySlider->setValue(0);
            }
        });
        connect(m_jumpEndBtn, &QToolButton::clicked, this, [this]() {
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
    contentLayout->setSpacing(4);
    contentLayout->setContentsMargins(0, 0, 0, 0);

    auto *tracesPanel = new QFrame(this);
    tracesPanel->setObjectName(u"tracesPanel"_s);
    tracesPanel->setFixedWidth(168);
    auto *tracesOuter = new QVBoxLayout(tracesPanel);
    tracesOuter->setContentsMargins(6, 6, 6, 6);
    tracesOuter->setSpacing(4);

    auto *tracesTitle = new QLabel(u"TRACES"_s, tracesPanel);
    tracesTitle->setObjectName(u"tracesPanelTitle"_s);
    tracesOuter->addWidget(tracesTitle);

    auto *tracesHint = new QLabel(
        u"2+ traces: Y normalized · hover = SI values"_s,
        tracesPanel);
    tracesHint->setWordWrap(true);
    tracesHint->setStyleSheet(u"color: #7a8796; font-size: 10px;"_s);
    tracesOuter->addWidget(tracesHint);

    auto *traceScroll = new QScrollArea(tracesPanel);
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
    tracesOuter->addWidget(traceScroll, 1);

    m_metricEnabled = {true, true, true, false, false, false, false, false, false};
    syncCheckboxStatesFromFlags();

    auto *chartColumn = new QVBoxLayout();
    chartColumn->setSpacing(0);
    chartColumn->setContentsMargins(0, 0, 0, 0);

    auto *chartFrame = new QFrame(this);
    chartFrame->setObjectName(u"chartFrame"_s);
    auto *chartFrameLayout = new QVBoxLayout(chartFrame);
    chartFrameLayout->setContentsMargins(0, 0, 0, 0);
    chartFrameLayout->setSpacing(0);

    auto *chartHeader = new QWidget(chartFrame);
    auto *chartHeaderLay = new QVBoxLayout(chartHeader);
    chartHeaderLay->setContentsMargins(6, 6, 6, 4);
    chartHeaderLay->setSpacing(4);

    auto *chartToolbar = new QHBoxLayout();
    chartToolbar->setSpacing(6);

    m_zoomOutBtn = new QPushButton(u"−"_s, chartHeader);
    m_zoomOutBtn->setObjectName(u"chartZoomBtn"_s);
    m_zoomOutBtn->setToolTip(u"Zoom out ×2 (axis ranges, same idea as wheel)"_s);
    chartToolbar->addWidget(m_zoomOutBtn);

    m_zoomInBtn = new QPushButton(u"+"_s, chartHeader);
    m_zoomInBtn->setObjectName(u"chartZoomBtn"_s);
    m_zoomInBtn->setToolTip(u"Zoom in ×2 (axis ranges, same idea as wheel)"_s);
    chartToolbar->addWidget(m_zoomInBtn);

    m_zoomResetBtn = new QPushButton(u"Fit"_s, chartHeader);
    m_zoomResetBtn->setObjectName(u"chartToolbarBtn"_s);
    m_zoomResetBtn->setToolTip(u"Reset zoom to full data range"_s);
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
        u"Draw a dot on each sample (auto-disabled above 400 points/trace for clarity)."_s);
    chartToolbar->addWidget(m_showMarkersToggle);

    m_showPointValuesToggle = new QToolButton(chartHeader);
    m_showPointValuesToggle->setObjectName(u"chartToggleBtn"_s);
    m_showPointValuesToggle->setText(u"Values"_s);
    m_showPointValuesToggle->setCheckable(true);
    m_showPointValuesToggle->setChecked(false);
    m_showPointValuesToggle->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_showPointValuesToggle->setToolTip(
        u"Show Y value next to each point — single trace only, max 100 points (engineering Y, not normalized)."_s);
    chartToolbar->addWidget(m_showPointValuesToggle);

    connect(m_showMarkersToggle, &QToolButton::toggled, this, &DashboardPage::onChartVisualOptionsToggled);
    connect(m_showPointValuesToggle, &QToolButton::toggled, this, &DashboardPage::onChartVisualOptionsToggled);

    chartToolbar->addStretch(1);
    chartHeaderLay->addLayout(chartToolbar);

    m_chartInteractionHint = new QLabel(
        u"Drag: pan · Ctrl+drag: zoom box · Trackpad: two-finger pan · ⌘/Ctrl+scroll: zoom · Wheel: zoom · − / + / Fit"_s,
        chartHeader);
    m_chartInteractionHint->setObjectName(u"chartInteractionHint"_s);
    m_chartInteractionHint->setWordWrap(true);
    chartHeaderLay->addWidget(m_chartInteractionHint);

    m_hoverReadoutLabel = new QLabel(chartHeader);
    m_hoverReadoutLabel->setObjectName(u"chartHoverReadout"_s);
    m_hoverReadoutLabel->setWordWrap(true);
    m_hoverReadoutLabel->setMinimumHeight(40);
    updateHoverReadoutDefault();
    chartHeaderLay->addWidget(m_hoverReadoutLabel);

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

    m_liveChartCoalesceTimer = new QTimer(this);
    m_liveChartCoalesceTimer->setSingleShot(true);
    connect(m_liveChartCoalesceTimer, &QTimer::timeout, this, [this]() {
        rebuildLiveSeriesFromHistory();
        updateChartStatsLabel();
    });

    auto *tcv = new TelemetryChartView(m_chart, chartFrame);
    tcv->setObjectName(u"telemetryChartView"_s);
    tcv->setToolTip(
        u"Left drag: pan (map-style). Ctrl+left drag: zoom rectangle. "
        u"Trackpad: two-finger scroll pans; ⌘ or Ctrl + two-finger scroll zooms at pointer. "
        u"Mouse wheel: zoom at pointer. − / + / Fit: zoom. Hover: sample readout."_s);
    tcv->onUserAdjustedAxes = [this]() {
        m_preserveChartAxes = true;
    };
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

    chartFrameLayout->addWidget(m_chartView, 1);

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
    m_chart->setMargins(QMargins(2, 2, 2, 6));
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
    updateChartStatsLabel();
}

void DashboardPage::updateHoverReadoutDefault() {
    if (m_hoverReadoutLabel) {
        m_hoverReadoutLabel->setText(
            u"Hover: time, row #, all trace values. Drag: pan · Ctrl+drag: zoom box · "
            u"Trackpad: two-finger pan · ⌘/Ctrl+scroll: zoom · Wheel: zoom · − / + / Fit."_s);
    }
}

void DashboardPage::onResetChartZoom() {
    refreshAllSeriesFromData();
}

void DashboardPage::onChartVisualOptionsToggled() {
    refreshAllSeriesFromData();
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

void DashboardPage::syncReplayTransportChrome() {
    if (!m_playPauseBtn) {
        return;
    }
    const bool live = !m_model || !m_model->replayMode();
    const bool hasLog = m_session && !m_session->samples.empty();
    const bool canUse = !live && hasLog && m_replay;

    if (m_replayBarTitle) {
        m_replayBarTitle->setText(live ? u"LIVE BUFFER"_s : u"REPLAY"_s);
    }
    if (m_replay && style()) {
        const QIcon icon = style()->standardIcon(m_replay->isPlaying() ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay);
        m_playPauseBtn->setIcon(icon);
    }
    m_playPauseBtn->setEnabled(canUse);
    if (m_stopBtn) {
        m_stopBtn->setEnabled(canUse);
    }
    if (m_jumpStartBtn) {
        m_jumpStartBtn->setEnabled(canUse);
    }
    if (m_jumpEndBtn) {
        m_jumpEndBtn->setEnabled(canUse);
    }
    if (m_speedCombo) {
        m_speedCombo->setEnabled(canUse);
    }
    if (m_replaySlider) {
        if (live || !hasLog) {
            m_replaySlider->setEnabled(false);
        } else {
            m_replaySlider->setEnabled(static_cast<int>(m_session->samples.size()) > 0);
        }
    }
}

void DashboardPage::updateReplayPanel() {
    if (!m_replayInfoLabel) {
        return;
    }
    if (!m_model) {
        m_replayInfoLabel->setText(u""_s);
        if (m_replayTimeLeftLabel) {
            m_replayTimeLeftLabel->setText(u"—"_s);
        }
        if (m_replayTimeRightLabel) {
            m_replayTimeRightLabel->setText(u"—"_s);
        }
        syncReplayTransportChrome();
        return;
    }
    if (!m_model->replayMode()) {
        const int n = static_cast<int>(m_liveSamples.size());
        if (m_replayTimeLeftLabel) {
            m_replayTimeLeftLabel->setText(u"—"_s);
        }
        if (m_replayTimeRightLabel) {
            m_replayTimeRightLabel->setText(u"—"_s);
        }
        m_replayInfoLabel->setText(
            QStringLiteral("Live capture · %1 samples in memory · connect serial on Monitoring.").arg(n));
        syncReplayTransportChrome();
        return;
    }

    if (!m_session || m_session->samples.empty()) {
        if (m_replayTimeLeftLabel) {
            m_replayTimeLeftLabel->setText(u"—"_s);
        }
        if (m_replayTimeRightLabel) {
            m_replayTimeRightLabel->setText(u"—"_s);
        }
        m_replayInfoLabel->setText(
            u"No flight file loaded. Open a log from the toolbar, or switch to Monitoring for live data."_s);
        syncReplayTransportChrome();
        return;
    }

    const auto &samples = m_session->samples;
    const int n = static_cast<int>(samples.size());
    const int head = std::clamp(m_lastReplayTrailLength, 0, n);
    const long tRef = samples.front().timestamp;
    const bool sessionElapsed = useSessionElapsedTimeAxis(samples.front().timestamp, samples.back().timestamp);
    const double tLogStart = chartXSeconds(tRef, samples.front().timestamp, sessionElapsed);
    const double tLogEnd = chartXSeconds(tRef, samples.back().timestamp, sessionElapsed);
    const double fullDur = std::max(0.0, tLogEnd - tLogStart);

    if (m_replayTimeLeftLabel && m_replayTimeRightLabel) {
        const double elapsedAlongLog =
            (head > 0)
                ? (chartXSeconds(tRef, samples[static_cast<std::size_t>(head - 1)].timestamp, sessionElapsed) - tLogStart)
                : 0.0;
        m_replayTimeLeftLabel->setText(formatReplayClockHms(elapsedAlongLog));
        m_replayTimeRightLabel->setText(formatReplayClockHms(fullDur));
    }

    const double speedShown = m_replay ? m_replay->speed() : 1.0;
    const QString state = m_replayActivityText.isEmpty() ? u"Ready"_s : m_replayActivityText;

    QString detail;
    if (head <= 0) {
        detail = u"Scrub right to reveal samples on the chart."_s;
    } else {
        const double tVisEnd =
            chartXSeconds(tRef, samples[static_cast<std::size_t>(head - 1)].timestamp, sessionElapsed);
        detail = QStringLiteral("Samples 1–%1 of %2 · visible %3–%4 s · full log to %5 s")
                     .arg(head)
                     .arg(n)
                     .arg(tLogStart, 0, 'f', 2)
                     .arg(tVisEnd, 0, 'f', 2)
                     .arg(tLogEnd, 0, 'f', 2);
    }

    m_replayInfoLabel->setText(
        QStringLiteral("%1 · %2× — %3").arg(state).arg(speedShown, 0, 'f', 2).arg(detail));
    syncReplayTransportChrome();
}

void DashboardPage::setReplaySession(const FlightSession *session) {
    m_preserveChartAxes = false;
    m_session = session;
    const int n = session ? static_cast<int>(session->samples.size()) : 0;
    if (m_replaySlider) {
        m_replaySlider->setMaximum(std::max(0, n));
        m_replaySlider->setEnabled(n > 0);
    }
    if (m_replay && m_speedCombo) {
        const QSignalBlocker blocker(m_speedCombo);
        const double spd = m_replay->speed();
        int bestIdx = 0;
        double bestDiff = 1e9;
        for (int i = 0; i < m_speedCombo->count(); ++i) {
            const double v = m_speedCombo->itemData(i).toDouble();
            const double d = std::abs(v - spd);
            if (d < bestDiff) {
                bestDiff = d;
                bestIdx = i;
            }
        }
        m_speedCombo->setCurrentIndex(bestIdx);
        if (bestDiff > 0.01) {
            m_replay->setSpeed(m_speedCombo->currentData().toDouble());
        }
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
    m_preserveChartAxes = false;
    m_lastReplayTrailLength = trailLength;
    rebuildReplayCharts(trailLength);
}

void DashboardPage::refreshAllSeriesFromData() {
    m_preserveChartAxes = false;
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
        m_preserveChartAxes = false;
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
    updateChartStatsLabel();
    updateReplayPanel();
}

void DashboardPage::onSessionReset() {
    m_preserveChartAxes = false;
    if (m_liveChartCoalesceTimer) {
        m_liveChartCoalesceTimer->stop();
    }
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
    if (m_model && !m_model->replayMode()) {
        rebuildLiveSeriesFromHistory();
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
        m_preserveChartAxes = false;
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
    updateChartStatsLabel();
    updateReplayPanel();
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
    scheduleLiveChartRebuild();
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
