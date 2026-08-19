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
#include <QVector>

#include "domain/FlightSample.h"

Q_DECLARE_METATYPE(FlightSample)

/** @brief Qt-compatible batch of live flight samples. */
using FlightSampleBatch = QVector<FlightSample>;

Q_DECLARE_METATYPE(FlightSampleBatch)

/**
 * @class FlightDataModel
 * @brief Centralised, thread-safe store for the latest flight sample and session metadata.
 *
 * GUI pages subscribe to displayedSampleChanged() for the latest values and
 * liveSamplesReceived() when they need every point in a live batch.
 * The model itself never performs any I/O or parsing.
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
    /** @brief Stores one live sample and emits the live, display, and legacy signals. */
    void appendSample(const FlightSample &sample);

    /**
     * @brief Stores a batch of live samples and coalesces display notifications.
     *
     * The complete batch is emitted once through liveSamplesReceived(). The
     * final sample becomes latestSample() and is emitted once through
     * displayedSampleChanged(). The legacy sampleUpdated() signal remains
     * per-sample for compatibility with existing consumers.
     * Empty batches are ignored. Must be called on the GUI thread.
     */
    void appendLiveBatch(const FlightSampleBatch &samples);

    /** @brief Accumulates a positive @p byteCount and emits bytesReceivedChanged(). */
    void addBytesReceived(qint64 byteCount);

signals:
    /** @brief Emitted once for every new sample; carries that sample. */
    void sampleUpdated(const FlightSample &sample);

    /**
     * @brief Emitted once when the sample shown by monitoring widgets changes.
     *
     * New display-only consumers should prefer this signal to sampleUpdated().
     */
    void displayedSampleChanged(const FlightSample &sample);

    /**
     * @brief Emitted once for each accepted batch of live telemetry samples.
     *
     * Consumers that need every live point should use this signal and must not
     * also subscribe to sampleUpdated(), which is retained for compatibility.
     */
    void liveSamplesReceived(const FlightSampleBatch &samples);

    /** @brief Emitted whenever the cumulative byte counter changes. */
    void bytesReceivedChanged(qint64 totalBytes);

    /** @brief Emitted by resetSession(); pages should clear their displayed data. */
    void sessionReset();

    /** @brief Emitted when the replay / live mode flag changes. */
    void replayModeChanged(bool replay);

private:
    // Defensive: all current access is GUI-thread-only via QueuedConnection,
    // but the mutex guards against future direct cross-thread reads.
    mutable QMutex m_mutex;
    FlightSample m_latest;
    qint64 m_bytesReceived = 0;
    bool m_replayMode = false;
};

#endif // COSMO_SOFT_FLIGHTDATAMODEL_H
