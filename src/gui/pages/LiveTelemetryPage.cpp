#include "gui/pages/LiveTelemetryPage.h"
#include "gui/widgets/PreflightChecklistWidget.h"
#include "gui/widgets/VisualHorizonWidget.h"
#include "services/persistence/RecoveryState.h"
#include <QMenu>

#include "gui/FlightDataModel.h"
#include "gui/LayoutHelpers.h"
#include "gui/SettingsKeys.h"
#include "gui/TelemetryMath.h"
#include "gui/Theme.h"
#include "gui/ThemeManager.h"
#include "gui/widgets/Map3DWidget.h"
#include "gui/widgets/MetricDefs.h"
#include "gui/widgets/RocketAttitudeWidget.h"
#include "gui/widgets/StatTileWidget.h"

#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QList>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSettings>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

[[nodiscard]] QString formatMetric(double value, const QString &unit, int precision = 1) {
    if (!std::isfinite(value)) {
      return u"—"_s;
    }
    return QStringLiteral("%1 %2").arg(value, 0, 'f', precision).arg(unit);
}

[[nodiscard]] QString formatSignedMetric(double value, const QString &unit, int precision = 1) {
    if (!std::isfinite(value)) {
      return u"—"_s;
    }
    return QStringLiteral("%1%2 %3")
        .arg(value >= 0.0 ? u"+"_s : QString())
        .arg(value, 0, 'f', precision)
        .arg(unit);
}

[[nodiscard]] QString formatDisplayMetric(
    int metricIndex, double siValue, bool imperial) {
    if (!std::isfinite(siValue)) {
      return u"—"_s;
    }
    return QStringLiteral("%1 %2")
        .arg(MetricDefs::formatMetricDisplayValue(metricIndex, siValue, imperial),
             MetricDefs::metricDisplayUnitShort(metricIndex, imperial));
}

} // namespace

LiveTelemetryPage::LiveTelemetryPage(FlightDataModel *model, QWidget *parent,
                                     ClockSource clockSource)
    : QWidget(parent), m_model(model),
      m_clockSource(clockSource ? std::move(clockSource) : [] {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
      }) {
  const QSettings unitSettings(kSettingsOrg, kSettingsApp);
  m_imperialUnits = unitSettings.value(kSettingsUnitSystem, kUnitSystemMetric)
                        .toString()
                        .compare(QString::fromLatin1(kUnitSystemImperial),
                                 Qt::CaseInsensitive) == 0;
  setObjectName(u"liveTelemetryPage"_s);
  buildUi();
  m_rateAt = m_clockSource();
  auto ageTimer = std::make_unique<QTimer>(this);
  m_packetAgeTimer = ageTimer.get();
  ageTimer->setObjectName("packetAgeTimer");
  ageTimer->setInterval(1000);
  connect(ageTimer.release(), &QTimer::timeout, this,
          &LiveTelemetryPage::updateLastPacketLabel);
  resetMetricTiles();
  setActiveConnection(QString(), false);
  refreshStyleSheet();

  if (m_model) {
    connect(m_model, &FlightDataModel::liveSamplesReceived, this,
            &LiveTelemetryPage::onLiveSamplesReceived);
    connect(m_model, &FlightDataModel::liveSamplesReceived, m_mapWidget,
            &Map3DWidget::onLiveSamplesReceived);
    connect(m_model, &FlightDataModel::sessionReset, this,
            &LiveTelemetryPage::resetLiveState);
    connect(m_model, &FlightDataModel::bytesReceivedChanged, this,
            &LiveTelemetryPage::onBytesReceivedChanged);
  }

  connect(&cosmo::ThemeManager::instance(), &cosmo::ThemeManager::themeChanged,
          this, &LiveTelemetryPage::refreshStyleSheet);
}

void LiveTelemetryPage::setImperialUnits(bool imperial) {
    if (m_imperialUnits == imperial) {
        return;
    }
    m_imperialUnits = imperial;
    if (m_mapWidget) {
        m_mapWidget->setImperialUnits(imperial);
    }
    refreshTelemetryDisplay();
}

