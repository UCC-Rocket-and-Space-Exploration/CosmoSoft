#include "gui/pages/DashboardPage.h"

#include "domain/FlightSession.h"
#include "gui/FlightDataModel.h"
#include "gui/FlightReplayController.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

constexpr int kMaxChartPoints = 500;

QChartView *buildTelemetryChart(
    QWidget *parent,
    const QString &yTitle,
    const QColor &color,
    QLineSeries **seriesOut,
    QValueAxis **axisXOut,
    QValueAxis **axisYOut) {
    auto *series = new QLineSeries(parent);
    *seriesOut = series;
    series->setName(yTitle);
    series->setUseOpenGL(false);
    series->setColor(color);
    series->setPen(QPen(color, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

    auto *chart = new QChart();
    chart->legend()->setVisible(false);
    chart->addSeries(series);
    chart->setMargins(QMargins(8, 8, 8, 8));
    chart->setBackgroundBrush(QColor("#0a0a0a"));
    chart->setBackgroundPen(Qt::NoPen);

    auto *axisX = new QValueAxis();
    *axisXOut = axisX;
    axisX->setTitleText(u"t (s)"_s);
    axisX->setRange(0, 10);
    axisX->setTickCount(6);
    axisX->setLabelsColor(QColor("#f5f5f5"));
    axisX->setTitleBrush(QColor("#c0c0c0"));
    axisX->setLinePenColor(QColor("#f5f5f5"));
    axisX->setGridLineColor(QColor(255, 255, 255, 40));
    chart->addAxis(axisX, Qt::AlignBottom);
    series->attachAxis(axisX);

    auto *axisY = new QValueAxis();
    *axisYOut = axisY;
    axisY->setTitleText(yTitle);
    axisY->setRange(-1, 1);
    axisY->setTitleBrush(QColor("#c0c0c0"));
    axisY->setLabelsColor(QColor("#f5f5f5"));
    axisY->setLinePenColor(QColor("#f5f5f5"));
    axisY->setGridLineColor(QColor(255, 255, 255, 40));
    chart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisY);

    auto *chartView = new QChartView(chart, parent);
    chartView->setRenderHint(QPainter::Antialiasing);
    chartView->setMinimumHeight(140);
    chartView->setObjectName(u"chartView"_s);
    return chartView;
}

QFrame *createStatTile(const QString &label, const QString &value, QWidget *parent, QLabel **valueLabelOut) {
    auto *tile = new QFrame(parent);
    tile->setProperty("kind", u"statTile"_s);
    auto *layout = new QVBoxLayout(tile);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->setSpacing(2);

    auto *title = new QLabel(label, tile);
    title->setProperty("kind", u"statLabel"_s);
    layout->addWidget(title);

    auto *valueLabel = new QLabel(value, tile);
    valueLabel->setProperty("kind", u"statValue"_s);
    layout->addWidget(valueLabel);
    if (valueLabelOut) {
        *valueLabelOut = valueLabel;
    }

    return tile;
}

} // namespace

double DashboardPage::elapsedSeconds(long t0Ms, long tMs) {
    return static_cast<double>(tMs - t0Ms) / 1000.0;
}

