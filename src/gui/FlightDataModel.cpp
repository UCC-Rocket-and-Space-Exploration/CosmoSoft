#include "gui/FlightDataModel.h"

#include <limits>

FlightDataModel::FlightDataModel(QObject *parent)
    : QObject(parent) {
    qRegisterMetaType<FlightSampleBatch>("FlightSampleBatch");
}

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
    emit displayedSampleChanged(sample);
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
    appendLiveBatch(FlightSampleBatch{sample});
}

void FlightDataModel::appendLiveBatch(const FlightSampleBatch &samples) {
    if (samples.isEmpty()) {
        return;
    }

    const FlightSample &latest = samples.constLast();
    {
        QMutexLocker lock(&m_mutex);
        m_latest = latest;
    }

    emit liveSamplesReceived(samples);
    emit displayedSampleChanged(latest);
    for (const auto &sample : samples) {
        emit sampleUpdated(sample);
    }
}

void FlightDataModel::addBytesReceived(qint64 byteCount) {
    if (byteCount <= 0) {
        return;
    }
    qint64 total = 0;
    {
        QMutexLocker lock(&m_mutex);
        const qint64 maximum = std::numeric_limits<qint64>::max();
        m_bytesReceived = byteCount > maximum - m_bytesReceived
            ? maximum
            : m_bytesReceived + byteCount;
        total = m_bytesReceived;
    }
    emit bytesReceivedChanged(total);
}