void LiveTelemetryPage::setAvailablePorts(const QStringList &ports) {
    const QString previous = selectedPort();
    m_availablePorts = ports;

    if (m_portCombo) {
        const QSignalBlocker blocker(m_portCombo);
        m_portCombo->clear();
        m_portCombo->addItems(m_availablePorts);
        if (!previous.isEmpty()) {
            const int index = m_portCombo->findText(previous);
            if (index >= 0) {
                m_portCombo->setCurrentIndex(index);
            }
        }
        if (m_portCombo->currentIndex() < 0 && m_portCombo->count() > 0) {
            m_portCombo->setCurrentIndex(0);
        }
    }

    if (m_connectButton) {
        m_connectButton->setEnabled(!m_availablePorts.isEmpty() && !m_connected);
    }
}

void LiveTelemetryPage::setActiveConnection(const QString &label, bool connected) {
    m_activeConnectionLabel = label;
    m_connected = connected;
    if (m_packetAgeTimer && isVisible()) {
      if (connected || m_lastPacketAt)
        m_packetAgeTimer->start();
      else
        m_packetAgeTimer->stop();
    }

    if (m_connectionStatusLabel) {
        const QString text = connected
            ? QStringLiteral("Connected: %1").arg(label)
            : (label.isEmpty() ? u"Disconnected"_s : label);
        m_connectionStatusLabel->setText(connected ? tr("Connected") : text);
        m_connectionStatusLabel->setToolTip(text);
        m_connectionStatusLabel->setAccessibleName(text);
        m_connectionStatusLabel->setProperty("state", connected ? "connected" : "idle");
        m_connectionStatusLabel->style()->unpolish(m_connectionStatusLabel);
        m_connectionStatusLabel->style()->polish(m_connectionStatusLabel);
    }

    if (m_connectButton) {
        m_connectButton->setEnabled(!connected && !m_availablePorts.isEmpty());
    }
    if (m_disconnectButton) {
        m_disconnectButton->setEnabled(connected);
    }
}

void LiveTelemetryPage::resetLiveState() {
  m_lastPacketAt.reset();
  m_lastBytesAt.reset();
  m_rateAt = m_clockSource();
  m_rateBytes = 0;
  m_rateSamples = 0;
  m_malformedRows = m_droppedChunks = m_discardedSamples = 0;
  m_droppedBytes = m_lastDiagnosticRow = 0;
  m_lastDiagnosticMessage.clear();
  emit packetDiagnosticChanged({});
  m_packetAgeTimer->stop();
  if (m_horizonWidget)
    m_horizonWidget->resetDisplay();
  if (m_attitudeWidget)
    m_attitudeWidget->resetDisplay();
  m_sampleCount = 0;
  if (m_checklist) {
    m_checklist->setMeasurements({}, {}, false);
    m_checklist->resetChecks();
  }
  m_totalBytes = 0;
  m_havePreviousSample = false;
  m_haveLatestDisplaySample = false;
  m_latestVelocity = 0.0;
  resetMetricTiles();
  updateLastPacketLabel();
  if (m_mapWidget) {
    m_mapWidget->onSessionReset();
  }
}

