#include "gui/pages/MonitoringPage.h"

#include "gui/FlightDataModel.h"
#include "gui/MainWindow.h"

#include <QBrush>
#include <QColor>
#include <QFrame>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QPointF>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

MonitoringPage::MonitoringPage(MainWindow *hostWindow, FlightDataModel *model, QWidget *parent)
    : QWidget(parent),
      m_hostWindow(hostWindow),
      m_model(model) {
    Q_UNUSED(hostWindow);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAutoFillBackground(false);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(16);

    auto *textFrame = new QFrame(this);
    textFrame->setObjectName("monitoringDescriptionFrame");
    textFrame->setFrameShape(QFrame::StyledPanel);
    textFrame->setFrameShadow(QFrame::Raised);
    textFrame->setStyleSheet(R"(
    QFrame#monitoringDescriptionFrame {
    background-color:rgba(21, 22, 25, 0.80) ;
    border: 1px solid #3b3b45;
    border-radius: 8px;
    padding: 12px;
    }
    )");

    m_summaryLabel = new QLabel(
        u"Monitoring: waiting for telemetry. Open Settings to connect a serial port."_s,
        textFrame);
    m_summaryLabel->setWordWrap(true);
    m_summaryLabel->setStyleSheet(u"color: #e8e8e8; font-size: 14px;"_s);

    auto *frameLayout = new QVBoxLayout(textFrame);
    frameLayout->addWidget(m_summaryLabel);

    layout->addWidget(textFrame);
    layout->addStretch(1);

    if (m_model) {
        connect(m_model, &FlightDataModel::sampleUpdated, this, &MonitoringPage::onSampleUpdated);
    }
}

void MonitoringPage::onSampleUpdated(const FlightSample &sample) {
    if (!m_summaryLabel) {
        return;
    }
    m_summaryLabel->setText(
        QStringLiteral(
            "Last sample — Alt: %1 m, Temp: %2 °C, Press: %3, Batt: %4 V")
            .arg(sample.altitude, 0, 'f', 1)
            .arg(sample.temperature, 0, 'f', 1)
            .arg(sample.pressure, 0, 'f', 1)
            .arg(sample.batteryVoltage, 0, 'f', 2));
}

void MonitoringPage::paintEvent(QPaintEvent *event) {
    QPainter painter(this);
    painter.setClipRegion(event->region());

    const QColor backgroundColor(47, 47, 47);
    painter.fillRect(rect(), backgroundColor);

    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, 50));

    constexpr int dotSpacing = 28;
    constexpr qreal dotDiameter = 3.0;
    const qreal dotRadius = dotDiameter / 2.0;
    const int offset = dotSpacing / 2;

    const int widthLimit = width();
    const int heightLimit = height();

    for (int y = offset; y < heightLimit; y += dotSpacing) {
        for (int x = offset; x < widthLimit; x += dotSpacing) {
            painter.drawEllipse(QPointF(x, y), dotRadius, dotRadius);
        }
    }
}
