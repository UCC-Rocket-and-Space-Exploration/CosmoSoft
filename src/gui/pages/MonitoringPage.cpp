#include "gui/pages/MonitoringPage.h"

#include "gui/FlightDataModel.h"
#include "gui/LayoutHelpers.h"
#include "gui/SettingsKeys.h"
#include "gui/TelemetryMath.h"
#include "gui/Theme.h"
#include "gui/ThemeManager.h"
#include "gui/widgets/Map3DWidget.h"
#include "gui/widgets/MetricDefs.h"
#include "gui/widgets/StatTileWidget.h"

#include <QChart>
#include <QChartView>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineSeries>
#include <QList>
#include <QPen>
#include <QPushButton>
#include <QSerialPortInfo>
#include <QSettings>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSplitter>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QValueAxis>
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

[[nodiscard]] QString formatDisplayMetric(int metricIndex, double siValue, bool imperial) {
    if (!std::isfinite(siValue)) {
        return u"-NA-"_s;
    }
    return QStringLiteral("%1 %2")
        .arg(MetricDefs::formatMetricDisplayValue(metricIndex, siValue, imperial),
             MetricDefs::metricDisplayUnitShort(metricIndex, imperial));
}

} // namespace

MonitoringPage::MonitoringPage(FlightDataModel *model, QWidget *parent)
    : QWidget(parent), m_model(model) {
    const QSettings unitSettings(kSettingsOrg, kSettingsApp);
    m_imperialUnits = unitSettings.value(kSettingsUnitSystem, kUnitSystemMetric)
                          .toString()
                          .compare(QString::fromLatin1(kUnitSystemImperial),
                                   Qt::CaseInsensitive) == 0;

    setObjectName(u"monitoringPage"_s);
    buildUi();
    resetMetricTiles();
    setActiveConnection(QString(), false);
    refreshStyleSheet();

    if (m_model) {
        connect(m_model, &FlightDataModel::liveSamplesReceived,
                this, &MonitoringPage::onLiveSamplesReceived);
        connect(m_model, &FlightDataModel::liveSamplesReceived,
                m_mapWidget, &Map3DWidget::onLiveSamplesReceived);
        connect(m_model, &FlightDataModel::sessionReset,
                this, &MonitoringPage::resetLiveState);
        connect(m_model, &FlightDataModel::bytesReceivedChanged,
                this, &MonitoringPage::onBytesReceivedChanged);
    }

    connect(&cosmo::ThemeManager::instance(), &cosmo::ThemeManager::themeChanged,
            this, &MonitoringPage::refreshStyleSheet);
}

void MonitoringPage::setImperialUnits(bool imperial) {
    if (m_imperialUnits == imperial) {
        return;
    }
    m_imperialUnits = imperial;
    if (m_mapWidget) {
        m_mapWidget->setImperialUnits(imperial);
    }
    refreshTelemetryDisplay();
}

void MonitoringPage::setAvailablePorts(const QStringList &ports) {
    setScanningState(false);

    const QString previous = selectedPort();
    m_availablePorts = ports;

    if (m_portCombo) {
        const QSignalBlocker blocker(m_portCombo);
        m_portCombo->clear();

        // Build a lookup of QSerialPortInfo by system location for rich descriptions
        const auto available = QSerialPortInfo::availablePorts();
        for (const QString &portPath : ports) {
            QString displayText = portPath;
            for (const auto &info : available) {
                if (info.systemLocation() == portPath || info.portName() == portPath) {
                    const QString desc = info.description().trimmed();
                    if (!desc.isEmpty()) {
                        displayText = QStringLiteral("%1 — %2").arg(desc, portPath);
                    }
                    break;
                }
            }
            m_portCombo->addItem(displayText, portPath);
        }

        if (!previous.isEmpty()) {
            const int idx = m_portCombo->findData(previous);
            if (idx >= 0) {
                m_portCombo->setCurrentIndex(idx);
            }
        } else {
            const QSettings settings(kSettingsOrg, kSettingsApp);
            const QString lastPort = settings.value(kSettingsSerialPort).toString();
            if (!lastPort.isEmpty()) {
                const int idx = m_portCombo->findData(lastPort);
                if (idx >= 0) {
                    m_portCombo->setCurrentIndex(idx);
                }
            }
        }
    }

    if (m_connectButton) {
        m_connectButton->setEnabled(!m_availablePorts.isEmpty() && !m_connected);
    }
}