void LiveTelemetryPage::refreshStyleSheet() {
  setStyleSheet(QString(uR"(
        #liveTelemetryPage {
            background-color: %1;
        }
        QScrollArea#liveTelemetryScroll,
        QWidget#liveTelemetryContent {
            background-color: %1;
            border: none;
        }
        QFrame#livePanel,
        QFrame#liveSidePanel,
        QFrame#liveIgnitorPanel {
            background-color: %2;
            border: 1px solid %3;
            border-radius: %4px;
        }
        QLabel[kind="sectionTitle"] {
            color: %5;
            font-family: %6;
            font-size: 15px;
            font-weight: 700;
            letter-spacing: 1px;
            background: transparent;
            border: none;
        }
        QLabel[kind="caption"] {
            color: %7;
            font-family: %6;
            font-size: %8px;
            background: transparent;
            border: none;
        }
        QLabel#connectionStatus[state="connected"] {
            color: %9;
            background-color: %10;
            border: 1px solid %9;
            border-radius: 4px;
            padding: 5px 8px;
        }
        QLabel#connectionStatus[state="idle"] {
            color: %7;
            background-color: %11;
            border: 1px solid %12;
            border-radius: 4px;
            padding: 5px 8px;
        }
        QPushButton[kind="liveButton"],
        QComboBox {
            background-color: %13;
            color: %5;
            border: 1px solid %12;
            border-radius: 4px;
            padding: 6px 10px;
            font-family: %6;
            font-size: %8px;
        }
        QToolButton {
            background-color: %13;
            color: %5;
            border: 1px solid %12;
            border-radius: 4px;
            padding: 4px 6px;
            font-size: 11px;
        }
        QToolButton:hover { background-color: %14; }
        QToolButton:checked { color: %15; border-color: %15; }
        QPushButton[kind="liveButton"]:hover,
        QComboBox:hover {
            background-color: %14;
            border-color: %15;
        }
        QPushButton[kind="liveButton"]:pressed {
            background-color: %16;
        }
        QPushButton[kind="liveButton"]:focus,
        QComboBox:focus {
            border: 2px solid %19;
        }
        QPushButton[kind="liveButton"]:disabled,
        QComboBox:disabled {
            color: %17;
            border-color: %3;
            background-color: %11;
        }
        QFrame#liveSidePanel QLabel[kind="sectionTitle"],
        QFrame#liveIgnitorPanel QLabel[kind="sectionTitle"] {
            font-size: 13px;
        }
        QFrame#liveSidePanel QLabel[kind="caption"],
        QFrame#liveIgnitorPanel QLabel[kind="caption"],
        QLabel#connectionStatus {
            font-family: %6;
            font-size: 11px;
        }
        QFrame#liveSidePanel QPushButton[kind="liveButton"],
        QFrame#liveSidePanel QComboBox {
            font-size: 11px;
            padding: 4px 6px;
        }
        QFrame#liveSidePanel QPushButton[kind="liveButton"] {
            min-height: 18px;
        }
        QFrame#liveSidePanel QLabel#connectionStatus {
            padding: 2px 6px;
        }
    )"_s)
                    .arg(Theme::kBgBase())        // %1
                    .arg(Theme::kBgPanel())       // %2
                    .arg(Theme::kBorderSubtle())  // %3
                    .arg(Theme::kRadiusMd)        // %4
                    .arg(Theme::kTextPrimary())   // %5
                    .arg(Theme::kFontMono)        // %6
                    .arg(Theme::kTextMuted())     // %7
                    .arg(Theme::kFontSizeBase)    // %8
                    .arg(Theme::kSuccess())       // %9
                    .arg(Theme::kSuccessBg())     // %10
                    .arg(Theme::kBgDark())        // %11
                    .arg(Theme::kBorderDefault()) // %12
                    .arg(Theme::kBgButton())      // %13
                    .arg(Theme::kBtnHover())      // %14
                    .arg(Theme::kBorderLight())   // %15
                    .arg(Theme::kBtnPressed())    // %16
                    .arg(Theme::kTextDim())       // %17
                    .arg(Theme::kFocusRing()));   // %19

  for (auto *tile : m_metricTiles) {
    if (tile) {
      tile->setAccentColor(QColor(Theme::kAccentLink()));
    }
  }
}

void LiveTelemetryPage::onLiveSamplesReceived(const QVector<FlightSample> &samples) {
    if (samples.isEmpty()) {
        return;
    }

    const FlightSample &sample = samples.constLast();
    m_lastPacketAt = m_clockSource();
    if (isVisible() && !m_packetAgeTimer->isActive())
      m_packetAgeTimer->start();
    if (m_horizonWidget)
      m_horizonWidget->setSample(sample);
    if (m_attitudeWidget)
      m_attitudeWidget->setSample(sample);
    const FlightSample *velocityPrevious = nullptr;
    if (samples.size() >= 2) {
        velocityPrevious = &samples[samples.size() - 2];
    } else if (m_havePreviousSample) {
        velocityPrevious = &m_previousSample;
    }

    double velocity = std::numeric_limits<double>::quiet_NaN();
    if (velocityPrevious) {
        if (const auto calculated = cosmo::gui::verticalVelocityMetersPerSecond(
                *velocityPrevious, sample)) {
            velocity = *calculated;
        }
    }
    m_previousSample = sample;
    m_havePreviousSample = true;

    const qsizetype availableCount = static_cast<qsizetype>(
        std::numeric_limits<int>::max() - m_sampleCount);
    m_sampleCount += static_cast<int>(std::min(samples.size(), availableCount));

    m_latestDisplaySample = sample;
    m_latestVelocity = velocity;
    m_haveLatestDisplaySample = true;

    if (isVisible()) {
        refreshTelemetryDisplay();
    }
}

