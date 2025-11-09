#include "pages/ChartPage.h"

#include <QLabel>
#include <QVBoxLayout>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QtMath>
#include <QPainter>

using namespace Qt::StringLiterals;

ChartPage::ChartPage(QWidget *parent)
        : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(8);

    auto *heading = new QLabel(u"Telemetry Chart"_s, this);
    heading->setStyleSheet(u"font-size: 20px; font-weight: bold;"_s);
    layout->addWidget(heading);

    auto *description = new QLabel(
            u"This demo plots a synthetic sine wave that mimics smooth altitude oscillations.\n"
            u"Replace the generated points with live telemetry when the backend is connected."_s,
            this);
    description->setWordWrap(true);
    layout->addWidget(description);

    auto *series = new QLineSeries(this);
    series->setName(u"Sample Telemetry"_s);
    for (int degrees = 0; degrees <= 360; degrees += 30) {
        const double radians = qDegreesToRadians(static_cast<double>(degrees));
        series->append(degrees, std::sin(radians));
    }

    auto *chart = new QChart();
    chart->addSeries(series);
    chart->setTitle(u"Flight Altitude Trend (placeholder data)"_s);

    auto *axisX = new QValueAxis();
    axisX->setTitleText(u"Sample (degrees placeholder)"_s);
    axisX->setTickCount(series->count());
    chart->addAxis(axisX, Qt::AlignBottom);
    series->attachAxis(axisX);

    auto *axisY = new QValueAxis();
    axisY->setTitleText(u"Altitude (normalized)"_s);
    axisY->setRange(-1.1, 1.1);
    chart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisY);

    chart->legend()->setVisible(true);
    chart->legend()->setAlignment(Qt::AlignBottom);

    auto *chartView = new QChartView(chart, this);
    chartView->setRenderHint(QPainter::Antialiasing);
    layout->addWidget(chartView);

    auto *tips = new QLabel(
            u"Tip: add additional series (RSSI, temp) and sync their axes for richer visuals."_s,
            this);
    tips->setWordWrap(true);
    tips->setStyleSheet(u"color: #52606d; font-style: italic;"_s);
    layout->addWidget(tips);

    layout->addStretch(1);
}
