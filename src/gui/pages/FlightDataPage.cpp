#include "pages/FlightDataPage.h"

#include <QAbstractItemView>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

FlightDataPage::FlightDataPage(QWidget *parent)
        : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 32, 32, 32);
    layout->setSpacing(20);

    auto *heading = new QLabel(u"Flight Data"_s, this);
    heading->setStyleSheet(u"font-size: 26px; font-weight: bold;"_s);
    layout->addWidget(heading);

    auto *intro = new QLabel(
            u"Review downlinked telemetry. The widgets below are placeholders until the real feed is connected."_s,
            this);
    intro->setWordWrap(true);
    intro->setStyleSheet(u"color: #4a5568;"_s);
    layout->addWidget(intro);

    auto *summary = new QFrame(this);
    summary->setObjectName(u"flightSummary"_s);
    summary->setStyleSheet(uR"(
        QFrame#flightSummary {
            background: #f7fafc;
            border: 1px solid #d1d9e6;
            border-radius: 10px;
            padding: 24px;
        }
        QLabel[data-role="metricLabel"] {
            font-size: 13px;
            color: #4a5568;
        }
        QLabel[data-role="metricValue"] {
            font-size: 20px;
            font-weight: 600;
        }
    )"_s);
    auto *summaryGrid = new QGridLayout(summary);
    summaryGrid->setHorizontalSpacing(32);
    summaryGrid->setVerticalSpacing(12);

    const struct MetricRow {
        QString label;
        QString value;
    } metrics[] = {
            {u"Apogee (est)"_s, u"---- m"_s},
            {u"Velocity"_s, u"--.- m/s"_s},
            {u"Temperature"_s, u"--.- °C"_s},
            {u"Battery"_s, u"-- %"_s},
    };

    for (int i = 0; i < 4; ++i) {
        auto *label = new QLabel(metrics[i].label, summary);
        label->setProperty("data-role", "metricLabel");
        auto *value = new QLabel(metrics[i].value, summary);
        value->setProperty("data-role", "metricValue");
        summaryGrid->addWidget(label, i / 2 * 2, i % 2);
        summaryGrid->addWidget(value, i / 2 * 2 + 1, i % 2);
    }

    layout->addWidget(summary);

    auto *tableHeading = new QLabel(u"Recent Telemetry Frames"_s, this);
    tableHeading->setStyleSheet(u"font-size: 18px; font-weight: 600;"_s);
    layout->addWidget(tableHeading);

    auto *table = new QTableWidget(5, 4, this);
    table->setHorizontalHeaderLabels({u"Timestamp"_s, u"Event"_s, u"Value"_s, u"Notes"_s});
    table->horizontalHeader()->setStretchLastSection(true);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setFocusPolicy(Qt::NoFocus);
    table->setAlternatingRowColors(true);
    for (int row = 0; row < table->rowCount(); ++row) {
        for (int col = 0; col < table->columnCount(); ++col) {
            table->setItem(row, col, new QTableWidgetItem(u"--"_s));
        }
    }
    layout->addWidget(table);

    layout->addStretch(1);
}
