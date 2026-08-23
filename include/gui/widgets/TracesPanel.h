/**
 * @file TracesPanel.h
 * @brief Side-panel widget that lets the user toggle which telemetry metrics
 *        appear on the chart, shows a live value readout for each metric, and
 *        dims rows that have no data yet.
 *
 * Public interface:
 *  - enabledMetrics()         → current bool array (at least one entry is true)
 *  - setMetricsOffered()      → hide rows for metrics not present in the loaded source
 *  - setMetricDataStates()    → dims rows that have no points in the current chart build
 *  - updateLiveValues(sample) → refreshes the right-aligned readout labels
 *
 * The panel emits enabledMetricsChanged whenever a checkbox changes so that
 * DashboardPage can trigger a chart rebuild.
 */

#pragma once

#include <QFrame>
#include <array>

#include "domain/FlightSample.h"

class QCheckBox;
class QLabel;

class TracesPanel : public QFrame {
    Q_OBJECT

public:
    /** Must match MetricDefs::kMetricCount and DashboardPage::kMetricCount. */
    static constexpr int kMetricCount = 9;

    explicit TracesPanel(QWidget *parent = nullptr);

    /** Returns the current enabled state; always has at least one true entry. */
    [[nodiscard]] std::array<bool, kMetricCount> enabledMetrics() const;

    /**
     * Shows or hides each trace row (e.g. CSV columns missing from the file).
     * Hidden metrics are unchecked and omitted from ALL/NONE.
     */
    void setMetricsOffered(const std::array<bool, kMetricCount> &offered);

    /**
     * Updates the dim / undim state of each visible trace row.
     * Pass true for each metric that currently has data points on the chart.
     */
    void setMetricDataStates(const std::array<bool, kMetricCount> &hasData);

    /**
     * @brief Selects metric or imperial presentation units.
     *
     * Incoming FlightSample values remain SI; only labels and formatted
     * readouts are converted.
     */
    void setImperialUnits(bool imperial);

signals:
    /** Emitted whenever any checkbox changes. The array always has at least one true. */
    void enabledMetricsChanged(std::array<bool, kMetricCount> enabled);

public slots:
    /** Refreshes the right-aligned value labels from the latest sample. */
    void updateLiveValues(const FlightSample &sample);

private slots:
    void applyThemeStyleSheet();
    void onAnyMetricToggled();
    void onSelectAllTraces();
    void onSelectNoneTraces();

private:
    void syncCheckboxStatesFromFlags();
    void ensureAtLeastOneMetricEnabled();
    void refreshSwatchStates();
    void updateSecondaryGroupSeparatorVisibility();

    std::array<QCheckBox *, kMetricCount> m_metricChecks{};
    std::array<QLabel *,    kMetricCount> m_traceValueLabels{};
    std::array<QLabel *,    kMetricCount> m_traceSwatches{};
    std::array<QFrame *,    kMetricCount> m_traceRows{};
    std::array<bool,        kMetricCount> m_metricEnabled{};
    std::array<QString,     kMetricCount> m_lastValueText{}; ///< Cached display text; avoids redundant setText calls.
    FlightSample m_latestSample;
    bool m_haveLatestSample = false;
    bool m_imperialUnits = false;
    QFrame *m_secondaryGroupSeparator = nullptr;
    /** Row index where the secondary group starts (after separator). */
    static constexpr int kSecondaryGroupFirst = 6;
};
