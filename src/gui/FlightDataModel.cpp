#include "gui/FlightDataModel.h"

FlightDataModel::FlightDataModel(QObject *parent)
    : QObject(parent) {}

FlightSample FlightDataModel::latestSample() const {
    QMutexLocker lock(&m_mutex);
    return m_latest;
}

qint64 FlightDataModel::totalBytesReceived() const {
    QMutexLocker lock(&m_mutex);
    return m_bytesReceived;
}

bool FlightDataModel::replayMode() const {
    QMutexLocker lock(&m_mutex);
    return m_replayMode;
}

void FlightDataModel::setReplayMode(bool on) {
    bool changed = false;
    {
        QMutexLocker lock(&m_mutex);
        if (m_replayMode != on) {
            m_replayMode = on;
            changed = true;
        }
    }
    if (changed) {
        emit replayModeChanged(on);
    }
}

void FlightDataModel::resetSession() {
    {
        QMutexLocker lock(&m_mutex);
        m_latest = FlightSample{};
    }
    emit sessionReset();
}

void FlightDataModel::setDisplayedSample(const FlightSample &sample) {
    {
        QMutexLocker lock(&m_mutex);
        m_latest = sample;
    }
    emit sampleUpdated(sample);
}

void FlightDataModel::resetByteCounter() {
    {
        QMutexLocker lock(&m_mutex);
        m_bytesReceived = 0;
    }
    emit bytesReceivedChanged(0);
}

void FlightDataModel::appendSample(const FlightSample &sample) {
    setDisplayedSample(sample);
}

void FlightDataModel::addBytesReceived(qint64 byteCount) {
    if (byteCount <= 0) {
        return;
    }
    qint64 total = 0;
    {
        QMutexLocker lock(&m_mutex);
        m_bytesReceived += byteCount;
        total = m_bytesReceived;
    }
    emit bytesReceivedChanged(total);
}
