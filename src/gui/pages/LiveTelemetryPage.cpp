#include "gui/pages/LiveTelemetryPage.h"

#include "gui/FlightDataModel.h"
#include "gui/LayoutHelpers.h"
#include "gui/TelemetryMath.h"
#include "gui/Theme.h"
#include "gui/ThemeManager.h"
#include "gui/widgets/Map3DWidget.h"
#include "gui/widgets/StatTileWidget.h"

#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QList>
#include <QPushButton>
#include <QSignalBlocker>
#include <QShowEvent>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;

namespace {

[[nodiscard]] QString formatMetric(double value, const QString &unit, int precision = 1) {
    if (!std::isfinite(value)) {
        return u"-NA-"_s;
    }
    return QStringLiteral("%1 %2").arg(value, 0, 'f', precision).arg(unit);
}

[[nodiscard]] QString formatSignedMetric(double value, const QString &unit, int precision = 1) {
    if (!std::isfinite(value)) {
        return u"-NA-"_s;
    }
    return QStringLiteral("%1%2 %3")
        .arg(value >= 0.0 ? u"+"_s : QString())
        .arg(value, 0, 'f', precision)
        .arg(unit);
}

} // namespace

LiveTelemetryPage::LiveTelemetryPage(FlightDataModel *model, QWidget *parent)
    : QWidget(parent), m_model(model) {
    setObjectName(u"liveTelemetryPage"_s);
    buildUi();
    resetMetricTiles();
    setActiveConnection(QString(), false);
    refreshStyleSheet();

    if (m_model) {
        connect(m_model, &FlightDataModel::liveSamplesReceived,
                this, &LiveTelemetryPage::onLiveSamplesReceived);
        connect(m_model, &FlightDataModel::liveSamplesReceived,
                m_mapWidget, &Map3DWidget::onLiveSamplesReceived);
        connect(m_model, &FlightDataModel::sessionReset, this, &LiveTelemetryPage::resetLiveState);
        connect(m_model, &FlightDataModel::bytesReceivedChanged,
                this, &LiveTelemetryPage::onBytesReceivedChanged);
    }

    connect(&cosmo::ThemeManager::instance(), &cosmo::ThemeManager::themeChanged,
            this, &LiveTelemetryPage::refreshStyleSheet);
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
    }

    if (m_connectButton) {
        m_connectButton->setEnabled(!m_availablePorts.isEmpty() && !m_connected);
    }
    refreshDeviceRows();
}

void LiveTelemetryPage::setActiveConnection(const QString &label, bool connected) {
    m_activeConnectionLabel = label;
    m_connected = connected;

    if (m_connectionStatusLabel) {
        const QString text = connected
            ? QStringLiteral("Connected: %1").arg(label)
            : (label.isEmpty() ? u"Disconnected"_s : label);
        m_connectionStatusLabel->setText(text);
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
    if (m_demoButton) {
        m_demoButton->setEnabled(!connected);
    }
}

void LiveTelemetryPage::resetLiveState() {
    m_sampleCount = 0;
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
        QFrame#deviceRow {
            background-color: %11;
            border: 1px solid %12;
            border-radius: 4px;
        }
        QLabel[kind="deviceDot"] {
            color: %9;
            background: transparent;
            border: none;
            font-size: 16px;
        }
        QPushButton[kind="liveButton"],
        QToolButton[kind="mapButton"],
        QComboBox {
            background-color: %13;
            color: %5;
            border: 1px solid %12;
            border-radius: 4px;
            padding: 6px 10px;
            font-family: %6;
            font-size: %8px;
        }
        QPushButton[kind="liveButton"]:hover,
        QToolButton[kind="mapButton"]:hover,
        QComboBox:hover {
            background-color: %14;
            border-color: %15;
        }
        QPushButton[kind="liveButton"]:pressed,
        QToolButton[kind="mapButton"]:pressed {
            background-color: %16;
        }
        QPushButton[kind="liveButton"]:disabled,
        QToolButton[kind="mapButton"]:disabled,
        QComboBox:disabled {
            color: %17;
            border-color: %3;
            background-color: %11;
        }
        QPushButton#ignitorFireButton {
            min-width: 118px;
            min-height: 118px;
            border-radius: 59px;
            background-color: %18;
            color: %1;
            border: 2px solid %18;
            font-family: %6;
            font-size: 15px;
            font-weight: 800;
        }
        QPushButton#ignitorFireButton:disabled {
            background-color: %11;
            color: %17;
            border-color: %3;
        }
    )"_s)
        .arg(Theme::kBgBase())       // %1
        .arg(Theme::kBgPanel())      // %2
        .arg(Theme::kBorderPanel())  // %3
        .arg(Theme::kRadiusMd)       // %4
        .arg(Theme::kTextPrimary())  // %5
        .arg(Theme::kFontMono)       // %6
        .arg(Theme::kTextMuted())    // %7
        .arg(Theme::kFontSizeBase)   // %8
        .arg(Theme::kSuccess())      // %9
        .arg(Theme::kSuccessBg())    // %10
        .arg(Theme::kBgDark())       // %11
        .arg(Theme::kBorderDefault())// %12
        .arg(Theme::kBgButton())     // %13
        .arg(Theme::kBtnHover())     // %14
        .arg(Theme::kBorderLight())  // %15
        .arg(Theme::kBtnPressed())   // %16
        .arg(Theme::kTextDim())      // %17
        .arg(Theme::kDanger()));     // %18

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
    refreshTelemetryDisplay();
}

