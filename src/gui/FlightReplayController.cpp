#include "gui/FlightReplayController.h"

#include <QTimer>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <utility>

namespace {

[[nodiscard]] const FlightSession &emptySession()
{
    static const FlightSession session;
    return session;
}

[[nodiscard]] FlightReplayController::ClockSource defaultClockSource()
{
    return [] {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch());
        return static_cast<qint64>(elapsed.count());
    };
}

[[nodiscard]] double normalizedSpeed(const double multiplier)
{
    if (multiplier <= 0.0 || !std::isfinite(multiplier)) {
        return 1.0;
    }
    return std::clamp(multiplier, 0.25, 4.0);
}

} // namespace

FlightReplayController::FlightReplayController(QObject *parent)
    : FlightReplayController(ClockSource{}, parent) {}

FlightReplayController::FlightReplayController(
    ClockSource clockSource,
    QObject *parent)
    : QObject(parent),
      m_clockSource(clockSource ? std::move(clockSource) : defaultClockSource()),
      m_timer(new QTimer(this)) {
    m_timer->setInterval(kUiTickIntervalMs);
    m_timer->setSingleShot(false);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &FlightReplayController::onTimerTick);
}

void FlightReplayController::setSession(
    std::shared_ptr<const FlightSession> session,
    std::shared_ptr<const cosmo::preview::FlightPreviewCache> preview) {
    haltPlayback(true);
    const bool hasSamples = session && !session->samples.empty();
    const bool previewMatchesSession = hasSamples
        && preview
        && preview->wasBuiltFor(*session)
        && preview->displaySeconds().size() == session->samples.size();

    if (hasSamples && !previewMatchesSession) {
        m_session.reset();
        m_preview.reset();
        m_playbackTimeMs = 0.0;
        m_anchorPlaybackTimeMs = 0.0;
        m_anchorClockMs = 0;
        updatePosition(0, true);
        emit errorOccurred(tr("The flight preview is missing or belongs to another session."));
        return;
    }

    m_session = std::move(session);
    m_preview.reset();
    if (previewMatchesSession) {
        m_preview = std::move(preview);
    }

    m_playbackTimeMs = 0.0;
    m_anchorPlaybackTimeMs = 0.0;
    m_anchorClockMs = 0;
    updatePosition(0, true);
}

const FlightSession &FlightReplayController::session() const {
    return m_session ? *m_session : emptySession();
}

int FlightReplayController::sampleCount() const {
    if (!m_session) {
        return 0;
    }
    const std::size_t maximum = static_cast<std::size_t>(
        std::numeric_limits<int>::max());
    return static_cast<int>(std::min(m_session->samples.size(), maximum));
}

void FlightReplayController::setSpeed(double multiplier) {
    const double nextSpeed = normalizedSpeed(multiplier);
    if (nextSpeed == m_speed) {
        return;
    }

    if (!m_playing) {
        m_speed = nextSpeed;
        return;
    }

    const qint64 clockMilliseconds = currentClockMilliseconds();
    synchronizePlayback(clockMilliseconds);
    m_speed = nextSpeed;
    if (m_playing) {
        anchorPlayback(clockMilliseconds);
    }
}

void FlightReplayController::play() {
    const int count = sampleCount();
    if (count <= 0) {
        emit errorOccurred(tr("No flight loaded."));
        return;
    }
    if (m_playing) {
        return;
    }

    if (m_index < 0 || m_index >= count) {
        m_playbackTimeMs = 0.0;
        updatePosition(0);
    }

    const qint64 clockMilliseconds = currentClockMilliseconds();
    m_playing = true;
    anchorPlayback(clockMilliseconds);
    emit playbackStarted();
    synchronizePlayback(clockMilliseconds);
    if (m_playing) {
        m_timer->start();
    }
}

void FlightReplayController::pause() {
    if (!m_playing) {
        return;
    }

    synchronizePlayback(currentClockMilliseconds());
    if (!m_playing) {
        return;
    }

    m_playing = false;
    m_timer->stop();
    emit playbackPaused();
}

void FlightReplayController::stop() {
    haltPlayback(false);
    m_playbackTimeMs = 0.0;
    m_anchorPlaybackTimeMs = 0.0;
    m_anchorClockMs = 0;
    updatePosition(0);
    emit playbackStopped();
}

