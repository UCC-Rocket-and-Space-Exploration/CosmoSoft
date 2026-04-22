#include "gui/pages/MapPage.h"

#include "gui/FlightDataModel.h"
#include "gui/FlightReplayController.h"

#include <QChart>
#include <QChartView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineSeries>
#include <QPainter>
#include <QPushButton>
#include <QSizePolicy>
#include <QValueAxis>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;

namespace {

constexpr double kDefaultLon = 0.0;
constexpr double kDefaultLat = 0.0;
constexpr double kDefaultSpan = 0.01;

bool isValidCoord(double lat, double lon) {
    return std::isfinite(lat) && std::isfinite(lon) &&
           !(std::abs(lat) < 1e-9 && std::abs(lon) < 1e-9);
}

} // namespace

MapPage::MapPage(FlightDataModel *model,
                 FlightReplayController *replay,
                 QWidget *parent)
    : QWidget(parent),
      m_model(model),
      m_replay(replay) {
    setObjectName(u"mapPage"_s);

    m_chart = new QChart();
    m_chart->setTitle(u""_s);
    m_chart->legend()->hide();
    m_chart->setMargins(QMargins(8, 8, 8, 8));

    m_series = new QLineSeries(m_chart);
    m_series->setName(u"Flight path"_s);

    m_axisX = new QValueAxis(m_chart);
    m_axisX->setTitleText(u"Longitude (°)"_s);
    m_axisX->setLabelFormat(u"%.5f"_s);
    m_axisX->setRange(kDefaultLon - kDefaultSpan, kDefaultLon + kDefaultSpan);

    m_axisY = new QValueAxis(m_chart);
    m_axisY->setTitleText(u"Latitude (°)"_s);
    m_axisY->setLabelFormat(u"%.5f"_s);
    m_axisY->setRange(kDefaultLat - kDefaultSpan, kDefaultLat + kDefaultSpan);

    m_chart->addSeries(m_series);
    m_chart->addAxis(m_axisX, Qt::AlignBottom);
    m_chart->addAxis(m_axisY, Qt::AlignLeft);
    m_series->attachAxis(m_axisX);
    m_series->attachAxis(m_axisY);

    applyChartTheme();

    m_chartView = new QChartView(m_chart, this);
    m_chartView->setRenderHint(QPainter::Antialiasing);
    m_chartView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // ── Toolbar ──────────────────────────────────────────────────────────────
    auto *toolbar = new QWidget(this);
    toolbar->setObjectName(u"mapToolbar"_s);
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(12, 6, 12, 6);
    toolbarLayout->setSpacing(8);

    m_fitBtn = new QPushButton(u"Fit"_s, toolbar);
    m_fitBtn->setToolTip(u"Fit axes to the full flight path."_s);

    auto *hintLabel = new QLabel(u"Longitude → X axis · Latitude → Y axis"_s, toolbar);
    hintLabel->setObjectName(u"mapHintLabel"_s);

    toolbarLayout->addWidget(m_fitBtn);
    toolbarLayout->addSpacing(8);
    toolbarLayout->addWidget(hintLabel);
    toolbarLayout->addStretch(1);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(toolbar);
    root->addWidget(m_chartView, 1);

    setStyleSheet(uR"(
        #mapPage {
            background-color: #1e1e20;
        }
        #mapToolbar {
            background-color: #242426;
            border-bottom: 1px solid rgba(255, 255, 255, 0.06);
        }
        #mapHintLabel {
            color: #8a9ab0;
            font-size: 11px;
            font-family: "Red Hat Mono", "Courier New", monospace;
        }
        QPushButton {
            border: 1px solid #5a5a5a;
            border-radius: 4px;
            padding: 3px 10px;
            min-height: 24px;
            background-color: #2e2e32;
            color: #d8d8d8;
            font-size: 11px;
            font-family: "Red Hat Mono", "Courier New", monospace;
        }
        QPushButton:hover  { background-color: #3a3a3e; }
        QPushButton:pressed { background-color: #222224; }
    )"_s);

    if (m_model) {
        connect(m_model, &FlightDataModel::sampleUpdated,
                this, &MapPage::onSampleUpdated);
        connect(m_model, &FlightDataModel::sessionReset,
                this, &MapPage::onSessionReset);
    }
    connect(m_fitBtn, &QPushButton::clicked, this, &MapPage::onFitAxes);
}