void MonitoringPage::setActiveConnection(const QString &label, bool connected) {
    m_connected = connected;

    if (m_statusDot) {
        m_statusDot->setProperty("state", connected ? u"connected"_s : u"idle"_s);
        m_statusDot->style()->unpolish(m_statusDot);
        m_statusDot->style()->polish(m_statusDot);
    }

    if (m_statusLabel) {
        const QString text = connected
            ? QStringLiteral("CONNECTED  %1").arg(label)
            : (label.isEmpty() ? u"DISCONNECTED"_s : label.toUpper());
        m_statusLabel->setText(text);
        m_statusLabel->setProperty("state", connected ? u"connected"_s : u"idle"_s);
        m_statusLabel->style()->unpolish(m_statusLabel);
        m_statusLabel->style()->polish(m_statusLabel);
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

void MonitoringPage::resetLiveState() {
    m_sampleCount              = 0;
    m_totalBytes               = 0;
    m_havePreviousSample       = false;
    m_haveLatestDisplaySample  = false;
    m_latestVelocity           = 0.0;
    m_chartHasData             = false;
    m_chartStartMs             = 0;
    if (m_altSeries) { m_altSeries->clear(); }
    if (m_velSeries) { m_velSeries->clear(); }
    if (m_timeAxis)  { m_timeAxis->setRange(0.0, kChartWindowSec); }
    if (m_altAxis)   { m_altAxis->setRange(0.0, 100.0); }
    if (m_velAxis)   { m_velAxis->setRange(-50.0, 50.0); }
    resetMetricTiles();
    if (m_mapWidget) {
        m_mapWidget->onSessionReset();
    }
}

void MonitoringPage::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    refreshTelemetryDisplay();
}

void MonitoringPage::onLiveSamplesReceived(const QVector<FlightSample> &samples) {
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
    m_previousSample       = sample;
    m_havePreviousSample   = true;

    const qsizetype availableCount = static_cast<qsizetype>(
        std::numeric_limits<int>::max() - m_sampleCount);
    m_sampleCount += static_cast<int>(std::min(samples.size(), availableCount));

    m_latestDisplaySample      = sample;
    m_latestVelocity           = velocity;
    m_haveLatestDisplaySample  = true;

    if (std::isfinite(sample.altitude)) {
        appendToLiveChart(sample.altitude,
                          std::isfinite(velocity) ? velocity : 0.0);
    }

    if (isVisible()) {
        refreshTelemetryDisplay();
    }
}

void MonitoringPage::onBytesReceivedChanged(qint64 totalBytes) {
    m_totalBytes = totalBytes;
    if (isVisible()) {
        updateStatusCounters();
    }
}

void MonitoringPage::onConnectClicked() {
    const QString port = selectedPort();
    if (port.isEmpty()) {
        return;
    }
    QSettings settings(kSettingsOrg, kSettingsApp);
    settings.setValue(kSettingsSerialPort, port);
    settings.setValue(kSettingsSerialBaud, selectedBaud());
    emit connectDeviceRequested(port, selectedBaud());
}

// ── Layout ──────────────────────────────────────────────────────────────────

void MonitoringPage::buildUi() {
    auto *root = new QVBoxLayout(this);
    LayoutHelpers::setZeroMargins(root);
    root->setSpacing(0);

    // Top connection bar
    auto *bar = new QWidget(this);
    bar->setObjectName(u"monitoringBar"_s);
    bar->setFixedHeight(52);
    buildConnectionBar(bar);
    root->addWidget(bar);

    // Hairline separator
    auto *sep = new QFrame(this);
    sep->setObjectName(u"monitoringSep"_s);
    sep->setFrameShape(QFrame::HLine);
    sep->setFixedHeight(1);
    root->addWidget(sep);

    // Body: map (left) | metrics + log (right)
    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setObjectName(u"monitoringSplitter"_s);
    splitter->setHandleWidth(4);
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(buildLeftPanel());
    splitter->addWidget(buildRightPanel());
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);

    root->addWidget(splitter, 1);
}