void LiveTelemetryPage::onBytesReceivedChanged(qint64 totalBytes) {
    m_totalBytes = totalBytes;
    if (isVisible()) {
        updateLastPacketLabel();
    }
}

void LiveTelemetryPage::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    updateResponsiveLayout();
    reloadChecklists();
    refreshTelemetryDisplay();
    if (m_lastPacketAt || m_connected)
      m_packetAgeTimer->start();
}

void LiveTelemetryPage::hideEvent(QHideEvent *event) {
  m_packetAgeTimer->stop();
  QWidget::hideEvent(event);
}

void LiveTelemetryPage::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    updateResponsiveLayout();
}

void LiveTelemetryPage::onConnectClicked() {
    const QString port = selectedPort();
    if (port.isEmpty()) {
        return;
    }
    emit connectDeviceRequested(port, selectedBaud());
}

void LiveTelemetryPage::buildUi() {
    auto *root = new QVBoxLayout(this);
    LayoutHelpers::setZeroMargins(root);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setObjectName(u"liveTelemetryScroll"_s);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    m_scrollContent = new QWidget(m_scrollArea);
    m_scrollContent->setObjectName(u"liveTelemetryContent"_s);
    m_responsiveLayout = new QGridLayout(m_scrollContent);
    m_responsiveLayout->setContentsMargins(24, 8, 24, 20);
    m_responsiveLayout->setHorizontalSpacing(16);
    m_responsiveLayout->setVerticalSpacing(16);
    m_responsiveLayout->setSizeConstraint(QLayout::SetMinimumSize);

    m_mapPanel = buildMapPanel();
    m_metricsPanel = buildMetricsPanel();

    m_sideColumn = new QWidget(m_scrollContent);
    auto *sideLayout = new QVBoxLayout(m_sideColumn);
    LayoutHelpers::setZeroMargins(sideLayout);
    sideLayout->setSpacing(12);
    auto attitude = std::make_unique<RocketAttitudeWidget>(m_sideColumn);
    m_attitudeWidget = attitude.release();
    m_attitudeWidget->setCompact(true);
    auto instruments = std::make_unique<QHBoxLayout>();
    instruments->setSpacing(8);
    m_horizonWidget =
        std::make_unique<VisualHorizonWidget>(m_sideColumn).release();
    instruments->addWidget(m_horizonWidget, 1);
    instruments->addWidget(m_attitudeWidget, 1);
    sideLayout->addLayout(instruments.release());
    sideLayout->addWidget(buildDevicePanel());
    sideLayout->addWidget(buildIgnitorPanel());
    sideLayout->addStretch(1);

    m_scrollArea->setWidget(m_scrollContent);
    root->addWidget(m_scrollArea);
    updateResponsiveLayout();
}

QFrame *LiveTelemetryPage::buildMapPanel() {
    auto *panel = new QFrame(this);
    panel->setObjectName(u"livePanel"_s);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(8);

    auto *header = new QHBoxLayout();
    auto *title = new QLabel(u"FLIGHT PATH"_s, panel);
    title->setProperty("kind", u"sectionTitle"_s);
    header->addWidget(title);
    header->addStretch(1);
    m_instrumentsMenu =
        std::make_unique<QMenu>(tr("Instruments"), this).release();
    for (const auto &name : {tr("Visual horizon"), tr("Rocket attitude")}) {
      auto *action = m_instrumentsMenu->addAction(name);
      action->setCheckable(true);
      action->setChecked(true);
      connect(action, &QAction::toggled, this, [this, name](bool show) {
        if (name == tr("Visual horizon"))
          m_horizonWidget->setVisible(show);
        else
          m_attitudeWidget->setVisible(show);
      });
    }
    layout->addLayout(header);

    m_mapWidget = new Map3DWidget(panel);
    m_mapWidget->setImperialUnits(m_imperialUnits);
    m_mapWidget->setMinimumHeight(180);
    m_mapWidget->setAccessibleName(u"Live flight path map"_s);
    m_mapWidget->setAccessibleDescription(u"Interactive map showing live rocket telemetry coordinates"_s);
    layout->addWidget(m_mapWidget, 1);


    return panel;
}