void MapPage::setReplaySession(const FlightSession *session) {
    m_session = session;
    m_liveSamples.clear();
    if (!session || session->samples.empty()) {
        m_series->clear();
        m_axisX->setRange(kDefaultLon - kDefaultSpan, kDefaultLon + kDefaultSpan);
        m_axisY->setRange(kDefaultLat - kDefaultSpan, kDefaultLat + kDefaultSpan);
        return;
    }
    rebuildSeries(static_cast<int>(session->samples.size()));
}

void MapPage::setReplayTrailLength(int trailLength) {
    if (!m_session) {
        return;
    }
    rebuildSeries(trailLength);
}

void MapPage::onSampleUpdated(const FlightSample &sample) {
    if (m_session) {
        return;
    }
    m_liveSamples.push_back(sample);
    if (!isValidCoord(sample.coordinates.latitude, sample.coordinates.longitude)) {
        return;
    }
    const double lon = sample.coordinates.longitude;
    const double lat = sample.coordinates.latitude;
    m_series->append(lon, lat);

    const auto &pts = m_series->points();
    if (pts.isEmpty()) {
        return;
    }
    const double xMin = m_axisX->min();
    const double xMax = m_axisX->max();
    const double yMin = m_axisY->min();
    const double yMax = m_axisY->max();
    if (lon < xMin || lon > xMax || lat < yMin || lat > yMax) {
        fitAxesToData();
    }
}

void MapPage::onSessionReset() {
    m_session = nullptr;
    m_liveSamples.clear();
    m_series->clear();
    m_axisX->setRange(kDefaultLon - kDefaultSpan, kDefaultLon + kDefaultSpan);
    m_axisY->setRange(kDefaultLat - kDefaultSpan, kDefaultLat + kDefaultSpan);
}

void MapPage::onFitAxes() {
    fitAxesToData();
}

void MapPage::rebuildSeries(int trailLength) {
    if (!m_session) {
        return;
    }
    const int n = std::min(trailLength, static_cast<int>(m_session->samples.size()));
    QList<QPointF> pts;
    pts.reserve(n);
    for (int i = 0; i < n; ++i) {
        const auto &s = m_session->samples[static_cast<std::size_t>(i)];
        if (isValidCoord(s.coordinates.latitude, s.coordinates.longitude)) {
            pts.append(QPointF(s.coordinates.longitude, s.coordinates.latitude));
        }
    }
    m_series->replace(pts);
    if (!pts.isEmpty()) {
        fitAxesToData();
    }
}

void MapPage::fitAxesToData() {
    const auto &pts = m_series->points();
    if (pts.isEmpty()) {
        m_axisX->setRange(kDefaultLon - kDefaultSpan, kDefaultLon + kDefaultSpan);
        m_axisY->setRange(kDefaultLat - kDefaultSpan, kDefaultLat + kDefaultSpan);
        return;
    }

    double xMin = std::numeric_limits<double>::max();
    double xMax = std::numeric_limits<double>::lowest();
    double yMin = std::numeric_limits<double>::max();
    double yMax = std::numeric_limits<double>::lowest();

    for (const auto &p : pts) {
        xMin = std::min(xMin, p.x());
        xMax = std::max(xMax, p.x());
        yMin = std::min(yMin, p.y());
        yMax = std::max(yMax, p.y());
    }

    const double xPad = std::max((xMax - xMin) * 0.1, kDefaultSpan * 0.5);
    const double yPad = std::max((yMax - yMin) * 0.1, kDefaultSpan * 0.5);

    m_axisX->setRange(xMin - xPad, xMax + xPad);
    m_axisY->setRange(yMin - yPad, yMax + yPad);
}

void MapPage::applyChartTheme() {
    if (!m_chart) {
        return;
    }
    m_chart->setTheme(QChart::ChartThemeDark);
    m_chart->setBackgroundBrush(QBrush(QColor(0x1e, 0x1e, 0x20)));
    m_chart->setPlotAreaBackgroundBrush(QBrush(QColor(0x16, 0x16, 0x18)));
    m_chart->setPlotAreaBackgroundVisible(true);

    QPen seriesPen(QColor(0x4f, 0xa5, 0xde));
    seriesPen.setWidth(2);
    m_series->setPen(seriesPen);
}
