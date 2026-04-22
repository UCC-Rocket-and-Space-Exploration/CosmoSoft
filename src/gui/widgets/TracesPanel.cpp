#include "gui/widgets/TracesPanel.h"

#include "gui/Theme.h"
#include "gui/widgets/MetricDefs.h"

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStyle>
#include <QVBoxLayout>
#include <QWidget>

using namespace Qt::StringLiterals;

namespace {

/**
 * Row frame: swatch and value labels use WA_TransparentForMouseEvents so clicks
 * land here; toggles the checkbox unless the click is on the checkbox itself.
 */
class TraceRowFrame : public QFrame {
    QCheckBox *m_cb = nullptr;

public:
    explicit TraceRowFrame(QWidget *parent = nullptr)
        : QFrame(parent)
    {
    }

    void setToggleCheckBox(QCheckBox *cb) { m_cb = cb; }

protected:
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && m_cb && m_cb->isVisible() && m_cb->isEnabled()) {
            const QPoint inCb = m_cb->mapFrom(this, event->pos());
            if (!m_cb->rect().contains(inCb))
                m_cb->toggle();
        }
        QFrame::mouseReleaseEvent(event);
    }
};

} // namespace

// ── TracesPanel ──────────────────────────────────────────────────────────────

TracesPanel::TracesPanel(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(u"tracesPanel"_s);
    setMinimumWidth(130);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    setStyleSheet(
        QString(uR"(
        QFrame#tracesPanel {
            background-color: rgba(21, 22, 25, 0.88);
            border: 1px solid %1;
            border-radius: %2px;
        }
        QLabel#tracesPanelTitle {
            font-size: 10px;
            font-family: %3;
            color: #7a8898;
            letter-spacing: 1.5px;
        }
        QPushButton#tracesAllNoneBtn {
            font-size: 10px;
            font-family: %3;
            color: #7a8898;
            background: transparent;
            border: 1px solid %1;
            border-radius: %4px;
            padding: 3px 8px;
            min-height: 22px;
        }
        QPushButton#tracesAllNoneBtn:hover { color: #c8d4e0; border-color: #6a7080; background: rgba(255,255,255,0.04); }
        QFrame#traceSeparator { background-color: %1; border: none; }
        QFrame#traceRow {
            background-color: transparent;
            border: none;
            border-radius: %4px;
        }
        QFrame#traceRow:hover { background-color: rgba(255,255,255,0.04); }
        QFrame#traceRow[noData="true"] QCheckBox#traceCheck { color: #555a66; }
        QFrame#traceRow[noData="true"] QLabel#traceValueLabel { color: #555a66; }
        QCheckBox#traceCheck {
            font-size: %5px;
            font-family: %3;
            color: #c8d4e0;
            spacing: 0px;
        }
        QCheckBox#traceCheck::indicator {
            width: 0px;
            height: 0px;
            border: none;
            margin: 0px;
            padding: 0px;
        }
        QLabel#traceValueLabel {
            font-size: 10px;
            font-family: %3;
            color: %6;
        }
        QScrollArea#traceScroll { background: transparent; border: none; }
        QWidget#traceScrollInner { background: transparent; }
    )"_s)
            .arg(Theme::kBorderPanel)
            .arg(Theme::kRadiusMd)
            .arg(Theme::kFontMono)
            .arg(Theme::kRadiusSm)
            .arg(Theme::kFontSizeSm)
            .arg(Theme::kTextMuted));

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(6, 6, 6, 6);
    outer->setSpacing(4);

    // ── Header: "TRACES" title + ALL / NONE buttons ──────────────────────────
    auto *headerRow = new QHBoxLayout();
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->setSpacing(4);

    auto *title = new QLabel(u"TRACES"_s, this);
    title->setObjectName(u"tracesPanelTitle"_s);
    title->setToolTip(
        u"Toggle which metrics appear on the chart.\n"
        u"When replaying a log, only fields present in that file are listed.\n"
        u"2+ traces: Y axis is normalized (0–1).\n"
        u"Click the row (not only the box) to toggle."_s);
    headerRow->addWidget(title, 1, Qt::AlignVCenter);

    auto *allBtn = new QPushButton(u"ALL"_s, this);
    allBtn->setObjectName(u"tracesAllNoneBtn"_s);
    allBtn->setToolTip(u"Enable all traces"_s);

    auto *noneBtn = new QPushButton(u"NONE"_s, this);
    noneBtn->setObjectName(u"tracesAllNoneBtn"_s);
    noneBtn->setToolTip(u"Disable all traces (keeps one minimum)"_s);

    headerRow->addWidget(allBtn,  0, Qt::AlignVCenter);
    headerRow->addWidget(noneBtn, 0, Qt::AlignVCenter);
    outer->addLayout(headerRow);

    connect(allBtn,  &QPushButton::clicked, this, &TracesPanel::onSelectAllTraces);
    connect(noneBtn, &QPushButton::clicked, this, &TracesPanel::onSelectNoneTraces);

    // ── Scroll area containing the per-metric rows ────────────────────────────
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(u"traceScroll"_s);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setMinimumHeight(80);
    scroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *inner = new QWidget(scroll);
    inner->setObjectName(u"traceScrollInner"_s);
    auto *listLay = new QVBoxLayout(inner);
    listLay->setContentsMargins(0, 0, 2, 0);
    listLay->setSpacing(2);

    for (int i = 0; i < kMetricCount; ++i) {
        if (i == kSecondaryGroupFirst) {
            auto *sep = new QFrame(inner);
            sep->setObjectName(u"traceSeparator"_s);
            sep->setFrameShape(QFrame::HLine);
            sep->setFixedHeight(1);
            listLay->addWidget(sep);
            listLay->addSpacing(2);
            m_secondaryGroupSeparator = sep;
        }

        auto *row = new TraceRowFrame(inner);
        row->setObjectName(u"traceRow"_s);
        row->setProperty("noData", false);
        auto *rowLay = new QHBoxLayout(row);
        rowLay->setContentsMargins(0, 1, 0, 1);
        rowLay->setSpacing(5);

        const QColor col = MetricDefs::metricColor(i);
        auto *swatch = new QLabel(row);
        swatch->setFixedSize(20, 20);
        swatch->setAttribute(Qt::WA_TransparentForMouseEvents);
        swatch->setStyleSheet(
            QStringLiteral("QLabel { background-color: %1; border-radius: 2px; min-width:14px; min-height:14px; }")
                .arg(col.name(QColor::HexRgb)));
        m_traceSwatches[static_cast<std::size_t>(i)] = swatch;

        auto *cb = new QCheckBox(MetricDefs::metricTraceShortName(i), row);
        cb->setObjectName(u"traceCheck"_s);
        cb->setToolTip(QStringLiteral("%1 — click anywhere on the row to toggle")
                           .arg(MetricDefs::metricTitle(i)));
        m_metricChecks[static_cast<std::size_t>(i)] = cb;

        auto *valLabel = new QLabel(u"—"_s, row);
        valLabel->setObjectName(u"traceValueLabel"_s);
        valLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        valLabel->setMinimumWidth(52);
        valLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_traceValueLabels[static_cast<std::size_t>(i)] = valLabel;

        rowLay->addWidget(swatch,   0, Qt::AlignVCenter);
        rowLay->addWidget(cb,       1, Qt::AlignVCenter);
        rowLay->addWidget(valLabel, 0, Qt::AlignVCenter);
        listLay->addWidget(row);
        m_traceRows[static_cast<std::size_t>(i)] = row;

        static_cast<TraceRowFrame *>(row)->setToggleCheckBox(cb);
        row->setCursor(Qt::PointingHandCursor);
        row->setAttribute(Qt::WA_Hover);

        connect(cb, &QCheckBox::toggled, this, &TracesPanel::onAnyMetricToggled);
    }

    scroll->setWidget(inner);
    outer->addWidget(scroll, 1);

    // Default: first three metrics enabled.
    m_metricEnabled = {true, true, true, false, false, false, false, false, false};
    syncCheckboxStatesFromFlags();
}