QWidget *LiveTelemetryPage::buildMetricsPanel() {
    auto *panel = new QWidget(this);
    auto *layout = new QVBoxLayout(panel);
    LayoutHelpers::setZeroMargins(layout);
    layout->setSpacing(12);

    auto *title = new QLabel(u"FLIGHT TELEMETRY"_s, panel);
    title->setProperty("kind", u"sectionTitle"_s);
    layout->addWidget(title);

    m_metricsGrid = new QGridLayout();
    m_metricsGrid->setContentsMargins(0, 0, 0, 0);
    m_metricsGrid->setSpacing(12);

    const std::array<QString, 6> labels = {
        u"Altitude"_s,
        u"Velocity"_s,
        u"Temperature"_s,
        u"Pressure"_s,
        u"Battery"_s,
        u"RSSI"_s,
    };
    for (int i = 0; i < static_cast<int>(labels.size()); ++i) {
      auto *tile = new StatTileWidget(labels[static_cast<std::size_t>(i)],
                                      u"—"_s, panel);
      tile->setMinimumHeight(76);
      tile->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
      tile->setAccentColor(QColor(Theme::kAccentLink()));
      m_metricTiles[static_cast<std::size_t>(i)] = tile;
      m_metricsGrid->addWidget(tile, i / 2, i % 2);
    }
    m_metricColumnCount = 2;
    layout->addLayout(m_metricsGrid, 1);
    return panel;
}

QFrame *LiveTelemetryPage::buildDevicePanel() {
    auto *panel = new QFrame(this);
    panel->setObjectName(u"liveSidePanel"_s);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(6);

    auto *title = new QLabel(u"CONNECTION"_s, panel);
    title->setProperty("kind", u"sectionTitle"_s);
    auto header = std::make_unique<QHBoxLayout>();
    header->addWidget(title);
    header->addStretch();

    m_connectionStatusLabel = new QLabel(panel);
    m_connectionStatusLabel->setObjectName(u"connectionStatus"_s);
    m_connectionStatusLabel->setProperty("state", "idle");
    m_connectionStatusLabel->setSizePolicy(QSizePolicy::Ignored,
                                           QSizePolicy::Preferred);
    header->addWidget(m_connectionStatusLabel, 1);
    layout->addLayout(header.release());

    auto fields = std::make_unique<QGridLayout>();
    fields->setHorizontalSpacing(8);
    fields->setVerticalSpacing(4);
    fields->setColumnStretch(0, 2);
    fields->setColumnStretch(1, 1);

    m_portCombo = new QComboBox(panel);
    m_portCombo->setAccessibleName(u"Serial port"_s);
    m_portCombo->setPlaceholderText(tr("No devices found"));
    m_portCombo->setToolTip(
        tr("Select a serial port, or use Scan to find devices"));
    m_portCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_portCombo->setMinimumContentsLength(10);
    m_portCombo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    auto *portLabel = std::make_unique<QLabel>(tr("Serial port"), panel).release();
    portLabel->setProperty("kind", u"caption"_s);
    portLabel->setBuddy(m_portCombo);
    fields->addWidget(portLabel, 0, 0);
    fields->addWidget(m_portCombo, 1, 0);

    m_baudCombo = new QComboBox(panel);
    m_baudCombo->setAccessibleName(u"Baud rate"_s);
    m_baudCombo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    const QList<int> bauds{9600, 19200, 38400, 57600, 115200, 230400};
    for (const int baud : bauds) {
        m_baudCombo->addItem(QString::number(baud), baud);
    }
    m_baudCombo->setCurrentText(u"115200"_s);
    auto *baudLabel = std::make_unique<QLabel>(tr("Baud rate"), panel).release();
    baudLabel->setProperty("kind", u"caption"_s);
    baudLabel->setBuddy(m_baudCombo);
    fields->addWidget(baudLabel, 0, 1);
    fields->addWidget(m_baudCombo, 1, 1);
    layout->addLayout(fields.release());

    auto *buttonRow = new QHBoxLayout();
    buttonRow->setSpacing(6);
    m_scanButton = createPanelButton(u"Scan"_s, panel);
    m_connectButton = createPanelButton(u"Connect"_s, panel);
    m_disconnectButton = createPanelButton(u"Disconnect"_s, panel);
    buttonRow->addWidget(m_scanButton);
    buttonRow->addWidget(m_connectButton);
    buttonRow->addWidget(m_disconnectButton);
    layout->addLayout(buttonRow);

    auto details = std::make_unique<QToolButton>(panel);
    details->setText(tr("Link details"));
    details->setCheckable(true);
    details->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    details->setArrowType(Qt::RightArrow);
    details->setObjectName("linkDetailsToggle");
    m_linkRateLabel = std::make_unique<QLabel>(panel).release();
    m_linkRateLabel->setObjectName("linkRate");
    m_linkRateLabel->setText(tr("— Hz · — B/s"));
    m_linkRateLabel->setProperty("kind", "caption");
    auto rateRow = std::make_unique<QHBoxLayout>();
    rateRow->addWidget(m_linkRateLabel, 1);
    rateRow->addWidget(details.get());
    layout->addLayout(rateRow.release());
    m_linkDetails = std::make_unique<QLabel>(panel).release();
    m_linkDetails->setObjectName("linkDetails");
    m_linkDetails->setProperty("kind", "caption");
    m_linkDetails->setWordWrap(true);
    m_linkDetails->hide();
    connect(details.get(), &QToolButton::toggled, this,
            [this, button = details.get()](bool visible) {
              button->setArrowType(visible ? Qt::DownArrow : Qt::RightArrow);
              m_linkDetails->setVisible(visible);
            });
    details.release();
    layout->addWidget(m_linkDetails);

    m_lastPacketLabel = new QLabel(panel);
    m_lastPacketLabel->setProperty("kind", u"caption"_s);
    layout->addWidget(m_lastPacketLabel);

    m_sampleCountLabel = new QLabel(panel);
    m_sampleCountLabel->setProperty("kind", u"caption"_s);
    auto counters = std::make_unique<QHBoxLayout>();
    counters->addWidget(m_sampleCountLabel);
    counters->addStretch();

    m_bytesLabel = new QLabel(panel);
    m_bytesLabel->setProperty("kind", u"caption"_s);
    counters->addWidget(m_bytesLabel);
    layout->addLayout(counters.release());

    connect(m_scanButton, &QPushButton::clicked, this, &LiveTelemetryPage::scanDevicesRequested);
    connect(m_connectButton, &QPushButton::clicked, this, &LiveTelemetryPage::onConnectClicked);
    connect(m_disconnectButton, &QPushButton::clicked, this, &LiveTelemetryPage::disconnectRequested);

    return panel;
}

