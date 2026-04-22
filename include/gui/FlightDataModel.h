/**
 * @file FlightDataModel.h
 * @brief Thread-safe Qt model that holds the current telemetry state for the UI.
 *
 * All pages read from this model rather than from raw data structures.  Worker
 * threads must use Qt::QueuedConnection when invoking the appendSample and
 * addBytesReceived slots so that mutations happen on the GUI thread.
 */

#ifndef COSMO_SOFT_FLIGHTDATAMODEL_H
#define COSMO_SOFT_FLIGHTDATAMODEL_H

#include <QMutex>
#include <QObject>

#include "domain/FlightSample.h"

/**
 * @class FlightDataModel
 * @brief Centralised, thread-safe store for the latest flight sample and session metadata.
 *
 * MonitoringPage and DashboardPage subscribe to sampleUpdated() to refresh
 * their displays.  The model itself never performs any I/O or parsing.
 */
class FlightDataModel : public QObject {
    Q_OBJECT

public:
    explicit FlightDataModel(QObject *parent = nullptr);

    /** @brief Returns a copy of the most recently stored sample (mutex-protected). */
    FlightSample latestSample() const;

    /** @brief Returns the cumulative byte count received from the serial port. */
    qint64 totalBytesReceived() const;

    /** @brief Returns true when the model is operating in replay mode. */
    bool replayMode() const;

    /** @brief Switches between live and replay mode; emits replayModeChanged(). */
    void setReplayMode(bool on);

    /** Clears latest sample to defaults and notifies charts to reset (does not change byte counters). */
    void resetSession();

    /** Zero live byte counter (e.g. after disconnect). */
    void resetByteCounter();

    /** Updates latest sample for monitoring text without changing replay chart logic. */
    void setDisplayedSample(const FlightSample &sample);

public slots:
    /** @brief Stores @p sample as the latest and emits sampleUpdated(). Must be called on the GUI thread. */
    void appendSample(FlightSample sample);

    /** @brief Accumulates @p byteCount into totalBytesReceived() and emits bytesReceivedChanged(). */
    void addBytesReceived(qint64 byteCount);

signals:
    /** @brief Emitted after every new sample is stored; carries the new sample. */
    void sampleUpdated(const FlightSample &sample);

    /** @brief Emitted whenever the cumulative byte counter changes. */
    void bytesReceivedChanged(qint64 totalBytes);

    /** @brief Emitted by resetSession(); pages should clear their displayed data. */
    void sessionReset();

    /** @brief Emitted when the replay / live mode flag changes. */
    void replayModeChanged(bool replay);

private:
    mutable QMutex m_mutex;
    FlightSample m_latest;
    qint64 m_bytesReceived = 0;
    bool m_replayMode = false;
};

#endif // COSMO_SOFT_FLIGHTDATAMODEL_H
