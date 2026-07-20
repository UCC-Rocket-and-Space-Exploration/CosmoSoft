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
#include <QResizeEvent>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QShowEvent>
#include <QSizePolicy>
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

[[nodiscard]] double relativeLuminance(const QColor &color) {
    const auto linearChannel = [](const double channel) {
        return channel <= 0.04045
            ? channel / 12.92
            : std::pow((channel + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linearChannel(color.redF())
        + 0.7152 * linearChannel(color.greenF())
        + 0.0722 * linearChannel(color.blueF());
}

[[nodiscard]] double contrastRatio(const QColor &first, const QColor &second) {
    const double firstLuminance = relativeLuminance(first);
    const double secondLuminance = relativeLuminance(second);
    const double lighter = std::max(firstLuminance, secondLuminance);
    const double darker = std::min(firstLuminance, secondLuminance);
    return (lighter + 0.05) / (darker + 0.05);
}

[[nodiscard]] QString checkedButtonForeground() {
    const QColor accent(Theme::kAccentLink());
    const QColor base(Theme::kBgBase());
    const QColor primaryText(Theme::kTextPrimary());
    if (!accent.isValid() || !base.isValid() || !primaryText.isValid()) {
        return Theme::kBgBase();
    }
    return contrastRatio(accent, base) >= contrastRatio(accent, primaryText)
        ? Theme::kBgBase()
        : Theme::kTextPrimary();
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
        QToolButton[kind="mapButton"]:checked {
            background-color: %20;
            color: %21;
            border-color: %20;
            font-weight: 700;
        }
        QPushButton[kind="liveButton"]:focus,
        QToolButton[kind="mapButton"]:focus,
        QComboBox:focus {
            border: 2px solid %19;
        }
        QPushButton[kind="liveButton"]:disabled,
        QToolButton[kind="mapButton"]:disabled,
        QComboBox:disabled {
            color: %17;
            border-color: %3;
            background-color: %11;
        }
        QPushButton#ignitorFireButton {
            min-width: 88px;
            min-height: 88px;
            border-radius: 44px;
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
        .arg(Theme::kDanger())       // %18
        .arg(Theme::kFocusRing())    // %19
        .arg(Theme::kAccentLink())   // %20
        .arg(checkedButtonForeground())); // %21

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
    updateResponsiveLayout();
    refreshTelemetryDisplay();
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
    m_responsiveLayout->setContentsMargins(12, 12, 12, 12);
    m_responsiveLayout->setHorizontalSpacing(12);
    m_responsiveLayout->setVerticalSpacing(12);
    m_responsiveLayout->setSizeConstraint(QLayout::SetMinimumSize);

    m_mapPanel = buildMapPanel();
    m_metricsPanel = buildMetricsPanel();

    m_sideColumn = new QWidget(m_scrollContent);
    auto *sideLayout = new QVBoxLayout(m_sideColumn);
    LayoutHelpers::setZeroMargins(sideLayout);
    sideLayout->setSpacing(12);
    sideLayout->addWidget(buildDevicePanel(), 3);
    sideLayout->addWidget(buildIgnitorPanel(), 2);

    m_scrollArea->setWidget(m_scrollContent);
    root->addWidget(m_scrollArea);
    updateResponsiveLayout();
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
    m_followButton = createMapToolButton(u"Follow"_s, panel);
    m_followButton->setCheckable(true);
    header->addWidget(fitButton);
    header->addWidget(centerButton);
    header->addWidget(m_followButton);
    layout->addLayout(header);

    m_mapWidget = new Map3DWidget(panel);
    m_mapWidget->setMinimumHeight(180);
    m_mapWidget->setAccessibleName(u"Live flight path map"_s);
    m_mapWidget->setAccessibleDescription(u"Interactive map showing live rocket telemetry coordinates"_s);
    layout->addWidget(m_mapWidget, 1);

    connect(fitButton, &QToolButton::clicked, m_mapWidget, &Map3DWidget::fitPath);
    connect(centerButton, &QToolButton::clicked, m_mapWidget, &Map3DWidget::centerOnCurrent);
    connect(m_followButton, &QToolButton::toggled, this, [this](bool enabled) {
        updateFollowButtonState(enabled);
        m_mapWidget->setCameraFollow(enabled);
    });
    connect(m_mapWidget, &Map3DWidget::cameraFollowChanged,
            this, &LiveTelemetryPage::updateFollowButtonState);
    updateFollowButtonState(false);

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
        auto *tile = new StatTileWidget(labels[static_cast<std::size_t>(i)], u"-NA-"_s, panel);
        tile->setMinimumHeight(88);
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
    layout->setSpacing(8);

    auto *title = new QLabel(u"DEVICES"_s, panel);
    title->setProperty("kind", u"sectionTitle"_s);
    layout->addWidget(title);

    m_connectionStatusLabel = new QLabel(panel);
    m_connectionStatusLabel->setObjectName(u"connectionStatus"_s);
    m_connectionStatusLabel->setProperty("state", "idle");
    m_connectionStatusLabel->setWordWrap(true);
    layout->addWidget(m_connectionStatusLabel);

    m_portCombo = new QComboBox(panel);
    m_portCombo->setAccessibleName(u"Serial port"_s);
    m_portCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_portCombo->setMinimumContentsLength(10);
    m_portCombo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    layout->addWidget(m_portCombo);

    m_baudCombo = new QComboBox(panel);
    m_baudCombo->setAccessibleName(u"Baud rate"_s);
    m_baudCombo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
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

void LiveTelemetryPage::updateFollowButtonState(bool enabled) {
    if (!m_followButton) {
        return;
    }

    if (m_followButton->isChecked() != enabled) {
        const QSignalBlocker blocker(m_followButton);
        m_followButton->setChecked(enabled);
    }

    m_followButton->setText(enabled ? tr("Following") : tr("Follow"));
    m_followButton->setAccessibleName(
        enabled ? tr("Camera follow on") : tr("Camera follow off"));
    m_followButton->setAccessibleDescription(
        enabled
            ? tr("The map camera follows the latest position. Activate to stop following.")
            : tr("The map camera is free to move. Activate to follow the latest position."));
    m_followButton->setToolTip(
        enabled
            ? tr("Camera follow is on. Activate to stop following.")
            : tr("Camera follow is off. Activate to follow the latest position."));
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
        label->setWordWrap(true);
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        layout->addWidget(dot);
        layout->addWidget(label, 1);
        m_deviceRowsLayout->addWidget(row);
    }
}

void LiveTelemetryPage::updateResponsiveLayout() {
    if (!m_scrollArea || !m_responsiveLayout || !m_mapPanel
        || !m_metricsPanel || !m_sideColumn) {
        return;
    }

    const int availableWidth = std::max(0, width());
    constexpr int kWideLayoutWidth = 1180;
    constexpr int kTwoColumnLayoutWidth = 760;
    constexpr int kSingleMetricColumnWidth = 500;
    const int nextMode = availableWidth >= kWideLayoutWidth
        ? 0
        : (availableWidth >= kTwoColumnLayoutWidth ? 1 : 2);
    const int nextMetricColumns = availableWidth < kSingleMetricColumnWidth ? 1 : 2;
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
        for (int row = 0; row < 3; ++row) {
            m_responsiveLayout->setRowStretch(row, 0);
        }

        if (nextMode == 0) {
            m_responsiveLayout->addWidget(m_mapPanel, 0, 0);
            m_responsiveLayout->addWidget(m_metricsPanel, 0, 1);
            m_responsiveLayout->addWidget(m_sideColumn, 0, 2);
            m_responsiveLayout->setColumnStretch(0, 5);
            m_responsiveLayout->setColumnStretch(1, 4);
            m_responsiveLayout->setColumnStretch(2, 3);
            m_responsiveLayout->setRowStretch(0, 1);
        } else if (nextMode == 1) {
            m_responsiveLayout->addWidget(m_mapPanel, 0, 0, 1, 2);
            m_responsiveLayout->addWidget(m_metricsPanel, 1, 0);
            m_responsiveLayout->addWidget(m_sideColumn, 1, 1);
            m_responsiveLayout->setColumnStretch(0, 5);
            m_responsiveLayout->setColumnStretch(1, 3);
            m_responsiveLayout->setRowStretch(0, 5);
            m_responsiveLayout->setRowStretch(1, 4);
        } else {
            m_responsiveLayout->addWidget(m_mapPanel, 0, 0);
            m_responsiveLayout->addWidget(m_metricsPanel, 1, 0);
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