QFrame *LiveTelemetryPage::buildIgnitorPanel() {
  m_checklist = std::make_unique<PreflightChecklistWidget>(this).release();
  m_checklist->setObjectName("liveIgnitorPanel");
  return m_checklist;
}
void LiveTelemetryPage::reloadChecklists() { m_checklist->reloadProfiles(); }
void LiveTelemetryPage::setBatteryAvailable(bool available) {
  m_batteryAvailable = available;
}

QPushButton *LiveTelemetryPage::createPanelButton(const QString &text, QWidget *parent) {
    auto *button = new QPushButton(text, parent);
    button->setProperty("kind", u"liveButton"_s);
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::TabFocus);
    button->setAccessibleName(text);
    return button;
}

void LiveTelemetryPage::updateResponsiveLayout() {
    if (!m_scrollArea || !m_responsiveLayout || !m_mapPanel
        || !m_metricsPanel || !m_sideColumn) {
        return;
    }

    const int availableWidth = std::max(0, m_scrollArea->viewport()->width());
    constexpr int kWideLayoutWidth = 1180;
    constexpr int kTwoColumnLayoutWidth = 880;
    constexpr int kSingleMetricColumnWidth = 500;
    const int nextMode = availableWidth >= kWideLayoutWidth
        ? 0
        : (availableWidth >= kTwoColumnLayoutWidth ? 1 : 2);
    const int nextMetricColumns =
        availableWidth < kSingleMetricColumnWidth ? 1 : (nextMode == 0 ? 6 : 3);
    if (nextMode == m_responsiveMode
        && nextMetricColumns == m_metricColumnCount) {
        return;
    }

    if (nextMode != m_responsiveMode) {
        m_responsiveLayout->removeWidget(m_mapPanel);
        m_responsiveLayout->removeWidget(m_metricsPanel);
        m_responsiveLayout->removeWidget(m_sideColumn);
        for (int column = 0; column < 3; ++column) {
            m_responsiveLayout->setColumnStretch(column, 0);
        }
        for (int row = 0; row < 4; ++row) {
          m_responsiveLayout->setRowStretch(row, 0);
        }

        if (nextMode <= 1) {
          // Keep full-size readings at smaller desktop widths without adding
          // their second row to the height of the instrument column.
          m_responsiveLayout->addWidget(m_metricsPanel, 0, 0, 1,
                                        nextMode == 0 ? 2 : 1);
          m_responsiveLayout->addWidget(m_mapPanel, 1, 0);
          m_responsiveLayout->addWidget(m_sideColumn, nextMode == 0 ? 1 : 0, 1,
                                        nextMode == 0 ? 1 : 2, 1);
          m_responsiveLayout->setColumnStretch(0, nextMode == 0 ? 3 : 2);
          m_responsiveLayout->setColumnStretch(1, 1);
          m_responsiveLayout->setRowStretch(1, 1);
        } else {
            m_responsiveLayout->addWidget(m_metricsPanel, 0, 0);
            m_responsiveLayout->addWidget(m_mapPanel, 1, 0);
            m_responsiveLayout->addWidget(m_sideColumn, 2, 0);
            m_responsiveLayout->setColumnStretch(0, 1);
        }
        m_responsiveMode = nextMode;
    }

    if (m_metricsGrid && nextMetricColumns != m_metricColumnCount) {
        for (auto *tile : m_metricTiles) {
            if (tile) {
                m_metricsGrid->removeWidget(tile);
            }
        }
        for (int index = 0; index < static_cast<int>(m_metricTiles.size()); ++index) {
            if (auto *tile = m_metricTiles[static_cast<std::size_t>(index)]) {
                m_metricsGrid->addWidget(
                    tile,
                    index / nextMetricColumns,
                    index % nextMetricColumns);
            }
        }
        m_metricColumnCount = nextMetricColumns;
    }

    m_responsiveLayout->invalidate();
}