void MonitoringPage::buildConnectionBar(QWidget *bar) {
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(12, 0, 12, 0);
    layout->setSpacing(8);

    m_statusDot = new QLabel(u"●"_s, bar); // ●
    m_statusDot->setObjectName(u"monStatusDot"_s);
    m_statusDot->setProperty("state", u"idle"_s);
    m_statusDot->setFixedWidth(18);
    layout->addWidget(m_statusDot);

    m_statusLabel = new QLabel(u"DISCONNECTED"_s, bar);
    m_statusLabel->setObjectName(u"monStatusLabel"_s);
    m_statusLabel->setProperty("state", u"idle"_s);
    m_statusLabel->setMinimumWidth(200);
    layout->addWidget(m_statusLabel);

    layout->addStretch(1);

    m_portCombo = new QComboBox(bar);
    m_portCombo->setObjectName(u"monPortCombo"_s);
    m_portCombo->setAccessibleName(u"Serial port"_s);
    m_portCombo->setMinimumContentsLength(12);
    m_portCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_portCombo->setFixedHeight(32);
    layout->addWidget(m_portCombo);

    m_baudCombo = new QComboBox(bar);
    m_baudCombo->setObjectName(u"monBaudCombo"_s);
    m_baudCombo->setAccessibleName(u"Baud rate"_s);
    m_baudCombo->setFixedHeight(32);
    const QList<int> bauds{9600, 19200, 38400, 57600, 115200, 230400};
    for (const int baud : bauds) {
        m_baudCombo->addItem(QString::number(baud), baud);
    }
    const QSettings settings(kSettingsOrg, kSettingsApp);
    const int lastBaud = settings.value(kSettingsSerialBaud, 115200).toInt();
    const int baudIdx  = m_baudCombo->findData(lastBaud);
    m_baudCombo->setCurrentIndex(baudIdx >= 0 ? baudIdx : m_baudCombo->findData(115200));
    layout->addWidget(m_baudCombo);

    const auto makeBarBtn = [&](const QString &text) -> QPushButton * {
        auto *btn = new QPushButton(text, bar);
        btn->setProperty("kind", u"barButton"_s);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFocusPolicy(Qt::TabFocus);
        btn->setFixedHeight(32);
        btn->setAccessibleName(text);
        layout->addWidget(btn);
        return btn;
    };

    m_scanButton       = makeBarBtn(u"Scan"_s);
    m_connectButton    = makeBarBtn(u"Connect"_s);
    m_disconnectButton = makeBarBtn(u"Disconnect"_s);
    m_demoButton       = makeBarBtn(u"Demo"_s);

    auto *vDiv = new QFrame(bar);
    vDiv->setObjectName(u"monBarDivider"_s);
    vDiv->setFrameShape(QFrame::VLine);
    vDiv->setFixedWidth(1);
    layout->addWidget(vDiv);

    m_samplesLabel = new QLabel(u"Samples: 0"_s, bar);
    m_samplesLabel->setObjectName(u"monCountLabel"_s);
    layout->addWidget(m_samplesLabel);

    m_bytesLabel = new QLabel(u"Bytes: 0"_s, bar);
    m_bytesLabel->setObjectName(u"monCountLabel"_s);
    layout->addWidget(m_bytesLabel);

    connect(m_scanButton,       &QPushButton::clicked, this, [this]() {
        setScanningState(true);
        emit scanDevicesRequested();
    });
    connect(m_connectButton,    &QPushButton::clicked,
            this, &MonitoringPage::onConnectClicked);
    connect(m_disconnectButton, &QPushButton::clicked,
            this, &MonitoringPage::disconnectRequested);
    connect(m_demoButton,       &QPushButton::clicked,
            this, &MonitoringPage::startDemoRequested);
}