void FlightReplayController::setPosition(int trailLength) {
    const int count = sampleCount();
    if (count <= 0) {
        return;
    }

    haltPlayback(true);
    const int position = std::clamp(trailLength, 0, count);
    m_playbackTimeMs = playbackTimeForPosition(position);
    m_anchorPlaybackTimeMs = m_playbackTimeMs;
    m_anchorClockMs = 0;
    updatePosition(position);
}

void FlightReplayController::onTimerTick() {
    if (!m_playing || sampleCount() <= 0) {
        return;
    }
    synchronizePlayback(currentClockMilliseconds());
}

qint64 FlightReplayController::currentClockMilliseconds() const {
    return m_clockSource();
}

double FlightReplayController::durationMilliseconds() const {
    const int count = sampleCount();
    if (!m_preview || count <= 0
        || m_preview->displaySeconds().size() < static_cast<std::size_t>(count)) {
        return 0.0;
    }

    const double duration = m_preview->displaySecondAt(count - 1) * 1000.0;
    return std::isfinite(duration) && duration > 0.0 ? duration : 0.0;
}

double FlightReplayController::playbackTimeForPosition(const int trailLength) const {
    const int count = sampleCount();
    if (!m_preview || trailLength <= 0 || count <= 0) {
        return 0.0;
    }
    if (trailLength >= count) {
        return durationMilliseconds();
    }

    const double time = m_preview->displaySecondAt(trailLength - 1) * 1000.0;
    if (!std::isfinite(time) || time <= 0.0) {
        return 0.0;
    }
    return std::min(time, durationMilliseconds());
}

double FlightReplayController::playbackTimeAt(const qint64 clockMilliseconds) const {
    if (!m_playing) {
        return m_playbackTimeMs;
    }

    const long double elapsedClockMilliseconds = std::max(
        0.0L,
        static_cast<long double>(clockMilliseconds)
            - static_cast<long double>(m_anchorClockMs));
    const long double candidate = static_cast<long double>(m_anchorPlaybackTimeMs)
        + elapsedClockMilliseconds * static_cast<long double>(m_speed);
    const long double duration = static_cast<long double>(durationMilliseconds());
    return static_cast<double>(std::clamp(candidate, 0.0L, duration));
}

int FlightReplayController::positionForPlaybackTime(
    const double playbackTimeMilliseconds) const {
    const int count = sampleCount();
    if (!m_preview || count <= 0) {
        return 0;
    }

    const auto &timeline = m_preview->displaySeconds();
    const auto end = timeline.cbegin() + count;
    const double comparisonTime = std::nextafter(
        std::max(0.0, playbackTimeMilliseconds),
        std::numeric_limits<double>::infinity());
    const auto upper = std::upper_bound(
        timeline.cbegin(),
        end,
        comparisonTime,
        [](const double playbackMilliseconds, const double sampleSeconds) {
            return playbackMilliseconds < sampleSeconds * 1000.0;
        });
    return static_cast<int>(std::distance(timeline.cbegin(), upper));
}

void FlightReplayController::anchorPlayback(const qint64 clockMilliseconds) {
    m_anchorClockMs = clockMilliseconds;
    m_anchorPlaybackTimeMs = m_playbackTimeMs;
}

void FlightReplayController::synchronizePlayback(const qint64 clockMilliseconds) {
    if (!m_playing) {
        return;
    }

    m_playbackTimeMs = std::max(
        m_playbackTimeMs,
        playbackTimeAt(clockMilliseconds));
    const int nextPosition = std::max(
        m_index,
        positionForPlaybackTime(m_playbackTimeMs));
    updatePosition(nextPosition);

    const int count = sampleCount();
    if (m_playbackTimeMs >= durationMilliseconds()) {
        updatePosition(count);
        m_playing = false;
        m_timer->stop();
        m_anchorPlaybackTimeMs = m_playbackTimeMs;
        m_anchorClockMs = clockMilliseconds;
        emit playbackFinished();
    }
}

void FlightReplayController::haltPlayback(const bool emitPausedSignal) {
    const bool wasPlaying = m_playing;
    m_playing = false;
    m_timer->stop();
    if (wasPlaying && emitPausedSignal) {
        emit playbackPaused();
    }
}

void FlightReplayController::updatePosition(
    const int trailLength,
    const bool forceSignal) {
    const int position = std::clamp(trailLength, 0, sampleCount());
    if (!forceSignal && position == m_index) {
        return;
    }
    m_index = position;
    emit positionChanged(m_index);
}