void LiveTelemetryPage::refreshTelemetryDisplay() {
    if (m_haveLatestDisplaySample) {
        const auto &sample = m_latestDisplaySample;
        if (m_metricTiles[0]) {
            m_metricTiles[0]->setValue(
                formatDisplayMetric(0, sample.altitude, m_imperialUnits));
        }
        if (m_metricTiles[1]) {
            m_metricTiles[1]->setValue(formatSignedMetric(
                MetricDefs::verticalSpeedDisplayValue(m_latestVelocity, m_imperialUnits),
                MetricDefs::verticalSpeedDisplayUnit(m_imperialUnits), 1));
        }
        if (m_metricTiles[2]) {
            m_metricTiles[2]->setValue(
                formatDisplayMetric(1, sample.temperature, m_imperialUnits));
        }
        if (m_metricTiles[3]) {
            m_metricTiles[3]->setValue(
                formatDisplayMetric(2, sample.pressure, m_imperialUnits));
        }
        if (m_metricTiles[4]) {
            m_metricTiles[4]->setValue(formatMetric(sample.batteryVoltage, u"V"_s, 2));
        }
        if (m_metricTiles[5]) {
            m_metricTiles[5]->setValue(formatMetric(sample.rssi, u"dBm"_s, 1));
        }
    }

    updateLastPacketLabel();
}

void LiveTelemetryPage::resetMetricTiles() {
    for (auto *tile : m_metricTiles) {
        if (tile) {
          tile->setValue(u"—"_s);
        }
    }
    updateLastPacketLabel();
}

void LiveTelemetryPage::updateLinkDiagnostics(
    const cosmo::telemetry::LineTelemetryBatch &batch,
    std::size_t discardedSamples) {
  m_malformedRows += batch.malformedLines;
  m_droppedChunks += batch.droppedChunks;
  m_droppedBytes += batch.droppedBytes;
  m_discardedSamples += discardedSamples;
  if (batch.receivedBytes)
    m_lastBytesAt = m_clockSource();
  if (batch.rowDiagnostic &&
      batch.rowDiagnostic->sequence > m_lastDiagnosticRow) {
    m_lastDiagnosticRow = batch.rowDiagnostic->sequence;
    const auto &row = *batch.rowDiagnostic;
    const std::array<QString, 16> names{
        tr("time"),           tr("temperature"),    tr("pressure"),
        tr("altitude"),       tr("acceleration X"), tr("acceleration Y"),
        tr("acceleration Z"), tr("gyro X"),         tr("gyro Y"),
        tr("gyro Z"),         tr("angle X"),        tr("angle Y"),
        tr("angle Z"),        tr("latitude"),       tr("longitude"),
        tr("distance")};
    QStringList missing, invalid;
    for (std::size_t i = 0; i < names.size(); ++i) {
      if (row.missing & (1U << i))
        missing << names[i];
      if (row.invalid & (1U << i))
        invalid << names[i];
    }
    QStringList issues;
    if (!missing.isEmpty())
      issues << tr("Last packet missing: %1").arg(missing.join(", "));
    if (!invalid.isEmpty())
      issues << tr("Invalid: %1").arg(invalid.join(", "));
    if (row.malformed && issues.isEmpty())
      issues << tr("Last packet invalid: layout or size");
    const auto message = issues.join(" · ");
    if (message != m_lastDiagnosticMessage) {
      m_lastDiagnosticMessage = message;
      emit packetDiagnosticChanged(message);
    }
  }
}