QWidget *MonitoringPage::buildLeftPanel() {
    auto *panel = new QWidget(this);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(8, 8, 4, 8);
    layout->setSpacing(6);

    // Map header row
    auto *header = new QHBoxLayout();

    auto *title = new QLabel(u"FLIGHT MAP"_s, panel);
    title->setObjectName(u"monSectionTitle"_s);
    header->addWidget(title);
    header->addStretch(1);

    const auto makeMapBtn = [&](const QString &text) -> QToolButton * {
        auto *btn = new QToolButton(panel);
        btn->setText(text);
        btn->setProperty("kind", u"mapButton"_s);
        btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(28);
        header->addWidget(btn);
        return btn;
    };

    auto *fitBtn    = makeMapBtn(u"Fit"_s);
    auto *centerBtn = makeMapBtn(u"Center"_s);
    m_followButton  = makeMapBtn(u"Follow"_s);
    m_followButton->setCheckable(true);

    layout->addLayout(header);

    m_mapWidget = new Map3DWidget(panel);
    m_mapWidget->setImperialUnits(m_imperialUnits);
    m_mapWidget->setMinimumHeight(200);
    m_mapWidget->setAccessibleName(u"Live flight path map"_s);
    layout->addWidget(m_mapWidget, 1);

    connect(fitBtn,    &QToolButton::clicked,  m_mapWidget, &Map3DWidget::fitPath);
    connect(centerBtn, &QToolButton::clicked,  m_mapWidget, &Map3DWidget::centerOnCurrent);
    connect(m_followButton, &QToolButton::toggled, m_mapWidget, &Map3DWidget::setCameraFollow);
    connect(m_mapWidget,    &Map3DWidget::cameraFollowChanged,
            m_followButton, &QToolButton::setChecked);

    return panel;
}

