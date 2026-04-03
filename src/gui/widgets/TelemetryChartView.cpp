#include "gui/widgets/TelemetryChartView.h"

#include <QChart>
#include <QFocusEvent>
#include <QLabel>
#include <QLineSeries>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPaintEvent>
#include <QValueAxis>
#include <QWheelEvent>
#include <QWidget>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

/**
 * Transparent full-viewport overlay that draws the vertical hover crosshair.
 *
 * Created as a child of TelemetryChartView::viewport() so it renders on top of
 * the chart content.  WA_TransparentForMouseEvents ensures it never intercepts
 * pan/zoom input.
 */
class ChartCrosshairOverlay : public QWidget {
public:
    int crosshairX = -1;

    explicit ChartCrosshairOverlay(QWidget *parent) : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAutoFillBackground(false);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (crosshairX < 0) return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(QPen(QColor(255, 255, 255, 100), 1));
        p.drawLine(crosshairX, 0, crosshairX, height());
    }
};

/** Returns the first visible non-empty QLineSeries from @p chart, or nullptr. */
QLineSeries *firstVisibleNonEmptyLineSeries(QChart *chart)
{
    if (!chart) return nullptr;
    for (QAbstractSeries *s : chart->series()) {
        auto *ls = qobject_cast<QLineSeries *>(s);
        if (ls && ls->isVisible() && !ls->points().isEmpty())
            return ls;
    }
    return nullptr;
}

/**
 * Binary-searches @p pts for the point whose X coordinate is closest to @p tx.
 * Returns -1 if the list is empty.
 */
int nearestIndexByX(const QList<QPointF> &pts, double tx)
{
    const int n = pts.size();
    if (n <= 0) return -1;
    if (n == 1) return 0;
    int lo = 0, hi = n - 1;
    while (lo < hi - 1) {
        const int mid = (lo + hi) / 2;
        if (pts[mid].x() <= tx) lo = mid; else hi = mid;
    }
    return std::abs(pts[lo].x() - tx) <= std::abs(pts[hi].x() - tx) ? lo : hi;
}

} // namespace

// ── TelemetryChartView ───────────────────────────────────────────────────────

TelemetryChartView::TelemetryChartView(QChart *c, QWidget *parent)
    : QChartView(c, parent), m_chartPtr(c)
{
    setRubberBand(QChartView::NoRubberBand);
    setRenderHint(QPainter::Antialiasing);
    setFrameShape(QFrame::NoFrame);
    setContentsMargins(0, 0, 0, 0);
    setViewportMargins(0, 0, 0, 0);
    setMinimumHeight(280);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
    setContextMenuPolicy(Qt::NoContextMenu);
    setDragMode(QGraphicsView::NoDrag);

    // Crosshair overlay — full viewport, always raised above chart content.
    m_crosshairOverlay = new ChartCrosshairOverlay(viewport());
    m_crosshairOverlay->resize(viewport()->size());
    m_crosshairOverlay->show();

    // Floating text overlay — positioned near cursor, hidden by default.
    m_hoverOverlay = new QLabel(viewport());
    m_hoverOverlay->setWordWrap(true);
    m_hoverOverlay->setMaximumWidth(300);
    m_hoverOverlay->setStyleSheet(
        u"background-color: rgba(10,11,14,220);"
        u"color: #c8d4e0;"
        u"font-size: 11px;"
        u"font-family: 'Red Hat Mono', 'Courier New', monospace;"
        u"border: 1px solid #4a4d56;"
        u"border-radius: 6px;"
        u"padding: 6px 8px;"_s);
    m_hoverOverlay->hide();
    m_crosshairOverlay->raise();

    viewport()->installEventFilter(this);
}

void TelemetryChartView::setChart(QChart *c)
{
    m_chartPtr = c;
}

void TelemetryChartView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_chartPtr) {
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

void TelemetryChartView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panning && m_chartPtr && (event->buttons() & Qt::LeftButton)) {
        const QPoint delta = event->pos() - m_lastPanPos;
        m_lastPanPos = event->pos();
        if (delta.x() != 0 || delta.y() != 0) {
            panAxesByPixels(delta);
            if (onUserAdjustedAxes) onUserAdjustedAxes();
        }
        event->accept();
        updateHoverReadoutAt(event->pos());
        return;
    }
    if (!(event->buttons() & Qt::LeftButton) || (event->modifiers() & Qt::ControlModifier))
        QChartView::mouseMoveEvent(event);
    updateHoverReadoutAt(event->pos());
}