DashboardPage::DashboardPage(FlightDataModel *model, FlightReplayController *replay, QWidget *parent)
    : QWidget(parent),
      m_model(model),
      m_replay(replay) {
    setObjectName(u"dashboardPage"_s);
    setStyleSheet(uR"(
        #dashboardPage {
            background-color: #1f1f1f;
            color: #f8f8f8;
            font-family: "Red Hat Mono", "Courier New", "Roboto Mono", monospace;
        }
        #dashboardPage QWidget {
            font-family: "Red Hat Mono", "Courier New", "Roboto Mono", monospace;
        }
        QFrame[kind="statTile"] {
            background-color: #2b2d33;
            border: 2px dotted #8ab6ff;
            border-radius: 6px;
        }
        QLabel[kind="statLabel"] {
            font-size: 12px;
            text-transform: uppercase;
            color: #b8c1cc;
            letter-spacing: 1px;
        }
        QLabel[kind="statValue"] {
            font-size: 20px;
            font-weight: 700;
            color: #fefefe;
        }
        QFrame#chartFrame {
            background-color: #1a1c24;
            border: 3px dotted #6e727f;
            border-radius: 8px;
            padding: 6px;
        }
        QFrame#chartWrapper {
            background-color: #050606;
            border: 3px solid #0290ff;
            border-radius: 14px;
            padding: 8px;
        }
        QFrame#logFrame {
            background-color: #252830;
            border: 2px dotted #6e727f;
            border-radius: 8px;
            min-height: 90px;
        }
        QLabel#logLabel {
            color: #b7b7b7;
            font-size: 13px;
        }
        QFrame#devicesFrame, QFrame#fireFrame {
            background-color: #3a3c44;
            border: 2px solid #b4b4b4;
            border-radius: 6px;
        }
        QLabel#devicesHeading {
            font-size: 20px;
            font-weight: bold;
        }
        QPushButton#scanButton, QPushButton#refreshButton {
            border: 2px solid #cfcfcf;
            border-radius: 4px;
            padding: 6px 12px;
            background-color: #4d4f57;
        }
        QFrame#replayBar {
            background-color: #2a2c32;
            border: 1px solid #4d4d4d;
            border-radius: 8px;
            padding: 8px;
        }
        QPushButton#replayBtn {
            border: 2px solid #cfcfcf;
            border-radius: 4px;
            padding: 6px 14px;
            background-color: #4d4f57;
            color: #f0f0f0;
        }
        QComboBox#deviceSelector {
            background-color: #1a1a1a;
            color: #f5f5f5;
            border: 2px solid #b4b4b4;
            border-radius: 4px;
            padding: 4px 8px;
        }
        QPushButton#igniteButton {
            background-color: #d70000;
            color: white;
            border: 4px solid #f2f2f2;
            border-radius: 60px;
            padding: 20px;
            font-size: 18px;
            font-weight: 700;
        }
        QPushButton#timerButton {
            background-color: #1a7ad1;
            border-radius: 24px;
            color: white;
            padding: 8px 16px;
        }
        QFrame#indicatorStack QLabel {
            border: 2px solid #cfcfcf;
            border-radius: 6px;
            padding: 6px;
            background-color: #111;
        }
        QFrame#fireFrame QLabel#fireTitle {
            font-size: 20px;
            font-weight: 700;
        }
    )"_s);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setSpacing(14);
    rootLayout->setContentsMargins(18, 18, 18, 18);

    auto *replayBar = new QFrame(this);
    replayBar->setObjectName(u"replayBar"_s);
    auto *replayLayout = new QHBoxLayout(replayBar);
    replayLayout->setSpacing(10);

    m_playBtn = new QPushButton(u"Play"_s, replayBar);
    m_playBtn->setObjectName(u"replayBtn"_s);
    m_pauseBtn = new QPushButton(u"Pause"_s, replayBar);
    m_pauseBtn->setObjectName(u"replayBtn"_s);
    m_stopBtn = new QPushButton(u"Stop"_s, replayBar);
    m_stopBtn->setObjectName(u"replayBtn"_s);
    replayLayout->addWidget(m_playBtn);
    replayLayout->addWidget(m_pauseBtn);
    replayLayout->addWidget(m_stopBtn);

    m_replaySlider = new QSlider(Qt::Horizontal, replayBar);
    m_replaySlider->setRange(0, 0);
    m_replaySlider->setEnabled(false);
    replayLayout->addWidget(m_replaySlider, 1);

    m_speedSpin = new QDoubleSpinBox(replayBar);
    m_speedSpin->setRange(0.25, 4.0);
    m_speedSpin->setSingleStep(0.25);
    m_speedSpin->setValue(1.0);
    m_speedSpin->setPrefix(u"Speed "_s);
    m_speedSpin->setSuffix(u"x"_s);
    replayLayout->addWidget(m_speedSpin);

    m_replayStatus = new QLabel(u"No flight loaded"_s, replayBar);
    m_replayStatus->setStyleSheet(u"color: #9aa7b8;"_s);
    replayLayout->addWidget(m_replayStatus);

    rootLayout->addWidget(replayBar);

    if (m_replay) {
        connect(m_playBtn, &QPushButton::clicked, m_replay, &FlightReplayController::play);
        connect(m_pauseBtn, &QPushButton::clicked, m_replay, &FlightReplayController::pause);
        connect(m_stopBtn, &QPushButton::clicked, m_replay, &FlightReplayController::stop);
        connect(m_speedSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), m_replay, &FlightReplayController::setSpeed);
        connect(m_replaySlider, &QSlider::valueChanged, m_replay, &FlightReplayController::setPosition);
        connect(m_replay, &FlightReplayController::positionChanged, this, [this](int len) {
            if (m_replaySlider) {
                m_replaySlider->blockSignals(true);
                m_replaySlider->setValue(len);
                m_replaySlider->blockSignals(false);
            }
            setReplayTrailLength(len);
        });
        connect(m_replay, &FlightReplayController::playbackFinished, this, [this]() {
            if (m_replayStatus) {
                m_replayStatus->setText(u"Replay finished"_s);
            }
        });
        connect(m_replay, &FlightReplayController::errorOccurred, this, [this](const QString &msg) {
            if (m_replayStatus) {
                m_replayStatus->setText(msg);
            }
        });
    }

    auto *statsRowWidget = new QWidget(this);
    auto *statsLayout = new QHBoxLayout(statsRowWidget);
    statsLayout->setContentsMargins(0, 0, 0, 0);
    statsLayout->setSpacing(12);
    statsLayout->addWidget(createStatTile(u"ACCEL"_s, u"N/A"_s, statsRowWidget, &m_velValue), 1);
    statsLayout->addWidget(createStatTile(u"ALTITUDE"_s, u"N/A"_s, statsRowWidget, &m_altValue), 1);
    statsLayout->addWidget(createStatTile(u"TELEMETRY"_s, u"UNKNOWN"_s, statsRowWidget, &m_telemValue), 1);
    statsLayout->addWidget(createStatTile(u"DEVICES"_s, u"N/A"_s, statsRowWidget, nullptr), 1);
    rootLayout->addWidget(statsRowWidget);

    auto *contentLayout = new QHBoxLayout();
    contentLayout->setSpacing(16);
    rootLayout->addLayout(contentLayout);

    auto *leftColumn = new QVBoxLayout();
    leftColumn->setSpacing(12);
    auto *chartFrame = new QFrame(this);
    chartFrame->setObjectName(u"chartFrame"_s);
    auto *chartFrameLayout = new QVBoxLayout(chartFrame);
    chartFrameLayout->setContentsMargins(10, 10, 10, 10);
    chartFrameLayout->setSpacing(8);

    auto *chartWrapper = new QFrame(chartFrame);
    chartWrapper->setObjectName(u"chartWrapper"_s);
    auto *chartWrapperLayout = new QVBoxLayout(chartWrapper);
    chartWrapperLayout->setContentsMargins(12, 12, 12, 12);
    chartWrapperLayout->setSpacing(10);

    chartWrapperLayout->addWidget(
        buildTelemetryChart(chartWrapper, u"Altitude (m)"_s, QColor("#f4413f"), &m_altSeries, &m_axisTime, &m_axisAlt));
    chartWrapperLayout->addWidget(
        buildTelemetryChart(chartWrapper, u"Temp (°C)"_s, QColor("#4ecdc4"), &m_tempSeries, &m_axisTime2, &m_axisTemp));
    chartWrapperLayout->addWidget(
        buildTelemetryChart(chartWrapper, u"Pressure"_s, QColor("#ffe66d"), &m_pressSeries, &m_axisTime3, &m_axisPress));

    chartFrameLayout->addWidget(chartWrapper);

    auto *chartHint = new QLabel(
        u"Live: charts fill from serial telemetry. Replay: load a CSV in Settings, then scrub or play."_s,
        chartFrame);
    chartHint->setAlignment(Qt::AlignCenter);
    chartHint->setStyleSheet(u"color: #8f9aa8; font-style: italic;"_s);
    chartFrameLayout->addWidget(chartHint);
    leftColumn->addWidget(chartFrame, 4);

    auto *logFrame = new QFrame(this);
    logFrame->setObjectName(u"logFrame"_s);
    auto *logLayout = new QVBoxLayout(logFrame);
    logLayout->setContentsMargins(12, 12, 12, 12);
    m_logLabel = new QLabel(u"No events received yet."_s, logFrame);
    m_logLabel->setObjectName(u"logLabel"_s);
    m_logLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    logLayout->addWidget(m_logLabel);
    leftColumn->addWidget(logFrame, 1);

    contentLayout->addLayout(leftColumn, 3);

    auto *rightColumn = new QVBoxLayout();
    rightColumn->setSpacing(12);

    auto *devicesFrame = new QFrame(this);
    devicesFrame->setObjectName(u"devicesFrame"_s);
    auto *devicesLayout = new QVBoxLayout(devicesFrame);
    devicesLayout->setSpacing(10);

    auto *devicesHeader = new QHBoxLayout();
    auto *devicesLabel = new QLabel(u"Devices: N/A"_s, devicesFrame);
    devicesLabel->setObjectName(u"devicesHeading"_s);
    devicesHeader->addWidget(devicesLabel);
    devicesHeader->addStretch();
    auto *scanButton = new QPushButton(u"Scan Devices"_s, devicesFrame);
    scanButton->setObjectName(u"scanButton"_s);
    scanButton->setEnabled(false);
    devicesHeader->addWidget(scanButton);
    auto *refreshButton = new QPushButton(u"Refresh"_s, devicesFrame);
    refreshButton->setObjectName(u"refreshButton"_s);
    refreshButton->setEnabled(false);
    devicesHeader->addWidget(refreshButton);
    devicesLayout->addLayout(devicesHeader);

    auto *devicesPlaceholder = new QLabel(u"Awaiting device telemetry."_s, devicesFrame);
    devicesPlaceholder->setAlignment(Qt::AlignCenter);
    devicesLayout->addWidget(devicesPlaceholder, 1);

    rightColumn->addWidget(devicesFrame, 3);

    auto *fireFrame = new QFrame(this);
    fireFrame->setObjectName(u"fireFrame"_s);
    auto *fireLayout = new QVBoxLayout(fireFrame);
    fireLayout->setSpacing(10);

    auto *fireTitle = new QLabel(u"Fire Ignitor"_s, fireFrame);
    fireTitle->setObjectName(u"fireTitle"_s);
    fireLayout->addWidget(fireTitle);

    auto *selector = new QComboBox(fireFrame);
    selector->setObjectName(u"deviceSelector"_s);
    selector->addItem(u"No device selected"_s);
    selector->setEnabled(false);
    fireLayout->addWidget(selector);

    auto *indicatorStack = new QFrame(fireFrame);
    indicatorStack->setObjectName(u"indicatorStack"_s);
    auto *indicatorLayout = new QHBoxLayout(indicatorStack);
    indicatorLayout->setSpacing(12);
    auto *batteryA = new QLabel(u"IGN-A"_s, indicatorStack);
    auto *batteryB = new QLabel(u"IGN-B"_s, indicatorStack);
    indicatorLayout->addWidget(batteryA);
    indicatorLayout->addWidget(batteryB);
    fireLayout->addWidget(indicatorStack);

    auto *igniteRow = new QHBoxLayout();
    igniteRow->setSpacing(12);
    auto *igniteButton = new QPushButton(u"FIRE\nIGNITE"_s, fireFrame);
    igniteButton->setObjectName(u"igniteButton"_s);
    igniteButton->setFixedSize(120, 120);
    igniteButton->setEnabled(false);
    igniteRow->addWidget(igniteButton, 0, Qt::AlignCenter);

    auto *timerButton = new QPushButton(u"TIMER"_s, fireFrame);
    timerButton->setObjectName(u"timerButton"_s);
    timerButton->setFixedSize(70, 70);
    timerButton->setEnabled(false);
    igniteRow->addWidget(timerButton, 0, Qt::AlignBottom);
    igniteRow->addStretch(1);
    fireLayout->addLayout(igniteRow);

    rightColumn->addWidget(fireFrame, 2);

    contentLayout->addLayout(rightColumn, 1);

    if (m_model) {
        connect(m_model, &FlightDataModel::sampleUpdated, this, &DashboardPage::onSampleUpdated);
        connect(m_model, &FlightDataModel::sessionReset, this, &DashboardPage::onSessionReset);
    }
}