QWidget *MonitoringPage::buildRightPanel() {
    auto *panel = new QWidget(this);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(4, 8, 8, 8);
    layout->setSpacing(8);

    // ── Metric tile grid 2×3 ────────────────────────────────────────────────
    auto *metricsTitle = new QLabel(u"TELEMETRY"_s, panel);
    metricsTitle->setObjectName(u"monSectionTitle"_s);
    layout->addWidget(metricsTitle);

    auto *grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(6);

    const std::array<QString, 6> tileLabels = {
        u"Altitude"_s, u"Velocity"_s,
        u"Temperature"_s, u"Pressure"_s,
        u"Battery"_s, u"RSSI"_s,
    };
    for (int i = 0; i < 6; ++i) {
        auto *tile = new StatTileWidget(
            tileLabels[static_cast<std::size_t>(i)], u"-NA-"_s, panel);
        tile->setMinimumHeight(72);
        tile->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        tile->setAccentColor(QColor(Theme::kAccentLink()));
        m_metricTiles[static_cast<std::size_t>(i)] = tile;
        grid->addWidget(tile, i / 2, i % 2);
    }
    layout->addLayout(grid);

    // ── Link quality bar ────────────────────────────────────────────────────
    auto *linkFrame = new QFrame(panel);
    linkFrame->setObjectName(u"monLinkFrame"_s);
    auto *linkLayout = new QVBoxLayout(linkFrame);
    linkLayout->setContentsMargins(10, 6, 10, 6);
    linkLayout->setSpacing(4);

    auto *linkHeader = new QHBoxLayout();
    auto *linkTitle  = new QLabel(u"LINK QUALITY"_s, linkFrame);
    linkTitle->setObjectName(u"monSectionTitle"_s);
    m_rssiValueLabel = new QLabel(u"-NA-"_s, linkFrame);
    m_rssiValueLabel->setObjectName(u"monRssiValue"_s);
    linkHeader->addWidget(linkTitle);
    linkHeader->addStretch(1);
    linkHeader->addWidget(m_rssiValueLabel);
    linkLayout->addLayout(linkHeader);

    auto *barBg = new QFrame(linkFrame);
    barBg->setObjectName(u"monRssiBarBg"_s);
    barBg->setFixedHeight(10);
    barBg->setFrameShape(QFrame::NoFrame);
    auto *barLayout = new QHBoxLayout(barBg);
    LayoutHelpers::setZeroMargins(barLayout);

    m_rssiBarFill = new QFrame(barBg);
    m_rssiBarFill->setObjectName(u"monRssiBarFill"_s);
    m_rssiBarFill->setFixedHeight(10);
    m_rssiBarFill->setMaximumWidth(0);
    barLayout->addWidget(m_rssiBarFill);
    barLayout->addStretch(1);

    linkLayout->addWidget(barBg);
    layout->addWidget(linkFrame);

    // ── Live rolling chart ──────────────────────────────────────────────────
    auto *chartTitle = new QLabel(u"LIVE TELEMETRY"_s, panel);
    chartTitle->setObjectName(u"monSectionTitle"_s);
    layout->addWidget(chartTitle);

    m_altSeries = new QLineSeries();
    m_altSeries->setName(u"Altitude"_s);
    m_altSeries->setPen(QPen(QColor(u"#5b9bd5"_s), 1.5));

    m_velSeries = new QLineSeries();
    m_velSeries->setName(u"Velocity"_s);
    m_velSeries->setPen(QPen(QColor(u"#70c1a5"_s), 1.5));

    m_chart = new QChart();
    m_chart->setBackgroundBrush(QBrush(QColor(Theme::kBgPanel())));
    m_chart->setPlotAreaBackgroundBrush(QBrush(QColor(Theme::kBgDark())));
    m_chart->setPlotAreaBackgroundVisible(true);
    m_chart->setMargins(QMargins(4, 4, 4, 4));
    m_chart->legend()->hide();

    m_timeAxis = new QValueAxis();
    m_timeAxis->setRange(0.0, kChartWindowSec);
    m_timeAxis->setLabelFormat(u"%.0f s"_s);
    m_timeAxis->setLabelsColor(QColor(Theme::kTextMuted()));
    m_timeAxis->setGridLineColor(QColor(Theme::kBorderPanel()));
    m_timeAxis->setLinePen(QPen(QColor(Theme::kBorderPanel())));
    m_timeAxis->setTickCount(7);

    m_altAxis = new QValueAxis();
    m_altAxis->setRange(0.0, 100.0);
    m_altAxis->setLabelFormat(u"%.0f m"_s);
    m_altAxis->setLabelsColor(QColor(u"#5b9bd5"_s));
    m_altAxis->setGridLineColor(QColor(Theme::kBorderPanel()));
    m_altAxis->setLinePen(QPen(Qt::transparent));
    m_altAxis->setTickCount(5);

    m_velAxis = new QValueAxis();
    m_velAxis->setRange(-50.0, 50.0);
    m_velAxis->setLabelFormat(u"%.0f m/s"_s);
    m_velAxis->setLabelsColor(QColor(u"#70c1a5"_s));
    m_velAxis->setGridLineVisible(false);
    m_velAxis->setLinePen(QPen(Qt::transparent));
    m_velAxis->setTickCount(5);

    m_chart->addSeries(m_altSeries);
    m_chart->addSeries(m_velSeries);
    m_chart->addAxis(m_timeAxis, Qt::AlignBottom);
    m_chart->addAxis(m_altAxis, Qt::AlignLeft);
    m_chart->addAxis(m_velAxis, Qt::AlignRight);
    m_altSeries->attachAxis(m_timeAxis);
    m_altSeries->attachAxis(m_altAxis);
    m_velSeries->attachAxis(m_timeAxis);
    m_velSeries->attachAxis(m_velAxis);

    m_chartView = new QChartView(m_chart, panel);
    m_chartView->setObjectName(u"monLiveChart"_s);
    m_chartView->setRenderHint(QPainter::Antialiasing);
    m_chartView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_chartView->setMinimumHeight(120);
    layout->addWidget(m_chartView, 1);

    return panel;
}

