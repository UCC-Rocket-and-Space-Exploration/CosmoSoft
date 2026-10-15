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
#include "services/telemetry/LineTelemetryDecodeWorker.h"

#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <array>
#include <functional>
#include <optional>

class QMenu;
class QComboBox;
class QFrame;
class QGridLayout;
class QLabel;
class QPushButton;
class QResizeEvent;
class QScrollArea;
class QShowEvent;
class QHideEvent;
class QTimer;
class QVBoxLayout;
class FlightDataModel;
class Map3DWidget;
class StatTileWidget;
class RocketAttitudeWidget;
class VisualHorizonWidget;
class PreflightChecklistWidget;

/**
 * @class LiveTelemetryPage
 * @brief Live operations screen for serial telemetry and no-hardware demo data.
 */
class LiveTelemetryPage : public QWidget {
    Q_OBJECT

public:
  /** @brief Page-specific instrument actions for the main Instruments menu. */
  [[nodiscard]] QMenu *instrumentsMenu() const { return m_instrumentsMenu; }

  /** @brief Monotonic milliseconds used to display time since packet receipt.
   */
  using ClockSource = std::function<qint64()>;
  /**
   * @brief Construct the live telemetry page bound to @p model.
   * @param model Shared flight data model that publishes live samples.
   * @param parent Optional Qt parent.
   * @param clockSource Monotonic receipt clock; defaults to steady_clock.
   */
  explicit LiveTelemetryPage(FlightDataModel *model, QWidget *parent = nullptr,
                             ClockSource clockSource = {});
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
  /** @brief Reload saved preflight definitions without reopening the page. */
  void reloadChecklists();
  /** @brief Mark whether this source actually supplies battery voltage. */
  void setBatteryAvailable(bool available);

  /**
   * @brief Selects metric or imperial presentation units for live telemetry.
   *
   * Incoming samples and vertical-speed calculations remain in SI units.
   */
  void setImperialUnits(bool imperial);
  /** @brief Update transport counters and the latest completed-row diagnostic.
   */
  void updateLinkDiagnostics(const cosmo::telemetry::LineTelemetryBatch &batch,
                             std::size_t discardedSamples = 0);

signals:
    /** @brief User requested serial device scanning. */
    void scanDevicesRequested();
    /** @brief Publish source-specific packet issues to the application status
     * line. */
    void packetDiagnosticChanged(QString text);

    /** @brief User requested connection to @p port at @p baud. */
    void connectDeviceRequested(QString port, int baud);

    /** @brief User requested live connection shutdown. */
    void disconnectRequested();

    /** @brief User requested deterministic fake live telemetry. */
    void startDemoRequested();

    /** @brief User requested a recorded AltOS IMU byte stream. */
    void recordedImuRequested();

  protected:
    /** @brief Refresh deferred telemetry presentation when the page becomes visible. */
    void showEvent(QShowEvent *event) override;
    /** @brief Suspend packet-age refresh while this page is hidden. */
    void hideEvent(QHideEvent *event) override;

    /** @brief Reflow telemetry panels to match the available viewport width. */
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void refreshStyleSheet();
    void onLiveSamplesReceived(const QVector<FlightSample> &samples);
    void onBytesReceivedChanged(qint64 totalBytes);
    void onConnectClicked();

private:
  QMenu *m_instrumentsMenu = nullptr;
  void buildUi();
  QFrame *buildMapPanel();
  QWidget *buildMetricsPanel();
  QFrame *buildDevicePanel();
  QFrame *buildIgnitorPanel();
  QPushButton *createPanelButton(const QString &text, QWidget *parent);
  void refreshTelemetryDisplay();
  void resetMetricTiles();
  void updateLastPacketLabel();
  void updateResponsiveLayout();
  [[nodiscard]] QString selectedPort() const;
  [[nodiscard]] int selectedBaud() const;

  FlightDataModel *m_model = nullptr;
  ClockSource m_clockSource;
  std::optional<qint64> m_lastPacketAt;
  QTimer *m_packetAgeTimer = nullptr;
  Map3DWidget *m_mapWidget = nullptr;
  RocketAttitudeWidget *m_attitudeWidget = nullptr;
  VisualHorizonWidget *m_horizonWidget = nullptr;
  PreflightChecklistWidget *m_checklist = nullptr;
  bool m_batteryAvailable = false;
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
  QLabel *m_linkRateLabel = nullptr;
  QLabel *m_linkDetails = nullptr;
  std::optional<qint64> m_lastBytesAt;
  qint64 m_rateAt = 0, m_rateBytes = 0;
  int m_rateSamples = 0;
  std::size_t m_malformedRows = 0, m_droppedChunks = 0, m_discardedSamples = 0;
  std::uint64_t m_droppedBytes = 0, m_lastDiagnosticRow = 0;
  QString m_lastDiagnosticMessage;

  QComboBox *m_portCombo = nullptr;
  QComboBox *m_baudCombo = nullptr;
  QPushButton *m_scanButton = nullptr;
  QPushButton *m_connectButton = nullptr;
  QPushButton *m_disconnectButton = nullptr;

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