void TelemetryChartView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_panning) {
        endPanningIfActive();
        event->accept();
        return;
    }
    QChartView::mouseReleaseEvent(event);
    if (event->button() == Qt::LeftButton && m_rubberZoomActive) {
        m_rubberZoomActive = false;
        setRubberBand(QChartView::NoRubberBand);
        if (onUserAdjustedAxes) onUserAdjustedAxes();
    } else {
        setRubberBand(QChartView::NoRubberBand);
    }
}

void TelemetryChartView::wheelEvent(QWheelEvent *event)
{
    if (!m_chartPtr) { QChartView::wheelEvent(event); return; }

    const QRectF plot = m_chartPtr->plotArea();
    const QPointF ep  = event->position();
    if (!plot.contains(ep)) { QChartView::wheelEvent(event); return; }

    auto *axX = qobject_cast<QValueAxis *>(m_chartPtr->axes(Qt::Horizontal).value(0));
    auto *axY = qobject_cast<QValueAxis *>(m_chartPtr->axes(Qt::Vertical).value(0));
    if (!axX || !axY) { QChartView::wheelEvent(event); return; }

    const double spanX = axX->max() - axX->min();
    const double spanY = axY->max() - axY->min();
    if (spanX <= 0.0 || spanY <= 0.0) { event->accept(); return; }

    double ax = axX->min() + std::clamp((ep.x() - plot.left()) / plot.width(), 0.0, 1.0) * spanX;
    double ay = axY->min() + std::clamp((plot.bottom() - ep.y()) / plot.height(), 0.0, 1.0) * spanY;

    if (QLineSeries *ref = firstVisibleNonEmptyLineSeries(m_chartPtr)) {
        const QPointF v = m_chartPtr->mapToValue(m_chartPtr->mapFromScene(mapToScene(ep.toPoint())), ref);
        ax = v.x(); ay = v.y();
    }

    const QPoint pixelDelta = event->pixelDelta();
    const bool hasPixel     = (pixelDelta.x() != 0 || pixelDelta.y() != 0);
    const int  angleY       = event->angleDelta().y();
    const int  angleX       = event->angleDelta().x();
    const bool zoomMod      = (event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)) != 0;

    auto zoomStep = [&](int steps) {
        double f = 1.0;
        for (int i = 0; i < std::abs(steps); ++i)
            f *= (steps > 0) ? 0.88 : (1.0 / 0.88);
        zoomValueAxesAtFocal(axX, axY, ax, ay, f);
        if (onUserAdjustedAxes) onUserAdjustedAxes();
        event->accept();
    };

    if (zoomMod) {
        if (pixelDelta.y() != 0) {
            double s = std::exp(-static_cast<double>(pixelDelta.y()) * 0.0021);
            s = std::clamp(s, 0.9, 1.11);
            zoomValueAxesAtFocal(axX, axY, ax, ay, s);
            if (onUserAdjustedAxes) onUserAdjustedAxes();
            event->accept(); return;
        }
        if (angleY != 0) { zoomStep(angleY / 120); return; }
        event->accept(); return;
    }
    if (hasPixel) {
        panAxesByPixels(pixelDelta);
        if (onUserAdjustedAxes) onUserAdjustedAxes();
        event->accept(); return;
    }
    if (angleY != 0) { zoomStep(angleY / 120); return; }
    if (angleX != 0) {
        panAxesByPixels(QPoint(-static_cast<int>(std::lround(angleX * 0.35)), 0));
        if (onUserAdjustedAxes) onUserAdjustedAxes();
        event->accept(); return;
    }
    QChartView::wheelEvent(event);
}

void TelemetryChartView::leaveEvent(QEvent *event)
{
    if (m_panning) endPanningIfActive();
    else if (hoverReadout) hoverReadout({});
    hideHoverOverlays();
    QChartView::leaveEvent(event);
}

void TelemetryChartView::focusOutEvent(QFocusEvent *event)
{
    endPanningIfActive();
    QChartView::focusOutEvent(event);
}