std::array<bool, TracesPanel::kMetricCount> TracesPanel::enabledMetrics() const
{
    return m_metricEnabled;
}

void TracesPanel::setMetricsOffered(const std::array<bool, kMetricCount> &offered)
{
    for (int i = 0; i < kMetricCount; ++i) {
        if (m_metricChecks[static_cast<std::size_t>(i)]) {
            m_metricEnabled[static_cast<std::size_t>(i)] =
                m_metricChecks[static_cast<std::size_t>(i)]->isChecked();
        }
    }
    const std::array<bool, kMetricCount> before = m_metricEnabled;

    for (int i = 0; i < kMetricCount; ++i) {
        auto *row = m_traceRows[static_cast<std::size_t>(i)];
        if (!row) {
            continue;
        }
        const bool show = offered[static_cast<std::size_t>(i)];
        row->setVisible(show);
        if (!show) {
            if (auto *cb = m_metricChecks[static_cast<std::size_t>(i)]) {
                const QSignalBlocker b(cb);
                cb->setChecked(false);
            }
            m_metricEnabled[static_cast<std::size_t>(i)] = false;
        }
    }

    updateSecondaryGroupSeparatorVisibility();
    ensureAtLeastOneMetricEnabled();
    refreshSwatchStates();

    if (before != m_metricEnabled) {
        emit enabledMetricsChanged(m_metricEnabled);
    }
}

void TracesPanel::setMetricDataStates(const std::array<bool, kMetricCount> &hasData)
{
    for (int i = 0; i < kMetricCount; ++i) {
        auto *row = m_traceRows[static_cast<std::size_t>(i)];
        if (!row || !row->isVisible()) {
            continue;
        }
        const bool noData = !hasData[static_cast<std::size_t>(i)];
        if (row->property("noData").toBool() != noData) {
            row->setProperty("noData", noData);
            row->style()->unpolish(row);
            row->style()->polish(row);
            const auto kids = row->findChildren<QWidget *>();
            for (auto *child : kids) {
                child->style()->unpolish(child);
                child->style()->polish(child);
            }
        }
    }
}

