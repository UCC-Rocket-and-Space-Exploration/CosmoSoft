#ifndef COSMO_SOFT_FLIGHTDATAMODEL_H
#define COSMO_SOFT_FLIGHTDATAMODEL_H

#include <QMutex>
#include <QObject>

#include "domain/FlightSample.h"

class FlightDataModel : public QObject {
    Q_OBJECT

public:
    explicit FlightDataModel(QObject *parent = nullptr);

    FlightSample latestSample() const;
    qint64 totalBytesReceived() const;

    bool replayMode() const;
    void setReplayMode(bool on);

    /** Clears latest sample to defaults and notifies charts to reset (does not change byte counters). */
    void resetSession();

    /** Zero live byte counter (e.g. after disconnect). */
    void resetByteCounter();

    /** Updates latest sample for monitoring text without changing replay chart logic. */
    void setDisplayedSample(const FlightSample &sample);

public slots:
    void appendSample(FlightSample sample);
    void addBytesReceived(qint64 byteCount);

signals:
    void sampleUpdated(const FlightSample &sample);
    void bytesReceivedChanged(qint64 totalBytes);
    void sessionReset();
    void replayModeChanged(bool replay);

private:
    mutable QMutex m_mutex;
    FlightSample m_latest;
    qint64 m_bytesReceived = 0;
    bool m_replayMode = false;
};

#endif // COSMO_SOFT_FLIGHTDATAMODEL_H