bool TelemetryChartView::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == viewport() && event->type() == QEvent::Resize && m_crosshairOverlay)
        m_crosshairOverlay->resize(viewport()->size());
    return QChartView::eventFilter(obj, event);
}

// ── Private helpers ──────────────────────────────────────────────────────────

void TelemetryChartView::hideHoverOverlays()
{
    if (m_hoverOverlay) m_hoverOverlay->hide();
    if (m_crosshairOverlay) {
        static_cast<ChartCrosshairOverlay *>(m_crosshairOverlay)->crosshairX = -1;
        m_crosshairOverlay->update();
    }
}

void TelemetryChartView::endPanningIfActive()
{
    if (!m_panning) return;
    m_panning = false;
    releaseMouse();
    unsetCursor();
    setRubberBand(QChartView::NoRubberBand);
}

void TelemetryChartView::panAxesByPixels(const QPoint &delta)
{
    if (!m_chartPtr) return;
    const QRectF plot = m_chartPtr->plotArea();
    if (plot.width() <= 1.0 || plot.height() <= 1.0) return;
    auto *axX = qobject_cast<QValueAxis *>(m_chartPtr->axes(Qt::Horizontal).value(0));
    auto *axY = qobject_cast<QValueAxis *>(m_chartPtr->axes(Qt::Vertical).value(0));
    if (!axX || !axY) return;
    const double dValX = static_cast<double>(delta.x()) * (axX->max() - axX->min()) / plot.width();
    const double dValY = static_cast<double>(delta.y()) * (axY->max() - axY->min()) / plot.height();
    axX->setRange(axX->min() - dValX, axX->max() - dValX);
    axY->setRange(axY->min() + dValY, axY->max() + dValY);
}

void TelemetryChartView::zoomValueAxesAtFocal(QValueAxis *axX, QValueAxis *axY,
                                              double ax, double ay, double spanScale)
{
    axX->setRange(ax - (ax - axX->min()) * spanScale, ax + (axX->max() - ax) * spanScale);
    axY->setRange(ay - (ay - axY->min()) * spanScale, ay + (axY->max() - ay) * spanScale);
}

void TelemetryChartView::updateHoverReadoutAt(const QPoint &widgetPos)
{
    if (!m_chartPtr) return;
    QLineSeries *ref = firstVisibleNonEmptyLineSeries(m_chartPtr);
    if (!ref) {
        hideHoverOverlays();
        if (hoverReadout) hoverReadout(u"—"_s);
        return;
    }
    const QList<QPointF> pts = ref->points();
    const QPointF scenePos   = mapToScene(widgetPos);
    const QPointF chartPos   = m_chartPtr->mapFromScene(scenePos);
    const QPointF plotVals   = m_chartPtr->mapToValue(chartPos, ref);
    const int idx = nearestIndexByX(pts, plotVals.x());
    if (idx < 0) return;

    const QPointF &p = pts[idx];
    QString text;
    if (hoverDetail)
        text = hoverDetail(p.x(), idx + 1, pts.size());
    else
        text = QStringLiteral("t=%1 s · #%2 / %3").arg(p.x(), 0, 'f', 3).arg(idx + 1).arg(pts.size());

    if (hoverReadout) hoverReadout(text);

    if (m_crosshairOverlay) {
        static_cast<ChartCrosshairOverlay *>(m_crosshairOverlay)->crosshairX = widgetPos.x();
        m_crosshairOverlay->update();
    }

    if (m_hoverOverlay && !text.isEmpty()) {
        m_hoverOverlay->setText(text);
        m_hoverOverlay->adjustSize();
        constexpr int kMargin = 12;
        const int vpW = viewport()->width(), vpH = viewport()->height();
        const int oW  = m_hoverOverlay->width(), oH = m_hoverOverlay->height();
        int ox = widgetPos.x() + kMargin;
        int oy = widgetPos.y() + kMargin;
        if (ox + oW > vpW - kMargin) ox = widgetPos.x() - oW - kMargin;
        if (oy + oH > vpH - kMargin) oy = widgetPos.y() - oH - kMargin;
        ox = qBound(kMargin, ox, std::max(kMargin, vpW - oW - kMargin));
        oy = qBound(kMargin, oy, std::max(kMargin, vpH - oH - kMargin));
        m_hoverOverlay->move(ox, oy);
        m_hoverOverlay->show();
    }
}