void DashboardPage::setReplaySession(const FlightSession *session) {
    m_session = session;
    const int n = session ? static_cast<int>(session->samples.size()) : 0;
    if (m_replaySlider) {
        m_replaySlider->setMaximum(std::max(0, n));
        m_replaySlider->setEnabled(n > 0);
        m_replaySlider->setValue(0);
    }
    if (m_replayStatus) {
        m_replayStatus->setText(n > 0 ? QStringLiteral("Loaded %1 samples").arg(n) : u"No flight loaded"_s);
    }
    rebuildReplayCharts(0);
}

void DashboardPage::setReplayTrailLength(int trailLength) {
    rebuildReplayCharts(trailLength);
}

void DashboardPage::rebuildReplayCharts(int trailLength) {
    if (!m_altSeries || !m_tempSeries || !m_pressSeries) {
        return;
    }
    m_altSeries->clear();
    m_tempSeries->clear();
    m_pressSeries->clear();

    if (!m_session || trailLength <= 0 || m_session->samples.empty()) {
        if (m_axisTime) {
            m_axisTime->setRange(0, 10);
        }
        if (m_axisTime2) {
            m_axisTime2->setRange(0, 10);
        }
        if (m_axisTime3) {
            m_axisTime3->setRange(0, 10);
        }
        return;
    }

    const auto &samples = m_session->samples;
    const int n = static_cast<int>(samples.size());
    const int end = std::min(trailLength, n);
    if (end <= 0) {
        return;
    }

    const long t0 = samples[0].timestamp;
    int step = 1;
    if (end > kMaxChartPoints) {
        step = (end + kMaxChartPoints - 1) / kMaxChartPoints;
    }

    double xMin = 0;
    double xMax = 1;
    double altMin = samples[0].altitude;
    double altMax = altMin;
    double tempMin = samples[0].temperature;
    double tempMax = tempMin;
    double pressMin = samples[0].pressure;
    double pressMax = pressMin;

    for (int i = 0; i < end; i += step) {
        const FlightSample &s = samples[static_cast<std::size_t>(i)];
        const double x = elapsedSeconds(t0, s.timestamp);
        m_altSeries->append(x, s.altitude);
        m_tempSeries->append(x, s.temperature);
        m_pressSeries->append(x, s.pressure);
        xMax = x;
        altMin = std::min(altMin, s.altitude);
        altMax = std::max(altMax, s.altitude);
        tempMin = std::min(tempMin, s.temperature);
        tempMax = std::max(tempMax, s.temperature);
        pressMin = std::min(pressMin, s.pressure);
        pressMax = std::max(pressMax, s.pressure);
    }
    const int last = end - 1;
    if (last % step != 0) {
        const FlightSample &s = samples[static_cast<std::size_t>(last)];
        const double x = elapsedSeconds(t0, s.timestamp);
        m_altSeries->append(x, s.altitude);
        m_tempSeries->append(x, s.temperature);
        m_pressSeries->append(x, s.pressure);
        xMax = x;
        altMin = std::min(altMin, s.altitude);
        altMax = std::max(altMax, s.altitude);
        tempMin = std::min(tempMin, s.temperature);
        tempMax = std::max(tempMax, s.temperature);
        pressMin = std::min(pressMin, s.pressure);
        pressMax = std::max(pressMax, s.pressure);
    }

    const double xPad = (xMax - xMin) * 0.02 + 0.05;
    const double xa = xMin - xPad;
    const double xb = xMax + xPad;
    if (m_axisTime) {
        m_axisTime->setRange(xa, xb);
    }
    if (m_axisTime2) {
        m_axisTime2->setRange(xa, xb);
    }
    if (m_axisTime3) {
        m_axisTime3->setRange(xa, xb);
    }

    auto padY = [](double lo, double hi) {
        const double span = std::max(hi - lo, 1e-6);
        const double p = span * 0.08 + 0.5;
        return std::pair{lo - p, hi + p};
    };
    if (m_axisAlt) {
        const auto [a, b] = padY(altMin, altMax);
        m_axisAlt->setRange(a, b);
    }
    if (m_axisTemp) {
        const auto [a, b] = padY(tempMin, tempMax);
        m_axisTemp->setRange(a, b);
    }
    if (m_axisPress) {
        const auto [a, b] = padY(pressMin, pressMax);
        m_axisPress->setRange(a, b);
    }
}