void LiveTelemetryPage::onConnectClicked() {
    const QString port = selectedPort();
    if (port.isEmpty()) {
        return;
    }
    emit connectDeviceRequested(port, selectedBaud());
}

void LiveTelemetryPage::buildUi() {
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(12);

    root->addWidget(buildMapPanel(), 5);
    root->addWidget(buildMetricsPanel(), 4);

    auto *sideColumn = new QWidget(this);
    auto *sideLayout = new QVBoxLayout(sideColumn);
    LayoutHelpers::setZeroMargins(sideLayout);
    sideLayout->setSpacing(12);
    sideLayout->addWidget(buildDevicePanel(), 3);
    sideLayout->addWidget(buildIgnitorPanel(), 2);
    root->addWidget(sideColumn, 3);
}

QFrame *LiveTelemetryPage::buildMapPanel() {
    auto *panel = new QFrame(this);
    panel->setObjectName(u"livePanel"_s);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto *header = new QHBoxLayout();
    auto *title = new QLabel(u"LIVE MAP"_s, panel);
    title->setProperty("kind", u"sectionTitle"_s);
    header->addWidget(title);
    header->addStretch(1);

    auto *fitButton = createMapToolButton(u"Fit"_s, panel);
    auto *centerButton = createMapToolButton(u"Center"_s, panel);
    auto *followButton = createMapToolButton(u"Follow"_s, panel);
    followButton->setCheckable(true);
    header->addWidget(fitButton);
    header->addWidget(centerButton);
    header->addWidget(followButton);
    layout->addLayout(header);

    m_mapWidget = new Map3DWidget(panel);
    m_mapWidget->setAccessibleName(u"Live flight path map"_s);
    m_mapWidget->setAccessibleDescription(u"Interactive map showing live rocket telemetry coordinates"_s);
    layout->addWidget(m_mapWidget, 1);

    connect(fitButton, &QToolButton::clicked, m_mapWidget, &Map3DWidget::fitPath);
    connect(centerButton, &QToolButton::clicked, m_mapWidget, &Map3DWidget::centerOnCurrent);
    connect(followButton, &QToolButton::toggled, m_mapWidget, &Map3DWidget::setCameraFollow);

    return panel;
}