// ── Styling ─────────────────────────────────────────────────────────────────

void MonitoringPage::refreshStyleSheet() {
    setStyleSheet(QString(uR"(
        #monitoringPage {
            background-color: %1;
        }
        #monitoringBar {
            background-color: %2;
        }
        #monitoringSep, QFrame#monBarDivider {
            background-color: %3;
            border: none;
        }
        QSplitter#monitoringSplitter::handle {
            background-color: %3;
        }
        QLabel#monSectionTitle {
            color: %4;
            font-size: 11px;
            font-weight: 700;
            letter-spacing: 1.5px;
            background: transparent;
            border: none;
        }
        QLabel#monStatusDot[state="connected"] { color: %5;  font-size: 18px; border: none; background: transparent; }
        QLabel#monStatusDot[state="idle"]      { color: %6;  font-size: 18px; border: none; background: transparent; }
        QLabel#monStatusDot[state="error"]     { color: %19; font-size: 18px; border: none; background: transparent; }
        QLabel#monStatusLabel[state="connected"] {
            color: %5;
            font-weight: 700;
            font-family: %7;
            font-size: %8px;
            background: transparent;
            border: none;
        }
        QLabel#monStatusLabel[state="idle"] {
            color: %6;
            font-family: %7;
            font-size: %8px;
            background: transparent;
            border: none;
        }
        QLabel#monStatusLabel[state="error"] {
            color: %19;
            font-weight: 700;
            font-family: %7;
            font-size: %8px;
            background: transparent;
            border: none;
        }
        QLabel#monCountLabel {
            color: %6;
            font-family: %7;
            font-size: %8px;
            background: transparent;
            border: none;
        }
        QLabel#monRssiValue {
            color: %4;
            font-family: %7;
            font-size: %8px;
            background: transparent;
            border: none;
        }
        QPushButton[kind="barButton"] {
            background-color: %9;
            color: %4;
            border: 1px solid %10;
            border-radius: 4px;
            padding: 4px 10px;
            font-family: %7;
            font-size: %8px;
        }
        QPushButton[kind="barButton"]:hover    { background-color: %11; border-color: %12; }
        QPushButton[kind="barButton"]:pressed  { background-color: %13; }
        QPushButton[kind="barButton"]:disabled { color: %14; border-color: %3; background-color: %15; }
        QComboBox#monPortCombo, QComboBox#monBaudCombo {
            background-color: %9;
            color: %4;
            border: 1px solid %10;
            border-radius: 4px;
            padding: 4px 8px;
            font-family: %7;
            font-size: %8px;
        }
        QComboBox#monPortCombo:hover, QComboBox#monBaudCombo:hover { border-color: %12; }
        QToolButton[kind="mapButton"] {
            background-color: %9;
            color: %4;
            border: 1px solid %10;
            border-radius: 4px;
            padding: 3px 8px;
            font-family: %7;
            font-size: %8px;
        }
        QToolButton[kind="mapButton"]:hover   { background-color: %11; border-color: %12; }
        QToolButton[kind="mapButton"]:checked {
            background-color: %16;
            color: %1;
            border-color: %16;
            font-weight: 700;
        }
        QFrame#monLinkFrame {
            background-color: %17;
            border: 1px solid %3;
            border-radius: %18px;
        }
        QFrame#monRssiBarBg   { background-color: %15; border-radius: 5px; border: none; }
        QFrame#monRssiBarFill { background-color: %5;  border-radius: 5px; border: none; }
        QChartView#monLiveChart {
            background-color: %17;
            border: 1px solid %3;
            border-radius: %18px;
        }
    )"_s)
        .arg(Theme::kBgBase())          // %1
        .arg(Theme::kBgDark())          // %2
        .arg(Theme::kBorderPanel())     // %3
        .arg(Theme::kTextPrimary())     // %4
        .arg(Theme::kSuccess())         // %5
        .arg(Theme::kTextMuted())       // %6
        .arg(Theme::kFontMono)          // %7
        .arg(Theme::kFontSizeBase)      // %8
        .arg(Theme::kBgButton())        // %9
        .arg(Theme::kBorderDefault())   // %10
        .arg(Theme::kBtnHover())        // %11
        .arg(Theme::kBorderLight())     // %12
        .arg(Theme::kBtnPressed())      // %13
        .arg(Theme::kTextDim())         // %14
        .arg(Theme::kBgDark())          // %15
        .arg(Theme::kAccentLink())      // %16
        .arg(Theme::kBgPanel())         // %17
        .arg(Theme::kRadiusMd)          // %18
        .arg(Theme::kDanger()));        // %19

    for (auto *tile : m_metricTiles) {
        if (tile) {
            tile->setAccentColor(QColor(Theme::kAccentLink()));
        }
    }

    if (m_chart) {
        m_chart->setBackgroundBrush(QBrush(QColor(Theme::kBgPanel())));
        m_chart->setPlotAreaBackgroundBrush(QBrush(QColor(Theme::kBgDark())));
    }
    if (m_timeAxis) {
        m_timeAxis->setLabelsColor(QColor(Theme::kTextMuted()));
        m_timeAxis->setGridLineColor(QColor(Theme::kBorderPanel()));
        m_timeAxis->setLinePen(QPen(QColor(Theme::kBorderPanel())));
    }
    if (m_altAxis) {
        m_altAxis->setGridLineColor(QColor(Theme::kBorderPanel()));
    }
}

