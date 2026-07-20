#include "gui/widgets/TelemetryChartView.h"

#include "gui/Theme.h"
#include "gui/ThemeManager.h"

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
#include <limits>
#include <utility>

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
    int crosshairY = -1;
    int snapX = -1, snapY = -1;
    QColor snapColor;

    explicit ChartCrosshairOverlay(QWidget *parent) : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAutoFillBackground(false);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (crosshairX < 0 && crosshairY < 0) return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);
        // Use theme-aware crosshair color
        QColor crosshairColor(Theme::kTextPrimary());
        crosshairColor.setAlpha(100);
        p.setPen(QPen(crosshairColor, 1));
        if (crosshairX >= 0)
            p.drawLine(crosshairX, 0, crosshairX, height());
        if (crosshairY >= 0)
            p.drawLine(0, crosshairY, width(), crosshairY);

        if (snapX >= 0 && snapY >= 0 && snapColor.isValid()) {
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(QPen(snapColor.darker(130), 2));
            p.setBrush(snapColor);
            p.drawEllipse(QPoint(snapX, snapY), 5, 5);
        }
    }
};

/** Returns the first visible non-empty QLineSeries from @p chart, or nullptr. */
QLineSeries *firstVisibleNonEmptyLineSeries(QChart *chart)
{
    if (!chart) return nullptr;
    for (QAbstractSeries *s : chart->series()) {
        auto *ls = qobject_cast<QLineSeries *>(s);
        if (ls && ls->isVisible() && ls->count() > 0)
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
    const qsizetype boundedSize = std::min(
        pts.size(), static_cast<qsizetype>(std::numeric_limits<int>::max()));
    const int n = static_cast<int>(boundedSize);
    if (n <= 0) return -1;
    if (n == 1) return 0;
    int lo = 0, hi = n - 1;
    while (lo < hi - 1) {
        const int mid = (lo + hi) / 2;
        if (pts[mid].x() <= tx) lo = mid; else hi = mid;
    }
    return std::abs(pts[lo].x() - tx) <= std::abs(pts[hi].x() - tx) ? lo : hi;
}

/** Returns the index of the sorted X value closest to @p tx. */
int nearestValueIndexByX(const QVector<double> &values, double tx)
{
    const qsizetype boundedSize = std::min(
        values.size(), static_cast<qsizetype>(std::numeric_limits<int>::max()));
    const int n = static_cast<int>(boundedSize);
    if (n <= 0) return -1;
    if (n == 1) return 0;
    int lo = 0;
    int hi = n - 1;
    while (lo < hi - 1) {
        const int mid = (lo + hi) / 2;
        if (values[mid] <= tx) lo = mid; else hi = mid;
    }
    return std::abs(values[lo] - tx) <= std::abs(values[hi] - tx) ? lo : hi;
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
    setFocusPolicy(Qt::StrongFocus);
    setContextMenuPolicy(Qt::NoContextMenu);
    setDragMode(QGraphicsView::NoDrag);

    // Crosshair overlay — full viewport, always raised above chart content.
    m_crosshairOverlay = new ChartCrosshairOverlay(viewport());
    m_crosshairOverlay->resize(viewport()->size());
    m_crosshairOverlay->show();

    // Floating text overlay — positioned near cursor, hidden by default.
    m_hoverOverlay = new QLabel(viewport());
    m_hoverOverlay->setWordWrap(true);
    m_hoverOverlay->setMaximumWidth(420);
    refreshHoverOverlayStyleSheet();
    m_hoverOverlay->hide();
    m_crosshairOverlay->raise();

    connect(&cosmo::ThemeManager::instance(), &cosmo::ThemeManager::themeChanged,
            this, &TelemetryChartView::refreshHoverOverlayStyleSheet);

    viewport()->installEventFilter(this);
}

void TelemetryChartView::setChart(QChart *c)
{
    m_chartPtr = c;
    QChartView::setChart(c);
    m_hoverXValues.clear();
    invalidateHoverSeriesCache();
}

void TelemetryChartView::setHoverXValues(QVector<double> values)
{
    m_hoverXValues = std::move(values);
}

void TelemetryChartView::invalidateHoverSeriesCache()
{
    m_hoverSeriesCacheDirty = true;
    hideHoverOverlays();
}

void TelemetryChartView::refreshHoverOverlayStyleSheet()
{
    if (!m_hoverOverlay) return;

    m_hoverOverlay->setStyleSheet(
        QString(u"background-color: %1;"
                u"color: %2;"
                u"font-size: %3px;"
                u"font-family: %4;"
                u"border: 1px solid %5;"
                u"border-radius: 6px;"
                u"padding: 6px 8px;"_s)
            .arg(Theme::kBgDark())
            .arg(Theme::kTextPrimary())
            .arg(Theme::kFontSizeSm)
            .arg(Theme::kFontMono)
            .arg(Theme::kBorderPanel()));

    // Force crosshair overlay repaint with new theme colors
    if (m_crosshairOverlay) {
        m_crosshairOverlay->update();
    }
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
        if (++m_hoverThrottleCounter % 3 == 0)
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
    if (!axX) { QChartView::wheelEvent(event); return; }

    QList<QValueAxis *> yAxes;
    for (auto *a : m_chartPtr->axes(Qt::Vertical)) {
        auto *ya = qobject_cast<QValueAxis *>(a);
        if (ya && ya->max() - ya->min() > 0.0) yAxes.append(ya);
    }
    if (yAxes.isEmpty()) { QChartView::wheelEvent(event); return; }

    const double spanX = axX->max() - axX->min();
    if (spanX <= 0.0) { event->accept(); return; }

    double ax = axX->min() + std::clamp((ep.x() - plot.left()) / plot.width(), 0.0, 1.0) * spanX;
    if (QLineSeries *ref = firstVisibleNonEmptyLineSeries(m_chartPtr)) {
        const QPointF v = m_chartPtr->mapToValue(m_chartPtr->mapFromScene(mapToScene(ep.toPoint())), ref);
        ax = v.x();
    }
    const double yFrac = std::clamp((plot.bottom() - ep.y()) / plot.height(), 0.0, 1.0);

    auto zoomAllAtFocal = [&](double spanScale) {
        zoomAxisAtFocal(axX, ax, spanScale);
        for (auto *ya : yAxes) {
            const double focal = ya->min() + yFrac * (ya->max() - ya->min());
            zoomAxisAtFocal(ya, focal, spanScale);
        }
    };

    const QPoint pixelDelta = event->pixelDelta();
    const bool hasPixel     = (pixelDelta.x() != 0 || pixelDelta.y() != 0);
    const int  angleY       = event->angleDelta().y();
    const int  angleX       = event->angleDelta().x();
    const bool zoomMod      = (event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)) != 0;

    auto zoomStep = [&](int steps) {
        double f = 1.0;
        for (int i = 0; i < std::abs(steps); ++i)
            f *= (steps > 0) ? 0.88 : (1.0 / 0.88);
        zoomAllAtFocal(f);
        if (onUserAdjustedAxes) onUserAdjustedAxes();
        event->accept();
    };

    if (zoomMod) {
        if (pixelDelta.y() != 0) {
            double s = std::exp(-static_cast<double>(pixelDelta.y()) * 0.0021);
            s = std::clamp(s, 0.9, 1.11);
            zoomAllAtFocal(s);
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
        auto *co = static_cast<ChartCrosshairOverlay *>(m_crosshairOverlay);
        co->crosshairX = -1;
        co->crosshairY = -1;
        co->snapX = -1;
        co->snapY = -1;
        co->update();
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
    if (!axX) return;

    QList<QValueAxis *> yAxes;
    for (auto *a : m_chartPtr->axes(Qt::Vertical)) {
        auto *ay = qobject_cast<QValueAxis *>(a);
        if (ay) yAxes.append(ay);
    }

    const double dValX = static_cast<double>(delta.x()) * (axX->max() - axX->min()) / plot.width();

    // Block signals on all axes except the last one updated so the chart
    // scene only repaints once instead of once per axis.
    const bool hadBlockX = axX->signalsBlocked();
    axX->blockSignals(true);
    axX->setRange(axX->min() - dValX, axX->max() - dValX);

    for (int i = 0; i < yAxes.size(); ++i) {
        QValueAxis *ay = yAxes[i];
        const double dValY = static_cast<double>(delta.y()) * (ay->max() - ay->min()) / plot.height();
        const bool isLast = (i == yAxes.size() - 1);
        if (!isLast) {
            const bool had = ay->signalsBlocked();
            ay->blockSignals(true);
            ay->setRange(ay->min() + dValY, ay->max() + dValY);
            ay->blockSignals(had);
        } else {
            // Unblock X before the last axis update triggers the repaint
            axX->blockSignals(hadBlockX);
            ay->setRange(ay->min() + dValY, ay->max() + dValY);
        }
    }
    if (yAxes.isEmpty()) {
        axX->blockSignals(hadBlockX);
        // Re-fire a rangeChanged so the chart repaints once
        axX->setRange(axX->min(), axX->max());
    }
}

void TelemetryChartView::zoomAxisAtFocal(QValueAxis *ax, double focal, double spanScale)
{
    ax->setRange(focal - (focal - ax->min()) * spanScale, focal + (ax->max() - focal) * spanScale);
}

void TelemetryChartView::rebuildHoverSeriesCache()
{
    m_hoverSeriesCache.clear();
    m_hoverSeriesCacheDirty = false;
    if (!m_chartPtr) {
        return;
    }

    const QList<QAbstractSeries *> chartSeries = m_chartPtr->series();
    m_hoverSeriesCache.reserve(static_cast<std::size_t>(chartSeries.size()));
    for (QAbstractSeries *abstractSeries : chartSeries) {
        auto *lineSeries = qobject_cast<QLineSeries *>(abstractSeries);
        if (!lineSeries || !lineSeries->isVisible() || lineSeries->count() <= 0) {
            continue;
        }

        QList<QPointF> points = lineSeries->points();
        points.erase(
            std::remove_if(points.begin(), points.end(), [](const QPointF &point) {
                return !std::isfinite(point.x()) || !std::isfinite(point.y());
            }),
            points.end());
        if (points.isEmpty()) {
            continue;
        }
        std::sort(points.begin(), points.end(), [](const QPointF &left, const QPointF &right) {
            return left.x() < right.x();
        });
        m_hoverSeriesCache.push_back({lineSeries, std::move(points)});
    }
}

void TelemetryChartView::updateHoverReadoutAt(const QPoint &widgetPos)
{
    if (!m_chartPtr) return;

    if (m_hoverSeriesCacheDirty) {
        rebuildHoverSeriesCache();
    }
    if (m_hoverSeriesCache.empty()) {
        hideHoverOverlays();
        if (hoverReadout) hoverReadout(u"—"_s);
        return;
    }

    auto *ref = qobject_cast<QLineSeries *>(m_hoverSeriesCache.front().series.data());
    if (!ref) {
        invalidateHoverSeriesCache();
        return;
    }
    const QList<QPointF> &referencePoints = m_hoverSeriesCache.front().points;
    const QPointF scenePos = mapToScene(widgetPos);
    const QPointF chartPos = m_chartPtr->mapFromScene(scenePos);
    const QPointF plotVals = m_chartPtr->mapToValue(chartPos, ref);
    const bool hasLogicalLookup = !m_hoverXValues.isEmpty();
    const int logicalIndex = hasLogicalLookup
        ? nearestValueIndexByX(m_hoverXValues, plotVals.x())
        : nearestIndexByX(referencePoints, plotVals.x());
    if (logicalIndex < 0) return;
    const double hoverX = hasLogicalLookup
        ? m_hoverXValues[logicalIndex]
        : referencePoints[logicalIndex].x();

    QLineSeries *closestSeries = nullptr;
    QPointF closestPoint;
    double closestDistSq = -1.0;
    for (const HoverSeriesCacheEntry &entry : m_hoverSeriesCache) {
        auto *lineSeries = qobject_cast<QLineSeries *>(entry.series.data());
        if (!lineSeries || !lineSeries->isVisible()) {
            continue;
        }
        const int seriesIndex = nearestIndexByX(entry.points, hoverX);
        if (seriesIndex < 0) {
            continue;
        }
        const QPointF candidate = entry.points[seriesIndex];
        const QPointF chartPoint = m_chartPtr->mapToPosition(candidate, lineSeries);
        const QPointF scenePoint = m_chartPtr->mapToScene(chartPoint);
        const QPointF viewportPoint = mapFromScene(scenePoint);
        const double dx = viewportPoint.x() - widgetPos.x();
        const double dy = viewportPoint.y() - widgetPos.y();
        const double distanceSquared = dx * dx + dy * dy;
        if (closestDistSq < 0.0 || distanceSquared < closestDistSq) {
            closestDistSq = distanceSquared;
            closestSeries = lineSeries;
            closestPoint = candidate;
        }
    }

    int snapWidgetX = -1, snapWidgetY = -1;
    QColor snapCol;
    if (closestSeries) {
        const QPointF cPt = m_chartPtr->mapToPosition(closestPoint, closestSeries);
        const QPointF sPt = m_chartPtr->mapToScene(cPt);
        const QPointF vPt = mapFromScene(sPt);
        snapWidgetX = static_cast<int>(std::round(vPt.x()));
        snapWidgetY = static_cast<int>(std::round(vPt.y()));
        snapCol = closestSeries->color();
    }

    QString text;
    const qsizetype lookupSize = hasLogicalLookup
        ? m_hoverXValues.size()
        : referencePoints.size();
    const int totalSamples = static_cast<int>(std::min(
        lookupSize, static_cast<qsizetype>(std::numeric_limits<int>::max())));
    if (hoverDetail) {
        text = hoverDetail(hoverX, logicalIndex + 1, totalSamples);
    } else {
        text = QStringLiteral("t=%1 s · #%2 / %3")
                   .arg(hoverX, 0, 'f', 3)
                   .arg(logicalIndex + 1)
                   .arg(totalSamples);
    }

    if (hoverReadout) hoverReadout(text);

    if (m_crosshairOverlay) {
        auto *co = static_cast<ChartCrosshairOverlay *>(m_crosshairOverlay);
        co->crosshairX = widgetPos.x();
        co->crosshairY = snapWidgetY;
        co->snapX = snapWidgetX;
        co->snapY = snapWidgetY;
        co->snapColor = snapCol;
        co->update();
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
