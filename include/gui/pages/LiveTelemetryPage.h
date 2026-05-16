/**
 * @file LiveTelemetryPage.h
 * @brief Placeholder page for the upcoming live-telemetry view.
 *
 * This page will be populated once the backend team finishes assembling the
 * live data pipeline.  Until then it shows a simple "coming soon" label.
 */

#ifndef COSMO_SOFT_LIVETELEMETRYPAGE_H
#define COSMO_SOFT_LIVETELEMETRYPAGE_H

#include <QWidget>

class QLabel;

/**
 * @class LiveTelemetryPage
 * @brief Empty placeholder for the live-telemetry view (to be implemented).
 */
class LiveTelemetryPage : public QWidget {
    Q_OBJECT

public:
    explicit LiveTelemetryPage(QWidget *parent = nullptr);
    ~LiveTelemetryPage() override = default;

private slots:
    void refreshStyleSheet();

private:
    QLabel *m_placeholderLabel = nullptr;
};

#endif // COSMO_SOFT_LIVETELEMETRYPAGE_H