void DashboardPage::onSessionReset() {
    m_haveT0 = false;
    if (m_altSeries) {
        m_altSeries->clear();
    }
    if (m_tempSeries) {
        m_tempSeries->clear();
    }
    if (m_pressSeries) {
        m_pressSeries->clear();
    }
    if (m_axisTime) {
        m_axisTime->setRange(0, 10);
    }
    if (m_axisTime2) {
        m_axisTime2->setRange(0, 10);
    }
    if (m_axisTime3) {
        m_axisTime3->setRange(0, 10);
    }
}

void DashboardPage::appendLiveChartPoint(const FlightSample &sample) {
    if (!m_altSeries || !m_tempSeries || !m_pressSeries || !m_axisTime || !m_axisAlt || !m_axisTemp || !m_axisPress) {
        return;
    }
    if (!m_haveT0) {
        m_t0Ms = sample.timestamp;
        m_haveT0 = true;
    }
    const double x = elapsedSeconds(m_t0Ms, sample.timestamp);
    m_altSeries->append(x, sample.altitude);
    m_tempSeries->append(x, sample.temperature);
    m_pressSeries->append(x, sample.pressure);

    while (m_altSeries->count() > kMaxChartPoints) {
        m_altSeries->remove(0);
        m_tempSeries->remove(0);
        m_pressSeries->remove(0);
    }

    double xMin = m_altSeries->at(0).x();
    double xMax = m_altSeries->at(m_altSeries->count() - 1).x();
    const double xPad = (xMax - xMin) * 0.05 + 0.1;
    const double xa = xMin - xPad;
    const double xb = xMax + xPad;
    m_axisTime->setRange(xa, xb);
    if (m_axisTime2) {
        m_axisTime2->setRange(xa, xb);
    }
    if (m_axisTime3) {
        m_axisTime3->setRange(xa, xb);
    }

    double altMin = sample.altitude;
    double altMax = sample.altitude;
    double tempMin = sample.temperature;
    double tempMax = sample.temperature;
    double pressMin = sample.pressure;
    double pressMax = sample.pressure;
    for (int i = 0; i < m_altSeries->count(); ++i) {
        const QPointF p = m_altSeries->at(i);
        altMin = std::min(altMin, p.y());
        altMax = std::max(altMax, p.y());
        const double ty = m_tempSeries->at(i).y();
        tempMin = std::min(tempMin, ty);
        tempMax = std::max(tempMax, ty);
        const double py = m_pressSeries->at(i).y();
        pressMin = std::min(pressMin, py);
        pressMax = std::max(pressMax, py);
    }
    auto padY = [](double lo, double hi) {
        const double span = std::max(hi - lo, 1e-6);
        const double p = span * 0.08 + 0.5;
        return std::pair{lo - p, hi + p};
    };
    const auto pa = padY(altMin, altMax);
    m_axisAlt->setRange(pa.first, pa.second);
    const auto pt = padY(tempMin, tempMax);
    m_axisTemp->setRange(pt.first, pt.second);
    const auto pp = padY(pressMin, pressMax);
    m_axisPress->setRange(pp.first, pp.second);
}

void DashboardPage::onSampleUpdated(const FlightSample &sample) {
    const double accelMag = std::sqrt(
        sample.acceleration.x * sample.acceleration.x
        + sample.acceleration.y * sample.acceleration.y
        + sample.acceleration.z * sample.acceleration.z);
    if (m_velValue) {
        m_velValue->setText(QStringLiteral("%1 m/s²").arg(accelMag, 0, 'f', 2));
    }
    if (m_altValue) {
        m_altValue->setText(QStringLiteral("%1 m").arg(sample.altitude, 0, 'f', 1));
    }
    if (m_telemValue) {
        if (m_model && m_model->replayMode()) {
            m_telemValue->setText(u"REPLAY"_s);
        } else {
            m_telemValue->setText(u"LIVE"_s);
        }
    }
    if (m_logLabel) {
        m_logLabel->setText(
            QStringLiteral("t=%1 ms | alt=%2 | temp=%3")
                .arg(sample.timestamp)
                .arg(sample.altitude, 0, 'f', 1)
                .arg(sample.temperature, 0, 'f', 1));
    }

    if (!m_model || m_model->replayMode()) {
        return;
    }
    appendLiveChartPoint(sample);
}
