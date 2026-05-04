#include "gui/pages/MonitoringPage.h"

#include "gui/FlightDataModel.h"
#include "gui/Theme.h"
#include "gui/ThemePainter.h"
#include "gui/widgets/MetricDefs.h"
#include "gui/widgets/StatTileWidget.h"

#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QPointF>
#include <QVBoxLayout>

#include <cmath>

using namespace Qt::StringLiterals;
using namespace MetricDefs;

MonitoringPage::MonitoringPage(FlightDataModel *model, QWidget *parent)
    : QWidget(parent),
      m_model(model)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAutoFillBackground(false);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 24, 24, 24);
    root->setSpacing(16);

    // ── Status label (shown while waiting for first sample) ──────────────────
    m_statusLabel = new QLabel(
        u"Waiting for telemetry data.\n\n"
        u"Connect a serial port using the controls above, or open a flight log via "
        u"File → Open log… to get started."_s,
        this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet(
        QString(uR"(
        QLabel {
            background-color: rgba(21, 22, 25, 0.80);
            border: 1px solid %1;
            border-radius: %2px;
            padding: 14px 16px;
            color: #e8e8e8;
            font-size: %3px;
        }
    )"_s)
            .arg(Theme::kBorderPanel())
            .arg(Theme::kRadiusMd)
            .arg(Theme::kFontSizeMd));
    root->addWidget(m_statusLabel);

    // ── Stat tile grid ───────────────────────────────────────────────────────
    auto *tilesFrame = new QFrame(this);
    tilesFrame->setObjectName(u"monitoringTilesFrame"_s);
    tilesFrame->setStyleSheet(uR"(
        QFrame#monitoringTilesFrame {
            background: transparent;
            border: none;
        }
    )"_s);

    auto *grid = new QGridLayout(tilesFrame);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(10);

    constexpr int kCols = 3;
    for (int i = 0; i < kTileCount; ++i) {
        auto *tile = new StatTileWidget(metricTraceShortName(i), {}, this);
        m_tiles[static_cast<std::size_t>(i)] = tile;
        grid->addWidget(tile, i / kCols, i % kCols);
    }

    root->addWidget(tilesFrame);
    root->addStretch(1);

    if (m_model) {
        connect(m_model, &FlightDataModel::sampleUpdated, this, &MonitoringPage::onSampleUpdated);
    }
}

void MonitoringPage::onSampleUpdated(const FlightSample &sample)
{
    if (m_statusLabel && m_statusLabel->isVisible())
        m_statusLabel->hide();

    for (int i = 0; i < kTileCount; ++i) {
        if (!m_tiles[static_cast<std::size_t>(i)]) continue;
        const double v = sampleValueForMetric(sample, i);
        const QString text = formatMetricValuePretty(i, v)
                             + (metricAxisUnitShort(i).isEmpty()
                                    ? QString{}
                                    : u" "_s + metricAxisUnitShort(i));
        m_tiles[static_cast<std::size_t>(i)]->setValue(text);
    }
}

void MonitoringPage::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setClipRegion(event->region());

    painter.fillRect(rect(), QColor(Theme::kBgBase()));
    cosmo::ThemePainter::paintBackground(painter, rect(), QStringLiteral("panel"));

    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(Theme::kBorderDefault()).lighter(120));

    constexpr int dotSpacing  = 28;
    constexpr qreal dotRadius = 1.5;
    const int offset          = dotSpacing / 2;

    for (int y = offset; y < height(); y += dotSpacing)
        for (int x = offset; x < width(); x += dotSpacing)
            painter.drawEllipse(QPointF(x, y), dotRadius, dotRadius);
}
