#pragma once

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
class QListWidget;
class QPushButton;
class QShowEvent;
class QToolButton;
class FlightDataModel;
class Map3DWidget;
class StatTileWidget;

class MonitoringPage : public QWidget {
    Q_OBJECT

public:
    explicit MonitoringPage(FlightDataModel *model, QWidget *parent = nullptr);
    ~MonitoringPage() override = default;

    /** Replace the serial device list. Existing selection is preserved when possible. */
    void setAvailablePorts(const QStringList &ports);

    /** Update connection chrome. */
    void setActiveConnection(const QString &label, bool connected);

    /** Clear samples, metric values, map state, and counters. */
    void resetLiveState();

    /** Select metric or imperial presentation units. */
    void setImperialUnits(bool imperial);

signals:
    void scanDevicesRequested();
    void connectDeviceRequested(QString port, int baud);
    void disconnectRequested();
    void startDemoRequested();

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void refreshStyleSheet();
    void onLiveSamplesReceived(const QVector<FlightSample> &samples);
    void onBytesReceivedChanged(qint64 totalBytes);
    void onConnectClicked();

private:
    void buildUi();
    void buildConnectionBar(QWidget *bar);
    QWidget *buildLeftPanel();
    QWidget *buildRightPanel();
    void refreshTelemetryDisplay();
    void resetMetricTiles();
    void updateStatusCounters();
    void updateRssiBar(double rssi);
    void appendEvent(bool isError, const QString &text);
    [[nodiscard]] QString selectedPort() const;
    [[nodiscard]] int selectedBaud() const;

    FlightDataModel *m_model = nullptr;
    Map3DWidget     *m_mapWidget = nullptr;
    QToolButton     *m_followButton = nullptr;

    // Connection bar
    QLabel      *m_statusDot    = nullptr;
    QLabel      *m_statusLabel  = nullptr;
    QComboBox   *m_portCombo    = nullptr;
    QComboBox   *m_baudCombo    = nullptr;
    QPushButton *m_scanButton       = nullptr;
    QPushButton *m_connectButton    = nullptr;
    QPushButton *m_disconnectButton = nullptr;
    QPushButton *m_demoButton       = nullptr;
    QLabel      *m_samplesLabel = nullptr;
    QLabel      *m_bytesLabel   = nullptr;

    // Metric tiles (alt, vel, temp, pressure, battery, rssi)
    std::array<StatTileWidget *, 6> m_metricTiles{};

    // Link quality bar
    QFrame *m_rssiBarFill    = nullptr;
    QLabel *m_rssiValueLabel = nullptr;

    // Scrollable event log
    QListWidget *m_eventLog = nullptr;

    QStringList m_availablePorts;
    bool        m_connected              = false;
    int         m_sampleCount           = 0;
    qint64      m_totalBytes            = 0;
    FlightSample m_previousSample;
    bool         m_havePreviousSample    = false;
    FlightSample m_latestDisplaySample;
    double       m_latestVelocity       = 0.0;
    bool         m_haveLatestDisplaySample = false;
    bool         m_imperialUnits        = false;
};