// ── Data display ─────────────────────────────────────────────────────────────

void MonitoringPage::refreshTelemetryDisplay() {
    if (m_haveLatestDisplaySample) {
        const auto &s = m_latestDisplaySample;

        if (m_metricTiles[0]) {
            m_metricTiles[0]->setValue(
                formatDisplayMetric(0, s.altitude, m_imperialUnits));
        }
        if (m_metricTiles[1]) {
            m_metricTiles[1]->setValue(formatSignedMetric(
                MetricDefs::verticalSpeedDisplayValue(m_latestVelocity, m_imperialUnits),
                MetricDefs::verticalSpeedDisplayUnit(m_imperialUnits)));
        }
        if (m_metricTiles[2]) {
            m_metricTiles[2]->setValue(
                formatDisplayMetric(1, s.temperature, m_imperialUnits));
        }
        if (m_metricTiles[3]) {
            m_metricTiles[3]->setValue(
                formatDisplayMetric(2, s.pressure, m_imperialUnits));
        }
        if (m_metricTiles[4]) {
            m_metricTiles[4]->setValue(formatMetric(s.batteryVoltage, u"V"_s, 2));
        }
        if (m_metricTiles[5]) {
            m_metricTiles[5]->setValue(formatMetric(s.rssi, u"dBm"_s, 1));
        }

        updateRssiBar(s.rssi);
    }

    updateStatusCounters();
}

void MonitoringPage::resetMetricTiles() {
    for (auto *tile : m_metricTiles) {
        if (tile) {
            tile->setValue(u"-NA-"_s);
        }
    }
    updateStatusCounters();
    updateRssiBar(std::numeric_limits<double>::quiet_NaN());
}

void MonitoringPage::updateStatusCounters() {
    if (m_samplesLabel) {
        m_samplesLabel->setText(QStringLiteral("Samples: %1").arg(m_sampleCount));
    }
    if (m_bytesLabel) {
        m_bytesLabel->setText(QStringLiteral("Bytes: %1").arg(m_totalBytes));
    }
}

