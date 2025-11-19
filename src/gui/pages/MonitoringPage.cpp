#include "pages/MonitoringPage.h"
#include "MainWindow.h"

#include <QColor>
#include <QFrame>
#include <QLabel>
#include <QPainter>
#include <QBrush>
#include <QPaintEvent>
#include <QPointF>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

MonitoringPage::MonitoringPage(MainWindow *hostWindow, QWidget *parent)
: QWidget(parent), m_hostWindow(hostWindow) {
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


    auto *description = new QLabel(u""_s,this);
    description->setWordWrap(true);
    layout->addWidget(description);

    layout->addWidget(textFrame);
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