QWidget *LiveTelemetryPage::buildMetricsPanel() {
    auto *panel = new QWidget(this);
    auto *layout = new QVBoxLayout(panel);
    LayoutHelpers::setZeroMargins(layout);
    layout->setSpacing(12);

    auto *title = new QLabel(u"LIVE TELEMETRY"_s, panel);
    title->setProperty("kind", u"sectionTitle"_s);
    layout->addWidget(title);

    auto *grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(12);

    const std::array<QString, 6> labels = {
        u"Altitude"_s,
        u"Velocity"_s,
        u"Temperature"_s,
        u"Pressure"_s,
        u"Battery"_s,
        u"RSSI"_s,
    };
    for (int i = 0; i < static_cast<int>(labels.size()); ++i) {
        auto *tile = new StatTileWidget(labels[static_cast<std::size_t>(i)], u"-NA-"_s, panel);
        tile->setMinimumHeight(140);
        tile->setAccentColor(QColor(Theme::kAccentLink()));
        m_metricTiles[static_cast<std::size_t>(i)] = tile;
        grid->addWidget(tile, i / 2, i % 2);
    }
    layout->addLayout(grid, 1);
    return panel;
}

QFrame *LiveTelemetryPage::buildDevicePanel() {
    auto *panel = new QFrame(this);
    panel->setObjectName(u"liveSidePanel"_s);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto *title = new QLabel(u"DEVICES"_s, panel);
    title->setProperty("kind", u"sectionTitle"_s);
    layout->addWidget(title);

    m_connectionStatusLabel = new QLabel(panel);
    m_connectionStatusLabel->setObjectName(u"connectionStatus"_s);
    m_connectionStatusLabel->setProperty("state", "idle");
    layout->addWidget(m_connectionStatusLabel);

    m_portCombo = new QComboBox(panel);
    m_portCombo->setAccessibleName(u"Serial port"_s);
    layout->addWidget(m_portCombo);

    m_baudCombo = new QComboBox(panel);
    m_baudCombo->setAccessibleName(u"Baud rate"_s);
    const QList<int> bauds{9600, 19200, 38400, 57600, 115200, 230400};
    for (const int baud : bauds) {
        m_baudCombo->addItem(QString::number(baud), baud);
    }
    m_baudCombo->setCurrentText(u"115200"_s);
    layout->addWidget(m_baudCombo);

    auto *buttonRow = new QHBoxLayout();
    buttonRow->setSpacing(6);
    m_scanButton = createPanelButton(u"Scan"_s, panel);
    m_connectButton = createPanelButton(u"Connect"_s, panel);
    m_disconnectButton = createPanelButton(u"Disconnect"_s, panel);
    buttonRow->addWidget(m_scanButton);
    buttonRow->addWidget(m_connectButton);
    buttonRow->addWidget(m_disconnectButton);
    layout->addLayout(buttonRow);

    m_demoButton = createPanelButton(u"Demo"_s, panel);
    layout->addWidget(m_demoButton);

    m_lastPacketLabel = new QLabel(panel);
    m_lastPacketLabel->setProperty("kind", u"caption"_s);
    layout->addWidget(m_lastPacketLabel);

    m_sampleCountLabel = new QLabel(panel);
    m_sampleCountLabel->setProperty("kind", u"caption"_s);
    layout->addWidget(m_sampleCountLabel);

    m_bytesLabel = new QLabel(panel);
    m_bytesLabel->setProperty("kind", u"caption"_s);
    layout->addWidget(m_bytesLabel);

    m_deviceRowsLayout = new QVBoxLayout();
    m_deviceRowsLayout->setContentsMargins(0, 0, 0, 0);
    m_deviceRowsLayout->setSpacing(6);
    layout->addLayout(m_deviceRowsLayout);
    layout->addStretch(1);

    connect(m_scanButton, &QPushButton::clicked, this, &LiveTelemetryPage::scanDevicesRequested);
    connect(m_connectButton, &QPushButton::clicked, this, &LiveTelemetryPage::onConnectClicked);
    connect(m_disconnectButton, &QPushButton::clicked, this, &LiveTelemetryPage::disconnectRequested);
    connect(m_demoButton, &QPushButton::clicked, this, &LiveTelemetryPage::startDemoRequested);

    refreshDeviceRows();
    return panel;
}

