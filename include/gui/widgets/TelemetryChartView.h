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
    /**
     * Called with the formatted hover text on every mouse-move over the chart,
     * and with an empty string when the cursor leaves.  Set to nullptr to ignore.
     */
    std::function<void(const QString &)> hoverReadout;

    /**
     * Returns the multi-line text for the floating detail overlay.
     * @param tSec              X-axis value at the cursor (seconds).
     * @param sampleIndex1Based 1-based index of the nearest display point.
     * @param totalSamples      Total number of display points in the series.
     */
    std::function<QString(double tSec, int sampleIndex1Based, int totalSamples)> hoverDetail;

    /**
     * Called after any user-initiated pan or zoom so the owning widget can
     * set a "preserve axes" flag and suppress automatic rescaling on rebuild.
     */
    std::function<void()> onUserAdjustedAxes;

    explicit TelemetryChartView(QChart *chart, QWidget *parent = nullptr);

    /**
     * Updates the internally stored chart pointer.
     * Call this if the owning widget replaces the QChart instance.
     */
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
    static void zoomAxisAtFocal(QValueAxis *ax, double focal, double spanScale);
    void refreshHoverOverlayStyleSheet();

    QChart  *m_chartPtr          = nullptr;
    bool     m_panning           = false;
    bool     m_rubberZoomActive  = false;
    QPoint   m_lastPanPos;

    QWidget *m_crosshairOverlay = nullptr;  ///< ChartCrosshairOverlay instance (type defined in .cpp).
    QLabel  *m_hoverOverlay     = nullptr;  ///< Floating text bubble that follows the cursor.

    int      m_hoverThrottleCounter = 0;    ///< Skips N hover updates during pan for performance.
};
