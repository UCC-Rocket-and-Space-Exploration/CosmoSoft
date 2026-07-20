/**
 * @file FlightReplayController.h
 * @brief Timer-driven replay controller for a loaded FlightSession.
 *
 * The controller samples a monotonic playback clock on a fixed UI cadence.
 * Each tick maps elapsed playback time onto the corrected display timeline in
 * FlightPreviewCache, so dense logs never create one timer event per sample.
 *
 * Ownership model:
 *  - MainWindow owns the controller and the loaded FlightSession.
 *  - DashboardPage's ReplayBar calls play/pause/stop/setPosition.
 *  - MainWindow and DashboardPage both subscribe to positionChanged().
 */

#ifndef COSMO_SOFT_FLIGHTREPLAYCONTROLLER_H
#define COSMO_SOFT_FLIGHTREPLAYCONTROLLER_H

#include <QObject>

#include <functional>
#include <memory>

#include "domain/FlightSample.h"
#include "domain/FlightSession.h"
#include "services/preview/FlightPreviewCache.h"

class QTimer;

/**
 * @class FlightReplayController
 * @brief Drives time-accurate, UI-bounded replay of a FlightSession.
 */
class FlightReplayController : public QObject {
    Q_OBJECT

public:
    /** @brief Fixed cadence used to update replay consumers. */
    static constexpr int kUiTickIntervalMs = 33;

    /** @brief Monotonic millisecond clock used to anchor playback time. */
    using ClockSource = std::function<qint64()>;

    /** @brief Construct a controller using the process monotonic clock. */
    explicit FlightReplayController(QObject *parent = nullptr);

    /**
     * @brief Construct a controller with an injected monotonic clock.
     * @param clockSource Callback returning monotonic milliseconds.
     * @param parent QObject owner, or nullptr.
     *
     * This additive overload supports deterministic hosts and tests. An empty
     * callback falls back to the process monotonic clock.
     */
    explicit FlightReplayController(ClockSource clockSource,
                                    QObject *parent);

    /**
     * @brief Loads @p session and resets the playback position to the start.
     * @param session Immutable sample storage to replay.
     * @param preview Corrected display timeline built for @p session.
     *
     * Any in-progress playback is paused before the session is replaced.
     * A missing, size-mismatched, or cross-session preview rejects the load
     * without doing expensive preprocessing on the controller's thread.
     */
    void setSession(std::shared_ptr<const FlightSession> session,
                    std::shared_ptr<const cosmo::preview::FlightPreviewCache> preview);

    /** @brief Returns a const reference to the currently loaded session. */
    [[nodiscard]] const FlightSession &session() const;

    /** @brief Returns a shared pointer to the currently loaded session, or null. */
    [[nodiscard]] std::shared_ptr<const FlightSession> sessionPtr() const { return m_session; }

    /** @brief Returns true when a non-empty session is loaded. */
    [[nodiscard]] bool hasSession() const { return m_session && !m_session->samples.empty(); }

    /** @brief Returns the current playback index (number of samples in the visible trail). */
    [[nodiscard]] int index() const { return m_index; }

    /** @brief Returns the UI-addressable number of samples in the loaded session. */
    [[nodiscard]] int sampleCount() const;

    /**
     * @brief Sets the playback speed multiplier.
     * @param multiplier Clamped to [0.25, 4.0]; invalid values reset to 1.0.
     */
    void setSpeed(double multiplier);

    /** @brief Returns the current speed multiplier. */
    [[nodiscard]] double speed() const { return m_speed; }

    /** @brief Returns true while playback is running. */
    [[nodiscard]] bool isPlaying() const { return m_playing; }

public slots:
    /** @brief Starts or resumes playback from the current index. */
    void play();

    /** @brief Pauses playback without resetting the index. */
    void pause();

    /** @brief Stops playback and resets the index to zero. */
    void stop();

    /**
     * @brief Seeks to @p trailLength and pauses.
     * @param trailLength Number of samples to include in the visible trail (0 = start, N = end).
     */
    void setPosition(int trailLength);

signals:
    /** @brief Emitted when playback starts or resumes. */
    void playbackStarted();

    /** @brief Emitted when playback is paused (without position reset). */
    void playbackPaused();

    /** @brief Emitted when playback is stopped and position reset to zero. */
    void playbackStopped();

    /** @brief Emitted when playback time reaches the last sample. */
    void playbackFinished();

    /**
     * @brief Emitted when the visible trail changes or a new session resets it.
     * @param trailLength Current number of samples in the visible trail (0..N).
     *
     * Timer ticks that still map to the same trail length are coalesced.
     */
    void positionChanged(int trailLength);

    /** @brief Emitted when an operation fails (e.g. play() called with no session). */
    void errorOccurred(const QString &message);

private slots:
    void onTimerTick();

private:
    [[nodiscard]] qint64 currentClockMilliseconds() const;
    [[nodiscard]] double durationMilliseconds() const;
    [[nodiscard]] double playbackTimeForPosition(int trailLength) const;
    [[nodiscard]] double playbackTimeAt(qint64 clockMilliseconds) const;
    [[nodiscard]] int positionForPlaybackTime(double playbackTimeMilliseconds) const;
    void anchorPlayback(qint64 clockMilliseconds);
    void synchronizePlayback(qint64 clockMilliseconds);
    void haltPlayback(bool emitPausedSignal);
    void updatePosition(int trailLength, bool forceSignal = false);

    std::shared_ptr<const FlightSession> m_session;
    std::shared_ptr<const cosmo::preview::FlightPreviewCache> m_preview;
    ClockSource m_clockSource;
    QTimer *m_timer = nullptr;
    int m_index = 0;
    double m_speed = 1.0;
    double m_playbackTimeMs = 0.0;
    double m_anchorPlaybackTimeMs = 0.0;
    qint64 m_anchorClockMs = 0;
    bool m_playing = false;
};

#endif // COSMO_SOFT_FLIGHTREPLAYCONTROLLER_H