void LiveTelemetryPage::updateLastPacketLabel() {
  const qint64 now = m_clockSource();
  if (m_checklist)
    m_checklist->setMeasurements(
        m_lastPacketAt && m_connected
            ? std::optional(std::max<qint64>(0, now - *m_lastPacketAt) / 1000.0)
            : std::nullopt,
        m_batteryAvailable && m_haveLatestDisplaySample &&
                std::isfinite(m_latestDisplaySample.batteryVoltage)
            ? std::optional(m_latestDisplaySample.batteryVoltage)
            : std::nullopt,
        m_haveLatestDisplaySample &&
            cosmo::mission::usablePosition(
                m_latestDisplaySample.coordinates.latitude,
                m_latestDisplaySample.coordinates.longitude));
  const qint64 period = now - m_rateAt;
  if (m_linkRateLabel && period >= 1000) {
    m_linkRateLabel->setText(
        tr("%1 Hz · %2 B/s")
            .arg((m_sampleCount - m_rateSamples) * 1000.0 / period, 0, 'f', 1)
            .arg((m_totalBytes - m_rateBytes) * 1000.0 / period, 0, 'f', 0));
    m_rateAt = now;
    m_rateSamples = m_sampleCount;
    m_rateBytes = m_totalBytes;
  }
  if (m_linkDetails)
    m_linkDetails->setText(
        tr("Malformed rows: %1 · Queue drops: %2 chunks / %3 bytes · GUI "
           "drops: %4 samples")
            .arg(static_cast<qulonglong>(m_malformedRows))
            .arg(static_cast<qulonglong>(m_droppedChunks))
            .arg(static_cast<qulonglong>(m_droppedBytes))
            .arg(static_cast<qulonglong>(m_discardedSamples)));
  if (m_connectionStatusLabel && m_connected) {
    m_connectionStatusLabel->setText(!m_lastPacketAt ? tr("No data")
                                     : now - *m_lastPacketAt >= 2000
                                         ? tr("Delayed")
                                         : tr("Receiving"));
    m_connectionStatusLabel->setToolTip(
        m_lastBytesAt && now - *m_lastBytesAt < 2000 &&
                (!m_lastPacketAt || now - *m_lastPacketAt >= 2000)
            ? tr("Bytes arriving without valid samples")
            : m_activeConnectionLabel);
  }

    if (m_lastPacketLabel) {
      const qint64 elapsed =
          m_lastPacketAt
              ? std::max<qint64>(0, m_clockSource() - *m_lastPacketAt)
              : 0;
      m_lastPacketLabel->setText(
          !m_lastPacketAt  ? tr("Last packet: —")
          : elapsed < 1000 ? tr("Last packet: now")
                           : tr("Last packet: %1 s ago").arg(elapsed / 1000));
    }
    if (m_sampleCountLabel) {
        m_sampleCountLabel->setText(QStringLiteral("Samples: %1").arg(m_sampleCount));
    }
    if (m_bytesLabel) {
        m_bytesLabel->setText(QStringLiteral("Bytes received: %1").arg(m_totalBytes));
    }
}

QString LiveTelemetryPage::selectedPort() const {
    if (!m_portCombo || m_portCombo->currentText().trimmed().isEmpty()) {
        return QString();
    }
    return m_portCombo->currentText().trimmed();
}

int LiveTelemetryPage::selectedBaud() const {
    if (!m_baudCombo) {
        return 115200;
    }
    bool ok = false;
    const int baud = m_baudCombo->currentData().toInt(&ok);
    return ok ? baud : 115200;
}
