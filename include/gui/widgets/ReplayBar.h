/**
 * @file ReplayBar.h
 * @brief Transport-bar widget that controls and displays replay playback state.
 *
 * The bar self-wires to FlightReplayController and FlightDataModel signals so
 * its labels and button states stay current automatically.  The only external
 * calls DashboardPage needs to make are:
 *
 *  - setSession(session)     — call after loading a new log file
 *  - setTrailLength(n)       — call from applyReplayControllerPosition to keep
 *                              the scrubber in sync without triggering a signal loop
 *  - setLiveSampleCount(n)   — call from onSampleUpdated / onSessionReset so
 *                              the "N samples buffered" caption stays current
 *
 * trailLengthChanged(int) is emitted whenever the scrubber or step buttons
 * move so DashboardPage can trigger chart rebuilds.
 */

#pragma once

#include <QFrame>
#include <QString>

class QComboBox;
class QLabel;
class QSlider;
class QToolButton;

class FlightDataModel;
class FlightReplayController;
struct FlightSession;

class ReplayBar : public QFrame {
    Q_OBJECT

public:
    explicit ReplayBar(FlightReplayController *replay,
                       FlightDataModel        *model,
                       QWidget                *parent = nullptr);

    /**
     * Resets the scrubber range and initial position for @p session.
     * Pass nullptr to return to the "no file" state.
     */
    void setSession(const FlightSession *session);

    /**
     * Updates the scrubber position without emitting trailLengthChanged.
     * Called by DashboardPage after it has already handled a position change.
     */
    void setTrailLength(int trailLength);

    /**
     * Updates the live-sample count shown in the caption when not in replay mode.
     */
    void setLiveSampleCount(int count);

signals:
    /** Emitted when the slider or step buttons change the replay position. */
    void trailLengthChanged(int trailLength);

private slots:
    void onPlaybackStarted();
    void onPlaybackPaused();
    void onPlaybackStopped();
    void onPlaybackFinished();
    void onErrorOccurred(const QString &msg);
    void onSpeedComboChanged(int index);
    void onReplayModeChanged(bool replayMode);

private:
    void buildUi();
    void updateLabels();
    void syncTransportChrome();

    FlightReplayController *m_replay = nullptr;
    FlightDataModel        *m_model  = nullptr;
    const FlightSession    *m_session = nullptr;
    int  m_liveSampleCount    = 0;
    int  m_lastTrailLength    = 0;
    QString m_replayActivityText;

    // Transport widgets
    QToolButton *m_playPauseBtn        = nullptr;
    QToolButton *m_stopBtn             = nullptr;
    QToolButton *m_jumpStartBtn        = nullptr;
    QToolButton *m_jumpEndBtn          = nullptr;
    QToolButton *m_stepBackBtn         = nullptr;
    QToolButton *m_stepFwdBtn          = nullptr;
    QSlider     *m_replaySlider        = nullptr;
    QLabel      *m_replayBarTitle      = nullptr;
    QLabel      *m_replaySampleCaption = nullptr;
    QLabel      *m_replayTimeLeftLabel = nullptr;
    QLabel      *m_replayTimeRightLabel = nullptr;
    QComboBox   *m_speedCombo          = nullptr;
};
