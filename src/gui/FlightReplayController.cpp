#include "gui/FlightReplayController.h"

#include <QTimer>

#include <algorithm>
#include <cmath>

FlightReplayController::FlightReplayController(QObject *parent)
    : QObject(parent),
      m_timer(new QTimer(this)) {
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &FlightReplayController::onTimerTick);
}

void FlightReplayController::setSession(FlightSession session) {
    pause();
    m_session = std::move(session);
    m_index = 0;
    emit positionChanged(m_index);
}

void FlightReplayController::setSpeed(double multiplier) {
    if (multiplier <= 0.0 || !std::isfinite(multiplier)) {
        m_speed = 1.0;
    } else {
        m_speed = std::clamp(multiplier, 0.25, 4.0);
    }
}

void FlightReplayController::play() {
    if (m_session.samples.empty()) {
        emit errorOccurred(tr("No flight loaded."));
        return;
    }
    const int n = static_cast<int>(m_session.samples.size());
    if (m_index > n) {
        m_index = 0;
    }
    if (m_index == n) {
        m_index = 0;
        emit positionChanged(m_index);
    }
    m_playing = true;
    emit playbackStarted();
    m_timer->start(0);
}

void FlightReplayController::pause() {
    const bool wasPlaying = m_playing;
    m_playing = false;
    m_timer->stop();
    if (wasPlaying) {
        emit playbackPaused();
    }
}

void FlightReplayController::stop() {
    m_playing = false;
    m_timer->stop();
    m_index = 0;
    emit positionChanged(m_index);
    emit playbackStopped();
}

void FlightReplayController::setPosition(int trailLength) {
    if (m_session.samples.empty()) {
        return;
    }
    pause();
    const int n = static_cast<int>(m_session.samples.size());
    m_index = std::clamp(trailLength, 0, n);
    emit positionChanged(m_index);
}

void FlightReplayController::onTimerTick() {
    if (!m_playing || m_session.samples.empty()) {
        return;
    }

    const int n = static_cast<int>(m_session.samples.size());
    if (m_index < 0 || m_index >= n) {
        m_playing = false;
        emit playbackFinished();
        return;
    }

    ++m_index;
    emit positionChanged(m_index);

    if (m_index >= n) {
        m_playing = false;
        emit playbackFinished();
        return;
    }

    const long tPrev = m_session.samples[static_cast<std::size_t>(m_index - 1)].timestamp;
    const long tCur = m_session.samples[static_cast<std::size_t>(m_index)].timestamp;
    long dt = tCur - tPrev;
    if (dt < 0) {
        dt = 0;
    }
    int ms = static_cast<int>(std::lround(static_cast<double>(dt) / m_speed));
    if (ms < 1) {
        ms = 1;
    }
    m_timer->start(ms);
}
