// #include "gui/pages/MonitoringPage.h"
// #include "gui/MainWindow.h"
//
// #include <QAbstractItemView>
// #include <QBrush>
// #include <QColor>
// #include <QDateTime>
// #include <QFrame>
// #include <QLabel>
// #include <QListWidget>
// #include <QPainter>
// #include <QPaintEvent>
// #include <QPlainTextEdit>
// #include <QPointF>
// #include <QPushButton>
// #include <QScrollBar>
// #include <QSplitter>
// #include <QVBoxLayout>
//
// using namespace Qt::StringLiterals;
//
// MonitoringPage::MonitoringPage(MainWindow *hostWindow, QWidget *parent)
// : QWidget(parent), m_hostWindow(hostWindow) {
//     setAttribute(Qt::WA_OpaquePaintEvent);
//     setAutoFillBackground(false);
//
//     auto *layout = new QVBoxLayout(this);
//     layout->setContentsMargins(24, 24, 24, 24);
//     layout->setSpacing(16);
//
//     auto *textFrame = new QFrame(this);
//     textFrame->setObjectName("monitoringDescriptionFrame");
//     textFrame->setFrameShape(QFrame::StyledPanel);
//     textFrame->setFrameShadow(QFrame::Raised);
//     textFrame->setStyleSheet(R"(
//     QFrame#monitoringDescriptionFrame {
//     background-color:rgba(21, 22, 25, 0.80) ;
//     border: 1px solid #3b3b45;
//     border-radius: 8px;
//     padding: 12px;
//     }
//     )");
//
//     auto *description = new QLabel(u"Monitor live serial output. Use the controls below to scan for ports and view data directly on this page."_s, this);
//     description->setWordWrap(true);
//     description->setStyleSheet(u"color: #d9e0f2;"_s);
//     layout->addWidget(description);
//
//     layout->addWidget(textFrame);
//
//     auto *monitorCard = new QFrame(this);
//     monitorCard->setObjectName(u"serialMonitorCard"_s);
//     monitorCard->setStyleSheet(uR"(
//         QFrame#serialMonitorCard {
//             background: rgba(10, 12, 16, 0.92);
//             border: 1px solid #2c2f36;
//             border-radius: 10px;
//             padding: 14px;
//         }
//     )"_s);
//     auto *cardLayout = new QVBoxLayout(monitorCard);
//     cardLayout->setSpacing(12);
//     cardLayout->setContentsMargins(8, 8, 8, 8);
//
//     auto *headerLayout = new QHBoxLayout();
//     auto *title = new QLabel(u"Serial Port Monitor"_s, monitorCard);
//     title->setStyleSheet(u"font-size: 18px; font-weight: 600; color: #f4f7ff;"_s);
//     headerLayout->addWidget(title);
//     headerLayout->addStretch();
//     m_statusLabel = new QLabel(u"Idle. Click \"Scan\" to search."_s, monitorCard);
//     m_statusLabel->setStyleSheet(u"color: #cfd8e3;"_s);
//     headerLayout->addWidget(m_statusLabel);
//     cardLayout->addLayout(headerLayout);
//
//     auto *controls = new QHBoxLayout();
//     controls->setSpacing(10);
//     auto *searchButton = new QPushButton(u"Scan Ports"_s, monitorCard);
//     searchButton->setCursor(Qt::PointingHandCursor);
//     connect(searchButton, &QPushButton::clicked, this, [this]() {
//         if (m_statusLabel) {
//             m_statusLabel->setText(u"Scanning... waiting for results"_s);
//         }
//         if (m_portList) {
//             m_portList->clear();
//         }
//         emit scanPortsRequested();
//     });
//     controls->addWidget(searchButton);
//
//     m_connectButton = new QPushButton(u"Connect to Selected"_s, monitorCard);
//     m_connectButton->setCursor(Qt::PointingHandCursor);
//     m_connectButton->setEnabled(false);
//     connect(m_connectButton, &QPushButton::clicked, this, [this]() {
//         if (const auto *item = m_portList ? m_portList->currentItem() : nullptr) {
//             emit connectToPortRequested(item->text());
//         }
//     });
//     controls->addWidget(m_connectButton);
//
//     auto *clearLogButton = new QPushButton(u"Clear Log"_s, monitorCard);
//     clearLogButton->setCursor(Qt::PointingHandCursor);
//     controls->addStretch();
//     controls->addWidget(clearLogButton);
//     cardLayout->addLayout(controls);
//
//     auto *splitter = new QSplitter(Qt::Horizontal, monitorCard);
//     splitter->setChildrenCollapsible(false);
//
//     auto *portsPane = new QWidget(splitter);
//     auto *portsLayout = new QVBoxLayout(portsPane);
//     portsLayout->setContentsMargins(0, 0, 0, 0);
//     portsLayout->setSpacing(8);
//
//     auto *portsLabel = new QLabel(u"Ports"_s, portsPane);
//     portsLabel->setStyleSheet(u"font-weight: 600; color: #e9ecf5;"_s);
//     portsLayout->addWidget(portsLabel);
//
//     m_portList = new QListWidget(portsPane);
//     m_portList->setSelectionMode(QAbstractItemView::SingleSelection);
//     m_portList->setStyleSheet(u"QListWidget { background: #151515; color: #f5f5f5; border: 1px solid #2f2f2f; }"_s);
//     portsLayout->addWidget(m_portList, 1);
//     splitter->addWidget(portsPane);
//
//     connect(m_portList, &QListWidget::itemSelectionChanged, this, [this]() {
//         if (m_connectButton) {
//             m_connectButton->setEnabled(m_portList && !m_portList->selectedItems().isEmpty());
//         }
//     });
//     connect(m_portList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
//         if (item) {
//             emit connectToPortRequested(item->text());
//         }
//     });
//
//     auto *logPane = new QWidget(splitter);
//     auto *logLayout = new QVBoxLayout(logPane);
//     logLayout->setContentsMargins(0, 0, 0, 0);
//     logLayout->setSpacing(8);
//
//     auto *logLabel = new QLabel(u"Live Port Output"_s, logPane);
//     logLabel->setStyleSheet(u"font-weight: 600; color: #e9ecf5;"_s);
//     logLayout->addWidget(logLabel);
//
//     m_logView = new QPlainTextEdit(logPane);
//     m_logView->setReadOnly(true);
//     m_logView->setMaximumBlockCount(2000);
//     m_logView->setStyleSheet(u"QPlainTextEdit { background: #0f1115; color: #d9e0f2; border: 1px solid #2f2f2f; font-family: \"Red Hat Mono\", \"Courier New\", monospace; }"_s);
//     logLayout->addWidget(m_logView, 1);
//
//     splitter->addWidget(logPane);
//     splitter->setStretchFactor(0, 1);
//     splitter->setStretchFactor(1, 2);
//
//     cardLayout->addWidget(splitter, 1);
//
//     connect(clearLogButton, &QPushButton::clicked, m_logView, &QPlainTextEdit::clear);
//
//     layout->addWidget(monitorCard);
// }
//
// void MonitoringPage::paintEvent(QPaintEvent *event) {
//     QPainter painter(this);
//     painter.setClipRegion(event->region());
//
//     const QColor backgroundColor(47, 47, 47);
//     painter.fillRect(rect(), backgroundColor);
//
//     painter.setRenderHint(QPainter::Antialiasing, true);
//     painter.setPen(Qt::NoPen);
//     painter.setBrush(QColor(255, 255, 255, 50));
//
//     constexpr int dotSpacing = 28;
//     constexpr qreal dotDiameter = 3.0;
//     const qreal dotRadius = dotDiameter / 2.0;
//     const int offset = dotSpacing / 2;
//
//     const int widthLimit = width();
//     const int heightLimit = height();
//
//     for (int y = offset; y < heightLimit; y += dotSpacing) {
//         for (int x = offset; x < widthLimit; x += dotSpacing) {
//             painter.drawEllipse(QPointF(x, y), dotRadius, dotRadius);
//         }
//     }
// }
//
// void MonitoringPage::showAvailablePorts(const QStringList &ports) {
//     if (!m_portList || !m_statusLabel) return;
//     m_portList->clear();
//     m_portList->addItems(ports);
//     if (ports.isEmpty()) {
//         m_statusLabel->setText(u"No ports reported."_s);
//     } else {
//         m_statusLabel->setText(u"Select a port and click connect."_s);
//         m_portList->setCurrentRow(0);
//     }
// }
//
// void MonitoringPage::appendSerialLog(const QString &text) {
//     if (m_logView) {
//         const QString timestamp = QDateTime::currentDateTime().toString(u"HH:mm:ss.zzz "_s);
//         m_logView->appendPlainText(timestamp + text);
//         if (auto *scrollBar = m_logView->verticalScrollBar()) {
//             scrollBar->setValue(scrollBar->maximum());
//         }
//     }
// }
//
// void MonitoringPage::showSerialMonitor() {
//     if (m_portList) {
//         m_portList->setFocus();
//     }
// }