void TracesPanel::updateLiveValues(const FlightSample &sample)
{
    for (int i = 0; i < kMetricCount; ++i) {
        auto *lbl = m_traceValueLabels[static_cast<std::size_t>(i)];
        if (!lbl) continue;
        const double v    = MetricDefs::sampleValueForMetric(sample, i);
        const QString val = MetricDefs::formatMetricValuePretty(i, v);
        const QString unit = MetricDefs::metricAxisUnitShort(i);
        lbl->setText(unit.isEmpty() ? val : val + u' ' + unit);
    }
}

// ── Private slots ────────────────────────────────────────────────────────────

void TracesPanel::onAnyMetricToggled()
{
    for (int i = 0; i < kMetricCount; ++i) {
        if (m_metricChecks[static_cast<std::size_t>(i)])
            m_metricEnabled[static_cast<std::size_t>(i)] = m_metricChecks[static_cast<std::size_t>(i)]->isChecked();
    }
    ensureAtLeastOneMetricEnabled();
    refreshSwatchStates();
    emit enabledMetricsChanged(m_metricEnabled);
}

void TracesPanel::onSelectAllTraces()
{
    for (int i = 0; i < kMetricCount; ++i) {
        auto *row = m_traceRows[static_cast<std::size_t>(i)];
        auto *cb = m_metricChecks[static_cast<std::size_t>(i)];
        if (cb && row && row->isVisible()) {
            cb->setChecked(true);
        }
    }
}

void TracesPanel::onSelectNoneTraces()
{
    int keeper = -1;
    for (int i = 0; i < kMetricCount; ++i) {
        auto *row = m_traceRows[static_cast<std::size_t>(i)];
        if (!row || !row->isVisible()) {
            continue;
        }
        if (m_metricEnabled[static_cast<std::size_t>(i)]) {
            keeper = i;
            break;
        }
    }
    if (keeper < 0) {
        for (int i = 0; i < kMetricCount; ++i) {
            auto *row = m_traceRows[static_cast<std::size_t>(i)];
            if (row && row->isVisible()) {
                keeper = i;
                break;
            }
        }
    }
    for (int i = 0; i < kMetricCount; ++i) {
        auto *row = m_traceRows[static_cast<std::size_t>(i)];
        auto *cb = m_metricChecks[static_cast<std::size_t>(i)];
        if (cb && row && row->isVisible()) {
            cb->setChecked(i == keeper);
        }
    }
}

// ── Private helpers ───────────────────────────────────────────────────────────

void TracesPanel::syncCheckboxStatesFromFlags()
{
    for (int i = 0; i < kMetricCount; ++i) {
        if (m_metricChecks[static_cast<std::size_t>(i)]) {
            const QSignalBlocker b(m_metricChecks[static_cast<std::size_t>(i)]);
            m_metricChecks[static_cast<std::size_t>(i)]->setChecked(m_metricEnabled[static_cast<std::size_t>(i)]);
        }
    }
    refreshSwatchStates();
}

void TracesPanel::ensureAtLeastOneMetricEnabled()
{
    int n = 0;
    int firstVisible = -1;
    for (int i = 0; i < kMetricCount; ++i) {
        auto *row = m_traceRows[static_cast<std::size_t>(i)];
        if (!row || !row->isVisible()) {
            continue;
        }
        if (firstVisible < 0) {
            firstVisible = i;
        }
        if (m_metricEnabled[static_cast<std::size_t>(i)]) {
            ++n;
        }
    }
    if (n == 0 && firstVisible >= 0) {
        m_metricEnabled[static_cast<std::size_t>(firstVisible)] = true;
        syncCheckboxStatesFromFlags();
    }
}

void TracesPanel::updateSecondaryGroupSeparatorVisibility()
{
    if (!m_secondaryGroupSeparator) {
        return;
    }
    bool anyPrimary = false;
    bool anySecondary = false;
    for (int i = 0; i < kSecondaryGroupFirst; ++i) {
        auto *row = m_traceRows[static_cast<std::size_t>(i)];
        if (row && row->isVisible()) {
            anyPrimary = true;
            break;
        }
    }
    for (int i = kSecondaryGroupFirst; i < kMetricCount; ++i) {
        auto *row = m_traceRows[static_cast<std::size_t>(i)];
        if (row && row->isVisible()) {
            anySecondary = true;
            break;
        }
    }
    m_secondaryGroupSeparator->setVisible(anyPrimary && anySecondary);
}

void TracesPanel::refreshSwatchStates()
{
    for (int i = 0; i < kMetricCount; ++i) {
        auto *swatch = m_traceSwatches[static_cast<std::size_t>(i)];
        if (!swatch) continue;
        const bool enabled = m_metricEnabled[static_cast<std::size_t>(i)];
        const QColor col = enabled ? MetricDefs::metricColor(i) : QColor(u"#2e3138"_s);
        swatch->setStyleSheet(
            QStringLiteral("QLabel { background-color: %1; border-radius: 2px; min-width:6px; min-height:14px; }")
                .arg(col.name(QColor::HexRgb)));
    }
}
