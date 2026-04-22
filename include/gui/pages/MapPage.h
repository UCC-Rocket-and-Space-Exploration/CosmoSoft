/**
 * @file MapPage.h
 * @brief 2-D lat/lon trajectory chart showing the flight path on a vector plot.
 *
 * MapPage uses a QChart with a single QLineSeries (longitude on X, latitude on Y)
 * and does not require an external map-tile library.  The chart redraws on every
 * new sample in live mode or on replay position changes via setReplayTrailLength().
 *
 * A "Fit" toolbar button resets the axes to the full data extent.
 */

#ifndef COSMO_SOFT_MAPPAGE_H
#define COSMO_SOFT_MAPPAGE_H

#include <QWidget>

#include <vector>

#include "domain/FlightSample.h"
#include "domain/FlightSession.h"

class QChart;
class QChartView;
class QLineSeries;
class QPushButton;
class QValueAxis;
class FlightDataModel;
class FlightReplayController;

/**
 * @class MapPage
 * @brief Renders a latitude/longitude flight-path plot using Qt Charts.
 *
 * In live mode the page appends each new sample to an internal buffer and
 * updates the series incrementally.  In replay mode the visible trail is
 * controlled externally via setReplayTrailLength(); the series is rebuilt from
 * the loaded session up to the current trail length.
 */
class MapPage : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Constructs the page and subscribes to @p model.
     * @param model Non-owning pointer to the shared flight data model.
     * @param replay Non-owning pointer to the replay controller (used for position sync).
     * @param parent Optional parent widget.
     */
    explicit MapPage(FlightDataModel *model,
                     FlightReplayController *replay,
                     QWidget *parent = nullptr);
    ~MapPage() override = default;

    /**
     * @brief Loads @p session for replay path display.
     * Pass nullptr to return to live mode and clear the chart.
     */
    void setReplaySession(const FlightSession *session);

    /**
     * @brief Rebuilds the path series to show the first @p trailLength samples.
     * Called by MainWindow on every FlightReplayController::positionChanged signal.
     */
    void setReplayTrailLength(int trailLength);

private slots:
    void onSampleUpdated(const FlightSample &sample);
    void onSessionReset();
    void onFitAxes();

private:
    void rebuildSeries(int trailLength);
    void fitAxesToData();
    void applyChartTheme();

    FlightDataModel        *m_model   = nullptr;
    FlightReplayController *m_replay  = nullptr;
    const FlightSession    *m_session = nullptr;

    QChart      *m_chart    = nullptr;
    QChartView  *m_chartView = nullptr;
    QLineSeries *m_series   = nullptr;
    QValueAxis  *m_axisX    = nullptr;
    QValueAxis  *m_axisY    = nullptr;
    QPushButton *m_fitBtn   = nullptr;

    std::vector<FlightSample> m_liveSamples;
};

#endif // COSMO_SOFT_MAPPAGE_H
