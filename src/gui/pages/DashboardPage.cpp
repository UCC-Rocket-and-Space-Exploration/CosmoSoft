// #include "pages/DashboardPage.h"
//
// #include <QComboBox>
// #include <QFrame>
// #include <QHBoxLayout>
// #include <QLabel>
// #include <QPainter>
// #include <QPushButton>
// #include <QVBoxLayout>
// #include <QtCharts/QChart>
// #include <QtCharts/QChartView>
// #include <QtCharts/QLineSeries>
// #include <QtCharts/QValueAxis>
// #include <QtMath>
//
// using namespace Qt::StringLiterals;
//
// namespace {
// QChartView *buildChart(QWidget *parent) {
//     auto *series = new QLineSeries(parent);
//     series->setUseOpenGL(false);
//     series->setColor(QColor("#f4413f"));
//     series->setPen(QPen(series->color(), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
//
//     auto *chart = new QChart();
//     chart->legend()->hide();
//     chart->addSeries(series);
//     chart->setMargins(QMargins(12, 12, 12, 12));
//     chart->setBackgroundBrush(QColor("#0a0a0a"));
//     chart->setBackgroundPen(Qt::NoPen);
//
//     auto *axisX = new QValueAxis();
//     axisX->setRange(0, 10);
//     axisX->setTickCount(6);
//     axisX->setLabelsColor(QColor("#f5f5f5"));
//     axisX->setLinePenColor(QColor("#f5f5f5"));
//     axisX->setGridLineColor(QColor(255, 255, 255, 40));
//     chart->addAxis(axisX, Qt::AlignBottom);
//     series->attachAxis(axisX);
//
//     auto *axisY = new QValueAxis();
//     axisY->setRange(-1, 1);
//     axisY->setTickCount(5);
//     axisY->setLabelsColor(QColor("#f5f5f5"));
//     axisY->setLinePenColor(QColor("#f5f5f5"));
//     axisY->setGridLineColor(QColor(255, 255, 255, 40));
//     chart->addAxis(axisY, Qt::AlignLeft);
//     series->attachAxis(axisY);
//
//     auto *chartView = new QChartView(chart, parent);
//     chartView->setRenderHint(QPainter::Antialiasing);
//     chartView->setObjectName(u"chartView"_s);
//     return chartView;
// }
//
// QFrame *createStatTile(const QString &label, const QString &value, QWidget *parent) {
//     auto *tile = new QFrame(parent);
//     tile->setProperty("kind", u"statTile"_s);
//     auto *layout = new QVBoxLayout(tile);
//     layout->setContentsMargins(12, 8, 12, 8);
//     layout->setSpacing(2);
//
//     auto *title = new QLabel(label, tile);
//     title->setProperty("kind", u"statLabel"_s);
//     layout->addWidget(title);
//
//     auto *valueLabel = new QLabel(value, tile);
//     valueLabel->setProperty("kind", u"statValue"_s);
//     layout->addWidget(valueLabel);
//
//     return tile;
// }
// } // namespace
//
// DashboardPage::DashboardPage(QWidget *parent)
//         : QWidget(parent) {
//     setObjectName(u"dashboardPage"_s);
//     setStyleSheet(uR"(
//         #dashboardPage {
//             background-color: #1f1f1f;
//             color: #f8f8f8;
//             font-family: "Red Hat Mono", "Courier New", "Roboto Mono", monospace;
//         }
//         #dashboardPage QWidget {
//             font-family: "Red Hat Mono", "Courier New", "Roboto Mono", monospace;
//         }
//         QFrame#headerFrame {
//             background-color: #3a3a3a;
//             border: 2px solid #4d4d4d;
//             border-radius: 6px;
//             border-bottom: 4px solid #5d5d5d;
//         }
//         QLabel#brandLabel {
//             font-size: 26px;
//             letter-spacing: 2px;
//         }
//         QLabel#missionTitle {
//             font-size: 24px;
//             font-weight: 600;
//         }
//         QLabel#missionMeta {
//             font-size: 14px;
//             color: #d6d6d6;
//         }
//         QPushButton[kind="navigation"] {
//             border: 2px solid rgba(217, 217, 217, 1);
//             border-radius: 4px;
//             padding: 8px 20px;
//             background-color: #4b4b4b;
//             color: rgba(217, 217, 217, 1);
//             font-size: 15px;
//         }
//         QPushButton[kind="navigation"][active="true"] {
//             background-color: rgba(217, 217, 217, 1);
//             color: #1a1a1a;
//             border-color: rgba(217, 217, 217, 1);
//         }
//         QLabel#alertLabel {
//             border: 3px solid #d1d1d1;
//             border-radius: 6px;
//             padding: 10px;
//             background-color: #d82323;
//             color: white;
//             font-weight: 700;
//             letter-spacing: 2px;
//             text-transform: uppercase;
//         }
//         QLabel#metricsLabel {
//             font-size: 15px;
//             color: #fdfdfd;
//             border: 2px solid #404040;
//             border-radius: 4px;
//             padding: 6px 12px;
//             background-color: #2b2b2b;
//             letter-spacing: 1px;
//         }
//         QFrame[kind="statTile"] {
//             background-color: #2b2d33;
//             border: 2px dotted #8ab6ff;
//             border-radius: 6px;
//         }
//         QLabel[kind="statLabel"] {
//             font-size: 12px;
//             text-transform: uppercase;
//             color: #b8c1cc;
//             letter-spacing: 1px;
//         }
//         QLabel[kind="statValue"] {
//             font-size: 20px;
//             font-weight: 700;
//             color: #fefefe;
//         }
//         QFrame#chartFrame {
//             background-color: #1a1c24;
//             border: 3px dotted #6e727f;
//             border-radius: 8px;
//             padding: 6px;
//         }
//         QFrame#chartWrapper {
//             background-color: #050606;
//             border: 3px solid #0290ff;
//             border-radius: 14px;
//             padding: 8px;
//         }
//         QFrame#logFrame {
//             background-color: #252830;
//             border: 2px dotted #6e727f;
//             border-radius: 8px;
//             min-height: 90px;
//         }
//         QLabel#logLabel {
//             color: #b7b7b7;
//             font-size: 13px;
//         }
//         QFrame#devicesFrame, QFrame#fireFrame {
//             background-color: #3a3c44;
//             border: 2px solid #b4b4b4;
//             border-radius: 6px;
//         }
//         QLabel#devicesHeading {
//             font-size: 20px;
//             font-weight: bold;
//         }
//         QPushButton#scanButton, QPushButton#refreshButton {
//             border: 2px solid #cfcfcf;
//             border-radius: 4px;
//             padding: 6px 12px;
//             background-color: #4d4f57;
//         }
//         QFrame[kind="deviceCard"] {
//             background-color: #2e3038;
//             border: 2px solid #8c8c8c;
//             border-radius: 6px;
//         }
//         QFrame[kind="deviceCard"][highlighted="true"] {
//             border-color: #ffffff;
//         }
//         QLabel[kind="deviceIndicator"] {
//             border-radius: 7px;
//             background-color: #63ff85;
//         }
//         QLabel[kind="deviceIndicator"][highlighted="false"] {
//             background-color: #4fba5f;
//         }
//         QLabel[kind="deviceTitle"] {
//             font-size: 16px;
//             font-weight: 600;
//         }
//         QLabel[kind="deviceLatency"] {
//             color: #ff8484;
//             font-size: 13px;
//         }
//         QPushButton[kind="deviceViewButton"] {
//             border: 1px solid #cfcfcf;
//             border-radius: 4px;
//             padding: 4px 12px;
//         }
//         QFrame#fireFrame QLabel#fireTitle {
//             font-size: 20px;
//             font-weight: 700;
//         }
//         QComboBox#deviceSelector {
//             background-color: #1a1a1a;
//             color: #f5f5f5;
//             border: 2px solid #b4b4b4;
//             border-radius: 4px;
//             padding: 4px 8px;
//         }
//         QPushButton#igniteButton {
//             background-color: #d70000;
//             color: white;
//             border: 4px solid #f2f2f2;
//             border-radius: 60px;
//             padding: 20px;
//             font-size: 18px;
//             font-weight: 700;
//         }
//         QPushButton#timerButton {
//             background-color: #1a7ad1;
//             border-radius: 24px;
//             color: white;
//             padding: 8px 16px;
//         }
//         QFrame#indicatorStack QLabel {
//             border: 2px solid #cfcfcf;
//             border-radius: 6px;
//             padding: 6px;
//             background-color: #111;
//         }
//     )"_s);
//
//     auto *rootLayout = new QVBoxLayout(this);
//     rootLayout->setSpacing(14);
//     rootLayout->setContentsMargins(18, 18, 18, 18);
//
//     auto *statsRowWidget = new QWidget(this);
//     auto *statsLayout = new QHBoxLayout(statsRowWidget);
//     statsLayout->setContentsMargins(0, 0, 0, 0);
//     statsLayout->setSpacing(12);
//     statsLayout->addWidget(createStatTile(u"VELOCITY"_s, u"N/A"_s, statsRowWidget), 1);
//     statsLayout->addWidget(createStatTile(u"ALTITUDE"_s, u"N/A"_s, statsRowWidget), 1);
//     statsLayout->addWidget(createStatTile(u"TELEMETRY"_s, u"UNKNOWN"_s, statsRowWidget), 1);
//     statsLayout->addWidget(createStatTile(u"DEVICES"_s, u"N/A"_s, statsRowWidget), 1);
//     rootLayout->addWidget(statsRowWidget);
//
//     auto *contentLayout = new QHBoxLayout();
//     contentLayout->setSpacing(16);
//     rootLayout->addLayout(contentLayout);
//
//     auto *leftColumn = new QVBoxLayout();
//     leftColumn->setSpacing(12);
//     auto *chartFrame = new QFrame(this);
//     chartFrame->setObjectName(u"chartFrame"_s);
//     auto *chartFrameLayout = new QVBoxLayout(chartFrame);
//     chartFrameLayout->setContentsMargins(10, 10, 10, 10);
//     chartFrameLayout->setSpacing(8);
//
//     auto *chartWrapper = new QFrame(chartFrame);
//     chartWrapper->setObjectName(u"chartWrapper"_s);
//     auto *chartWrapperLayout = new QVBoxLayout(chartWrapper);
//     chartWrapperLayout->setContentsMargins(12, 12, 12, 12);
//     auto *chartView = buildChart(chartWrapper);
//     chartWrapperLayout->addWidget(chartView);
//     chartFrameLayout->addWidget(chartWrapper);
//
//     auto *chartPlaceholder = new QLabel(u"No telemetry series loaded."_s, chartFrame);
//     chartPlaceholder->setAlignment(Qt::AlignCenter);
//     chartPlaceholder->setStyleSheet(u"color: #8f9aa8; font-style: italic;"_s);
//     chartFrameLayout->addWidget(chartPlaceholder);
//     leftColumn->addWidget(chartFrame, 4);
//
//     auto *logFrame = new QFrame(this);
//     logFrame->setObjectName(u"logFrame"_s);
//     auto *logLayout = new QVBoxLayout(logFrame);
//     logLayout->setContentsMargins(12, 12, 12, 12);
//     auto *logLabel = new QLabel(u"No events received yet."_s, logFrame);
//     logLabel->setObjectName(u"logLabel"_s);
//     logLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
//     logLayout->addWidget(logLabel);
//     leftColumn->addWidget(logFrame, 1);
//
//     contentLayout->addLayout(leftColumn, 3);
//
//     auto *rightColumn = new QVBoxLayout();
//     rightColumn->setSpacing(12);
//
//     auto *devicesFrame = new QFrame(this);
//     devicesFrame->setObjectName(u"devicesFrame"_s);
//     auto *devicesLayout = new QVBoxLayout(devicesFrame);
//     devicesLayout->setSpacing(10);
//
//     auto *devicesHeader = new QHBoxLayout();
//     auto *devicesLabel = new QLabel(u"Devices: N/A"_s, devicesFrame);
//     devicesLabel->setObjectName(u"devicesHeading"_s);
//     devicesHeader->addWidget(devicesLabel);
//     devicesHeader->addStretch();
//     auto *scanButton = new QPushButton(u"Scan Devices"_s, devicesFrame);
//     scanButton->setObjectName(u"scanButton"_s);
//     scanButton->setEnabled(false);
//     devicesHeader->addWidget(scanButton);
//     auto *refreshButton = new QPushButton(u"Refresh"_s, devicesFrame);
//     refreshButton->setObjectName(u"refreshButton"_s);
//     refreshButton->setEnabled(false);
//     devicesHeader->addWidget(refreshButton);
//     devicesLayout->addLayout(devicesHeader);
//
//     auto *devicesPlaceholder = new QLabel(u"Awaiting device telemetry."_s, devicesFrame);
//     devicesPlaceholder->setAlignment(Qt::AlignCenter);
//     devicesLayout->addWidget(devicesPlaceholder, 1);
//
//     rightColumn->addWidget(devicesFrame, 3);
//
//     auto *fireFrame = new QFrame(this);
//     fireFrame->setObjectName(u"fireFrame"_s);
//     auto *fireLayout = new QVBoxLayout(fireFrame);
//     fireLayout->setSpacing(10);
//
//     auto *fireTitle = new QLabel(u"Fire Ignitor"_s, fireFrame);
//     fireTitle->setObjectName(u"fireTitle"_s);
//     fireLayout->addWidget(fireTitle);
//
//     auto *selector = new QComboBox(fireFrame);
//     selector->setObjectName(u"deviceSelector"_s);
//     selector->addItem(u"No device selected"_s);
//     selector->setEnabled(false);
//     fireLayout->addWidget(selector);
//
//     auto *indicatorStack = new QFrame(fireFrame);
//     indicatorStack->setObjectName(u"indicatorStack"_s);
//     auto *indicatorLayout = new QHBoxLayout(indicatorStack);
//     indicatorLayout->setSpacing(12);
//     auto *batteryA = new QLabel(u"IGN-A"_s, indicatorStack);
//     auto *batteryB = new QLabel(u"IGN-B"_s, indicatorStack);
//     indicatorLayout->addWidget(batteryA);
//     indicatorLayout->addWidget(batteryB);
//     fireLayout->addWidget(indicatorStack);
//
//     auto *igniteRow = new QHBoxLayout();
//     igniteRow->setSpacing(12);
//     auto *igniteButton = new QPushButton(u"FIRE\nIGNITE"_s, fireFrame);
//     igniteButton->setObjectName(u"igniteButton"_s);
//     igniteButton->setFixedSize(120, 120);
//     igniteButton->setEnabled(false);
//     igniteRow->addWidget(igniteButton, 0, Qt::AlignCenter);
//
//     auto *timerButton = new QPushButton(u"TIMER"_s, fireFrame);
//     timerButton->setObjectName(u"timerButton"_s);
//     timerButton->setFixedSize(70, 70);
//     timerButton->setEnabled(false);
//     igniteRow->addWidget(timerButton, 0, Qt::AlignBottom);
//     igniteRow->addStretch(1);
//     fireLayout->addLayout(igniteRow);
//
//     rightColumn->addWidget(fireFrame, 2);
//
//     contentLayout->addLayout(rightColumn, 1);
// }