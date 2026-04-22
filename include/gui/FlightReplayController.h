/**
 * @file FlightReplayController.h
 * @brief Timer-driven replay controller for a loaded FlightSession.
 *
 * The controller advances through the session sample-by-sample using a
 * QTimer in single-shot mode.  The delay between ticks is derived from the
 * delta between consecutive sample timestamps and is divided by the playback
 * speed multiplier so that slow-motion and fast-forward work transparently.
 *
 * Ownership model:
 *  - MainWindow owns the controller and the loaded FlightSession.
 *  - DashboardPage's ReplayBar calls play/pause/stop/setPosition.
 *  - MainWindow and DashboardPage both subscribe to positionChanged().
 */

#ifndef COSMO_SOFT_FLIGHTREPLAYCONTROLLER_H
#define COSMO_SOFT_FLIGHTREPLAYCONTROLLER_H

#include <QObject>

#include "domain/FlightSample.h"
#include "domain/FlightSession.h"

class QTimer;

/**
 * @class FlightReplayController
 * @brief Drives sample-accurate replay of a FlightSession at a configurable speed.
 */
class FlightReplayController : public QObject {
    Q_OBJECT

public:
    explicit FlightReplayController(QObject *parent = nullptr);

    /**
     * @brief Loads @p session and resets the playback position to the start.
     *
     * Any in-progress playback is paused before the session is replaced.
     */
    void setSession(FlightSession session);

    /** @brief Returns a const reference to the currently loaded session. */
    [[nodiscard]] const FlightSession &session() const { return m_session; }

    /** @brief Returns true when a non-empty session is loaded. */
    [[nodiscard]] bool hasSession() const { return !m_session.samples.empty(); }

    /** @brief Returns the current playback index (number of samples in the visible trail). */
    [[nodiscard]] int index() const { return m_index; }

    /** @brief Returns the total number of samples in the loaded session. */
    [[nodiscard]] int sampleCount() const { return static_cast<int>(m_session.samples.size()); }

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

    /** @brief Emitted when the last sample is passed and playback ends naturally. */
    void playbackFinished();

    /**
     * @brief Emitted on every tick and on any manual seek.
     * @param trailLength Current number of samples in the visible trail (0..N).
     */
    void positionChanged(int trailLength);

    /** @brief Emitted when an operation fails (e.g. play() called with no session). */
    void errorOccurred(const QString &message);

private slots:
    void onTimerTick();

private:
    FlightSession m_session;
    QTimer *m_timer  = nullptr;
    int     m_index  = 0;
    double  m_speed  = 1.0;
    bool    m_playing = false;
};

#endif
