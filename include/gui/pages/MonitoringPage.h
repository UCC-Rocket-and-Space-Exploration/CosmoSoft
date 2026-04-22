/**
 * @file MonitoringPage.h
 * @brief Live-telemetry monitoring page displaying a 3×3 grid of stat tiles.
 *
 * MonitoringPage subscribes to FlightDataModel::sampleUpdated() and updates
 * each StatTileWidget with the corresponding metric value on every sample.
 * A status label is shown until the first sample arrives.
 */

#ifndef COSMO_SOFT_MONITORINGPAGE_H
#define COSMO_SOFT_MONITORINGPAGE_H

#include <QWidget>

#include <array>

#include "domain/FlightSample.h"

class QPaintEvent;
class QLabel;
class FlightDataModel;
class StatTileWidget;

/**
 * @class MonitoringPage
 * @brief Shows nine real-time metric tiles (altitude, temperature, pressure, etc.).
 *
 * The page paints a dark dotted background in paintEvent() and overlays a 3×3
 * grid of StatTileWidget instances whose content is driven by MetricDefs.
 */
class MonitoringPage : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Constructs the page and connects it to @p model.
     * @param model Non-owning pointer to the shared flight data model.
     * @param parent Optional parent widget.
     */
    explicit MonitoringPage(FlightDataModel *model, QWidget *parent = nullptr);
    ~MonitoringPage() override = default;

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void onSampleUpdated(const FlightSample &sample);

private:
    static constexpr int kTileCount = 9;

    FlightDataModel *m_model       = nullptr;
    QLabel          *m_statusLabel = nullptr;

    std::array<StatTileWidget *, kTileCount> m_tiles{};
};

#endif // COSMO_SOFT_MONITORINGPAGE_H
