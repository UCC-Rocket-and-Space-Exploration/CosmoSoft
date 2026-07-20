/**
 * @file LiveTelemetryPage.h
 * @brief Live telemetry operations page.
 *
 * The page displays live samples from FlightDataModel with a reusable map,
 * metric tiles, serial device controls, and a disabled ignitor panel.
 */

#ifndef COSMO_SOFT_LIVETELEMETRYPAGE_H
#define COSMO_SOFT_LIVETELEMETRYPAGE_H

#include "domain/FlightSample.h"

#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <array>

class QComboBox;
class QFrame;
class QGridLayout;
class QLabel;
class QPushButton;
class QResizeEvent;
class QScrollArea;
class QShowEvent;
class QToolButton;
class QVBoxLayout;
class FlightDataModel;
class Map3DWidget;
class StatTileWidget;

/**
 * @class LiveTelemetryPage
 * @brief Live operations screen for serial telemetry and no-hardware demo data.
 */
class LiveTelemetryPage : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Construct the live telemetry page bound to @p model.
     * @param model Shared flight data model that publishes live samples.
     * @param parent Optional Qt parent.
     */
    explicit LiveTelemetryPage(FlightDataModel *model, QWidget *parent = nullptr);
    ~LiveTelemetryPage() override = default;

    /**
     * @brief Replace the serial device list with @p ports.
     * Existing selection is preserved when possible.
     */
    void setAvailablePorts(const QStringList &ports);

    /**
     * @brief Update connection chrome.
     * @param label Human-readable active connection label.
     * @param connected True when live telemetry is connected or streaming.
     */
    void setActiveConnection(const QString &label, bool connected);

    /** @brief Clear samples, metric values, map state, and counters. */
    void resetLiveState();

    /**
     * @brief Selects metric or imperial presentation units for live telemetry.
     *
     * Incoming samples and vertical-speed calculations remain in SI units.
     */
    void setImperialUnits(bool imperial);

signals:
    /** @brief User requested serial device scanning. */
    void scanDevicesRequested();

    /** @brief User requested connection to @p port at @p baud. */
    void connectDeviceRequested(QString port, int baud);

    /** @brief User requested live connection shutdown. */
    void disconnectRequested();

    /** @brief User requested deterministic fake live telemetry. */
    void startDemoRequested();

protected:
    /** @brief Refresh deferred telemetry presentation when the page becomes visible. */
    void showEvent(QShowEvent *event) override;

    /** @brief Reflow telemetry panels to match the available viewport width. */
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void refreshStyleSheet();
    void onLiveSamplesReceived(const QVector<FlightSample> &samples);
    void onBytesReceivedChanged(qint64 totalBytes);
    void onConnectClicked();

private:
    void buildUi();
    QFrame *buildMapPanel();
    QWidget *buildMetricsPanel();
    QFrame *buildDevicePanel();
    QFrame *buildIgnitorPanel();
    QPushButton *createPanelButton(const QString &text, QWidget *parent);
    QToolButton *createMapToolButton(const QString &text, QWidget *parent);
    void refreshDeviceRows();
    void refreshTelemetryDisplay();
    void resetMetricTiles();
    void updateLastPacketLabel();
    void updateResponsiveLayout();
    void updateFollowButtonState(bool enabled);
    [[nodiscard]] QString selectedPort() const;
    [[nodiscard]] int selectedBaud() const;

    FlightDataModel *m_model = nullptr;
    Map3DWidget *m_mapWidget = nullptr;
    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_scrollContent = nullptr;
    QGridLayout *m_responsiveLayout = nullptr;
    QGridLayout *m_metricsGrid = nullptr;
    QFrame *m_mapPanel = nullptr;
    QWidget *m_metricsPanel = nullptr;
    QWidget *m_sideColumn = nullptr;
    int m_responsiveMode = -1;
    int m_metricColumnCount = 0;

    std::array<StatTileWidget *, 6> m_metricTiles{};
    QLabel *m_connectionStatusLabel = nullptr;
    QLabel *m_sampleCountLabel = nullptr;
    QLabel *m_bytesLabel = nullptr;
    QLabel *m_lastPacketLabel = nullptr;
    QComboBox *m_portCombo = nullptr;
    QComboBox *m_baudCombo = nullptr;
    QToolButton *m_followButton = nullptr;
    QPushButton *m_scanButton = nullptr;
    QPushButton *m_connectButton = nullptr;
    QPushButton *m_disconnectButton = nullptr;
    QPushButton *m_demoButton = nullptr;
    QVBoxLayout *m_deviceRowsLayout = nullptr;

    QStringList m_availablePorts;
    QString m_activeConnectionLabel;
    bool m_connected = false;
    int m_sampleCount = 0;
    qint64 m_totalBytes = 0;
    FlightSample m_previousSample;
    bool m_havePreviousSample = false;
    FlightSample m_latestDisplaySample;
    double m_latestVelocity = 0.0;
    bool m_haveLatestDisplaySample = false;
    bool m_imperialUnits = false;
};

#endif // COSMO_SOFT_LIVETELEMETRYPAGE_H
