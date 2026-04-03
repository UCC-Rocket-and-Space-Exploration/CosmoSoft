/**
 * @file TelemetryChartView.h
 * @brief Custom QChartView with pan/zoom, hover crosshair, and floating readout.
 *
 * Interaction model:
 *  - Left drag          → pan (both axes shift uniformly)
 *  - Ctrl + left drag   → rubber-band zoom rectangle
 *  - Scroll wheel       → zoom at cursor position
 *  - Two-finger swipe   → pan (pixel delta from QWheelEvent)
 *  - Cmd/Ctrl + swipe   → zoom at cursor
 *  - Mouse hover        → floating text overlay + vertical crosshair
 *
 * Callers wire three std::function callbacks:
 *  - hoverReadout(text) — fired on every hover (empty = cleared)
 *  - hoverDetail(tSec, idx1Based, total) → returns formatted multi-metric text
 *  - onUserAdjustedAxes() — fired after any pan/zoom
 */

#pragma once

#include <QChartView>
#include <functional>

class QLabel;
class QValueAxis;

class TelemetryChartView : public QChartView {
public:
    std::function<void(const QString &)> hoverReadout;
    std::function<QString(double tSec, int sampleIndex1Based, int totalSamples)> hoverDetail;
    std::function<void()> onUserAdjustedAxes;

    explicit TelemetryChartView(QChart *chart, QWidget *parent = nullptr);

    /** Update the internally stored chart pointer (e.g. after replacing the chart). */
    void setChart(QChart *c);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void hideHoverOverlays();
    void updateHoverReadoutAt(const QPoint &widgetPos);
    void endPanningIfActive();
    void panAxesByPixels(const QPoint &delta);
    static void zoomValueAxesAtFocal(QValueAxis *axX, QValueAxis *axY,
                                     double ax, double ay, double spanScale);

    QChart  *m_chartPtr          = nullptr;
    bool     m_panning           = false;
    bool     m_rubberZoomActive  = false;
    QPoint   m_lastPanPos;

    QWidget *m_crosshairOverlay = nullptr;  ///< ChartCrosshairOverlay (defined in .cpp)
    QLabel  *m_hoverOverlay     = nullptr;
};