void MonitoringPage::updateRssiBar(double rssi) {
    if (!m_rssiBarFill || !m_rssiValueLabel) {
        return;
    }

    if (!std::isfinite(rssi)) {
        m_rssiValueLabel->setText(u"-NA-"_s);
        m_rssiBarFill->setMaximumWidth(0);
        return;
    }

    // RSSI range: -120 dBm (no signal) to -30 dBm (excellent)
    constexpr double kMinRssi = -120.0;
    constexpr double kMaxRssi = -30.0;
    const double ratio = (std::clamp(rssi, kMinRssi, kMaxRssi) - kMinRssi)
                         / (kMaxRssi - kMinRssi);

    m_rssiValueLabel->setText(QStringLiteral("%1 dBm").arg(rssi, 0, 'f', 1));

    if (auto *barBg = m_rssiBarFill->parentWidget()) {
        m_rssiBarFill->setMaximumWidth(
            std::max(0, static_cast<int>(barBg->width() * ratio)));
    }
}

void MonitoringPage::setScanningState(bool scanning) {
    m_scanning = scanning;
    if (m_scanButton) {
        m_scanButton->setText(scanning ? u"Scanning…"_s : u"Scan"_s);
        m_scanButton->setEnabled(!scanning);
    }
}

void MonitoringPage::setConnectionError(const QString &message) {
    if (m_statusDot) {
        m_statusDot->setProperty("state", u"error"_s);
        m_statusDot->style()->unpolish(m_statusDot);
        m_statusDot->style()->polish(m_statusDot);
    }
    if (m_statusLabel) {
        m_statusLabel->setText(message.toUpper());
        m_statusLabel->setProperty("state", u"error"_s);
        m_statusLabel->style()->unpolish(m_statusLabel);
        m_statusLabel->style()->polish(m_statusLabel);
    }
    if (m_connectButton) {
        m_connectButton->setEnabled(!m_availablePorts.isEmpty());
    }
    QTimer::singleShot(5000, this, [this]() {
        if (!m_connected) {
            setActiveConnection(QString(), false);
        }
    });
}

void MonitoringPage::appendToLiveChart(double altM, double velMps) {
    if (!m_altSeries || !m_velSeries || !m_timeAxis || !m_altAxis || !m_velAxis) {
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (!m_chartHasData) {
        m_chartStartMs = nowMs;
        m_chartHasData = true;
    }

    const double x    = static_cast<double>(nowMs - m_chartStartMs) / 1000.0;
    const double xMin = x - kChartWindowSec;

    m_altSeries->append(x, altM);
    m_velSeries->append(x, velMps);

    m_timeAxis->setRange(std::max(0.0, xMin), std::max(kChartWindowSec, x));

    const auto pruneOld = [xMin](QLineSeries *series) {
        const auto pts = series->points();
        int n = 0;
        for (const auto &pt : pts) {
            if (pt.x() < xMin) { ++n; } else { break; }
        }
        if (n > 0) {
            series->removePoints(0, n);
        }
    };
    pruneOld(m_altSeries);
    pruneOld(m_velSeries);

    const auto autoRange = [](QValueAxis *axis, QLineSeries *series, double defaultLo, double defaultHi) {
        const auto pts = series->points();
        if (pts.isEmpty()) {
            axis->setRange(defaultLo, defaultHi);
            return;
        }
        double lo = pts.first().y(), hi = lo;
        for (const auto &pt : pts) {
            lo = std::min(lo, pt.y());
            hi = std::max(hi, pt.y());
        }
        const double pad = std::max(1.0, (hi - lo) * 0.1);
        axis->setRange(lo - pad, hi + pad);
    };
    autoRange(m_altAxis, m_altSeries, 0.0, 100.0);
    autoRange(m_velAxis, m_velSeries, -50.0, 50.0);
}

// ── Helpers ──────────────────────────────────────────────────────────────────

QString MonitoringPage::selectedPort() const {
    if (!m_portCombo) {
        return QString();
    }
    return m_portCombo->currentData().toString();
}

int MonitoringPage::selectedBaud() const {
    if (!m_baudCombo) {
        return 115200;
    }
    bool ok = false;
    const int baud = m_baudCombo->currentData().toInt(&ok);
    return ok ? baud : 115200;
}