QFrame *LiveTelemetryPage::buildIgnitorPanel() {
    auto *panel = new QFrame(this);
    panel->setObjectName(u"liveIgnitorPanel"_s);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto *title = new QLabel(u"FIRE IGNITOR"_s, panel);
    title->setProperty("kind", u"sectionTitle"_s);
    layout->addWidget(title);

    auto *targetCombo = new QComboBox(panel);
    targetCombo->addItem(u"DISABLED"_s);
    targetCombo->setEnabled(false);
    layout->addWidget(targetCombo);

    auto *armedRow = new QHBoxLayout();
    auto *keyOne = createPanelButton(u"1"_s, panel);
    auto *keyZero = createPanelButton(u"0"_s, panel);
    keyOne->setEnabled(false);
    keyZero->setEnabled(false);
    armedRow->addWidget(keyOne);
    armedRow->addWidget(keyZero);
    armedRow->addStretch(1);
    layout->addLayout(armedRow);

    auto *fireRow = new QHBoxLayout();
    fireRow->addStretch(1);
    auto *timerButton = createPanelButton(u"Timer"_s, panel);
    timerButton->setEnabled(false);
    auto *fireButton = new QPushButton(u"FIRE\nIGNITE"_s, panel);
    fireButton->setObjectName(u"ignitorFireButton"_s);
    fireButton->setEnabled(false);
    fireButton->setAccessibleName(u"Fire ignitor disabled"_s);
    fireRow->addWidget(timerButton);
    fireRow->addWidget(fireButton);
    layout->addLayout(fireRow);
    layout->addStretch(1);

    return panel;
}

QPushButton *LiveTelemetryPage::createPanelButton(const QString &text, QWidget *parent) {
    auto *button = new QPushButton(text, parent);
    button->setProperty("kind", u"liveButton"_s);
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::TabFocus);
    button->setAccessibleName(text);
    return button;
}

QToolButton *LiveTelemetryPage::createMapToolButton(const QString &text, QWidget *parent) {
    auto *button = new QToolButton(parent);
    button->setText(text);
    button->setProperty("kind", u"mapButton"_s);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::TabFocus);
    button->setAccessibleName(text);
    return button;
}

void LiveTelemetryPage::refreshDeviceRows() {
    if (!m_deviceRowsLayout) {
        return;
    }

    while (QLayoutItem *item = m_deviceRowsLayout->takeAt(0)) {
        if (auto *widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }

    if (m_availablePorts.isEmpty()) {
        auto *label = new QLabel(u"No serial devices found"_s, this);
        label->setProperty("kind", u"caption"_s);
        m_deviceRowsLayout->addWidget(label);
        return;
    }

    for (const auto &port : m_availablePorts) {
        auto *row = new QFrame(this);
        row->setObjectName(u"deviceRow"_s);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(8, 6, 8, 6);
        layout->setSpacing(6);
        auto *dot = new QLabel(u"●"_s, row);
        dot->setProperty("kind", u"deviceDot"_s);
        auto *label = new QLabel(port, row);
        label->setProperty("kind", u"caption"_s);
        layout->addWidget(dot);
        layout->addWidget(label, 1);
        m_deviceRowsLayout->addWidget(row);
    }
}

void LiveTelemetryPage::refreshTelemetryDisplay() {
    if (m_haveLatestDisplaySample) {
        const auto &sample = m_latestDisplaySample;
        if (m_metricTiles[0]) {
            m_metricTiles[0]->setValue(formatMetric(sample.altitude, u"m"_s, 1));
        }
        if (m_metricTiles[1]) {
            m_metricTiles[1]->setValue(formatSignedMetric(m_latestVelocity, u"m/s"_s, 1));
        }
        if (m_metricTiles[2]) {
            m_metricTiles[2]->setValue(formatMetric(sample.temperature, u"C"_s, 1));
        }
        if (m_metricTiles[3]) {
            m_metricTiles[3]->setValue(formatMetric(sample.pressure, u"Pa"_s, 0));
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
            tile->setValue(u"-NA-"_s);
        }
    }
    updateLastPacketLabel();
}

void LiveTelemetryPage::updateLastPacketLabel() {
    if (m_lastPacketLabel) {
        m_lastPacketLabel->setText(
            m_sampleCount > 0 ? u"Last packet: now"_s : u"Last packet: -NA- ms"_s);
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
