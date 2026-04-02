#ifndef COSMO_SOFT_FLIGHTREPLAYCONTROLLER_H
#define COSMO_SOFT_FLIGHTREPLAYCONTROLLER_H

#include <QObject>

#include "domain/FlightSample.h"
#include "domain/FlightSession.h"

class QTimer;

class FlightReplayController : public QObject {
    Q_OBJECT

public:
    explicit FlightReplayController(QObject *parent = nullptr);

    void setSession(FlightSession session);
    [[nodiscard]] const FlightSession &session() const { return m_session; }
    [[nodiscard]] bool hasSession() const { return !m_session.samples.empty(); }
    [[nodiscard]] int index() const { return m_index; }
    [[nodiscard]] int sampleCount() const { return static_cast<int>(m_session.samples.size()); }

    void setSpeed(double multiplier);
    [[nodiscard]] double speed() const { return m_speed; }
    [[nodiscard]] bool isPlaying() const { return m_playing; }

public slots:
    void play();
    void pause();
    void stop();
    /** Set visible trail length and pause (0 = empty, N = full flight). */
    void setPosition(int trailLength);

signals:
    void playbackStarted();
    void playbackPaused();
    void playbackStopped();
    void playbackFinished();
    /** Number of samples in the visible trail (0..N). Next sample to play is at index `count` when count < N. */
    void positionChanged(int trailLength);
    void errorOccurred(const QString &message);

private slots:
    void onTimerTick();

private:
    void scheduleNextTick();
    int clampIndex(int i) const;

    FlightSession m_session;
    QTimer *m_timer = nullptr;
    int m_index = 0;
    double m_speed = 1.0;
    bool m_playing = false;
};

#endif
