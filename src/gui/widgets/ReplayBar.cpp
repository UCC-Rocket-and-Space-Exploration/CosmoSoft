#include "gui/widgets/ReplayBar.h"

#include "gui/FlightDataModel.h"
#include "gui/FlightReplayController.h"
#include "gui/Theme.h"
#include "gui/ThemeManager.h"
#include "gui/widgets/MetricDefs.h"

#include "domain/FlightSession.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

// ── Construction ─────────────────────────────────────────────────────────────

ReplayBar::ReplayBar(FlightReplayController *replay,
                     FlightDataModel        *model,
                     QWidget                *parent)
    : QFrame(parent), m_replay(replay), m_model(model)
{
    buildUi();

    if (m_replay) {
        connect(m_playPauseBtn, &QToolButton::clicked, this, [this]() {
            if (!m_replay) return;
            if (m_replay->isPlaying()) m_replay->pause(); else m_replay->play();
        });
        connect(m_stopBtn, &QToolButton::clicked, m_replay, &FlightReplayController::stop);

        connect(m_replaySlider, &QSlider::valueChanged, this, [this](int v) {
            if (m_replay) m_replay->setPosition(v);
            emit trailLengthChanged(v);
        });

        connect(m_jumpStartBtn, &QToolButton::clicked, this, [this]() {
            if (m_replaySlider) m_replaySlider->setValue(0);
        });
        connect(m_jumpEndBtn, &QToolButton::clicked, this, [this]() {
            if (m_replaySlider) m_replaySlider->setValue(m_replaySlider->maximum());
        });
        connect(m_stepBackBtn, &QToolButton::clicked, this, [this]() {
            if (m_replaySlider) m_replaySlider->setValue(m_replaySlider->value() - 1);
        });
        connect(m_stepFwdBtn, &QToolButton::clicked, this, [this]() {
            if (m_replaySlider) m_replaySlider->setValue(m_replaySlider->value() + 1);
        });

        connect(m_replay, &FlightReplayController::positionChanged, this, [this](int len) {
            const QSignalBlocker b(m_replaySlider);
            m_replaySlider->setValue(len);
            m_lastTrailLength = len;
            updateLabels();
            emit trailLengthChanged(len);
        });

        connect(m_replay, &FlightReplayController::playbackStarted,  this, &ReplayBar::onPlaybackStarted);
        connect(m_replay, &FlightReplayController::playbackPaused,   this, &ReplayBar::onPlaybackPaused);
        connect(m_replay, &FlightReplayController::playbackStopped,  this, &ReplayBar::onPlaybackStopped);
        connect(m_replay, &FlightReplayController::playbackFinished, this, &ReplayBar::onPlaybackFinished);
        connect(m_replay, &FlightReplayController::errorOccurred,    this, &ReplayBar::onErrorOccurred);

        connect(m_speedCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &ReplayBar::onSpeedComboChanged);
    }

    if (m_model) {
        connect(m_model, &FlightDataModel::replayModeChanged,
                this, &ReplayBar::onReplayModeChanged);
    }

    // Keyboard shortcuts scoped to this widget
    auto *spaceShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
    spaceShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(spaceShortcut, &QShortcut::activated, this, [this]() {
        if (!m_replay || !m_playPauseBtn || !m_playPauseBtn->isEnabled()) return;
        if (m_replay->isPlaying()) m_replay->pause(); else m_replay->play();
    });

    auto *leftShortcut = new QShortcut(QKeySequence(Qt::Key_Left), this);
    leftShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(leftShortcut, &QShortcut::activated, this, [this]() {
        if (m_stepBackBtn && m_stepBackBtn->isEnabled() && m_replaySlider)
            m_replaySlider->setValue(m_replaySlider->value() - 1);
    });

    auto *rightShortcut = new QShortcut(QKeySequence(Qt::Key_Right), this);
    rightShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(rightShortcut, &QShortcut::activated, this, [this]() {
        if (m_stepFwdBtn && m_stepFwdBtn->isEnabled() && m_replaySlider)
            m_replaySlider->setValue(m_replaySlider->value() + 1);
    });

    auto *homeShortcut = new QShortcut(QKeySequence(Qt::Key_Home), this);
    homeShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(homeShortcut, &QShortcut::activated, this, [this]() {
        if (m_jumpStartBtn && m_jumpStartBtn->isEnabled())
            m_jumpStartBtn->click();
    });

    auto *endShortcut = new QShortcut(QKeySequence(Qt::Key_End), this);
    endShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(endShortcut, &QShortcut::activated, this, [this]() {
        if (m_jumpEndBtn && m_jumpEndBtn->isEnabled())
            m_jumpEndBtn->click();
    });

    m_replayActivityText = u"Ready"_s;
    syncTransportChrome();
    updateLabels();
}

// ── Theme ─────────────────────────────────────────────────────────────────────

void ReplayBar::applyThemeStyleSheet()
{
    const auto borderPanel = Theme::kBorderPanel();
    const auto textDim = Theme::kTextDim();
    const auto bgPanel = Theme::kBgPanel();
    const auto accent = Theme::kAccentLink();
    const auto textPri = Theme::kTextPrimary();
    const auto borderDef = Theme::kBorderDefault();
    const auto bgBtn = Theme::kBgButton();
    const auto btnHov = Theme::kBtnHover();
    const auto fontMono = QString::fromUtf8(Theme::kFontMono);

    setStyleSheet(
        QString(uR"(
        QFrame#replayBar {
            background-color: %1;
            border: 1px solid %2;
            border-radius: %3px;
        }
        QLabel#replayBarTitle {
            font-size: 10px;
            font-family: %4;
            color: %5;
            letter-spacing: 1.5px;
            font-weight: 700;
        }
        QLabel#replayRateLabel {
            font-size: 10px;
            font-family: %4;
            color: %6;
            letter-spacing: 0.5px;
        }
        QLabel#replaySampleCaption {
            font-size: 10px;
            font-family: %4;
            color: %6;
        }
        QLabel#replayClockLabel {
            font-size: %7px;
            font-family: %4;
            color: %8;
            letter-spacing: 0.5px;
            min-width: 36px;
        }
        QFrame#replayVDiv { background-color: %9; border: none; }
        QSlider#replayScrubSlider::groove:horizontal {
            height: 4px; background: %2; border-radius: 2px;
        }
        QSlider#replayScrubSlider::handle:horizontal {
            background: %5; width: 10px; height: 10px;
            margin: -3px 0; border-radius: 5px;
        }
        QSlider#replayScrubSlider::sub-page:horizontal { background: %5; border-radius: 2px; }
        QSlider#replayScrubSlider:disabled::handle:horizontal { background: %2; }
    )"_s)
            .arg(bgPanel)                   // %1
            .arg(borderPanel)               // %2
            .arg(Theme::kRadiusMd)          // %3
            .arg(fontMono)                  // %4
            .arg(accent)                    // %5
            .arg(textDim)                   // %6
            .arg(Theme::kFontSizeSm)        // %7
            .arg(textPri)                   // %8
            .arg(borderDef)                 // %9
    + QString(uR"(
        QToolButton#replayTransportBtn, QToolButton#replaySkipBtn {
            background: %1;
            border: 1px solid %2;
            border-radius: %3px;
            padding: 2px;
            min-width: 30px; max-width: 30px;
            min-height: 30px; max-height: 30px;
            color: %4;
        }
        QToolButton#replayTransportBtn:hover, QToolButton#replaySkipBtn:hover {
            background: %5; border-color: %6;
        }
        QToolButton#replayTransportBtn:disabled, QToolButton#replaySkipBtn:disabled {
            color: %7; border-color: %2;
        }
        QToolButton#replayPlayPauseBtn {
            background: %1;
            border: 1px solid %8;
            border-radius: %3px;
            padding: 2px;
            min-width: 30px; max-width: 30px;
            min-height: 30px; max-height: 30px;
            color: %4;
        }
        QToolButton#replayPlayPauseBtn:hover { background: %5; border-color: %8; }
        QToolButton#replayPlayPauseBtn:disabled { background: %1; border-color: %2; color: %7; }
        QComboBox#replaySpeedCombo {
            font-size: %9px;
            font-family: )"_s + fontMono + uR"(;
            font-weight: 700;
            background: %1;
            border: 1px solid %8;
            border-radius: 12px;
            padding: 3px 8px;
            color: %4;
            min-width: 52px;
        }
        QComboBox#replaySpeedCombo:hover {
            background: %5;
            border-color: %8;
        }
        QComboBox#replaySpeedCombo:disabled { color: %7; background: %1; border-color: %2; }
    )"_s)
            .arg(bgBtn)                     // %1
            .arg(borderPanel)               // %2
            .arg(Theme::kRadiusSm)          // %3
            .arg(textPri)                   // %4
            .arg(btnHov)                    // %5
            .arg(borderDef)                 // %6
            .arg(textDim)                   // %7
            .arg(accent)                    // %8
            .arg(Theme::kFontSizeSm));      // %9
}

// ── Public API ────────────────────────────────────────────────────────────────

void ReplayBar::setSession(const FlightSession *session)
{
    m_session = session;
    const int n = session ? static_cast<int>(session->samples.size()) : 0;

    if (m_replaySlider) {
        const QSignalBlocker b(m_replaySlider);
        m_replaySlider->setMaximum(std::max(0, n));
        m_replaySlider->setEnabled(n > 0);
        m_replaySlider->setValue(n > 0 ? n : 0);
    }

    // Sync speed combo to controller's current speed.
    if (m_replay && m_speedCombo) {
        const QSignalBlocker b(m_speedCombo);
        const double spd = m_replay->speed();
        int bestIdx = 0; double bestDiff = 1e9;
        for (int i = 0; i < m_speedCombo->count(); ++i) {
            const double v = m_speedCombo->itemData(i).toDouble();
            const double d = std::abs(v - spd);
            if (d < bestDiff) { bestDiff = d; bestIdx = i; }
        }
        m_speedCombo->setCurrentIndex(bestIdx);
        if (bestDiff > 0.01) m_replay->setSpeed(m_speedCombo->currentData().toDouble());
    }

    m_lastTrailLength = n;
    m_replayActivityText = n > 0 ? u"Ready"_s : u"Idle"_s;
    syncTransportChrome();
    updateLabels();
}

void ReplayBar::setTrailLength(int trailLength)
{
    if (m_replaySlider) {
        const QSignalBlocker b(m_replaySlider);
        m_replaySlider->setValue(trailLength);
    }
    m_lastTrailLength = trailLength;
    updateLabels();
}

void ReplayBar::setLiveSampleCount(int count)
{
    m_liveSampleCount = count;
    if (!m_model || !m_model->replayMode())
        updateLabels();
}

// ── Private slots ─────────────────────────────────────────────────────────────

void ReplayBar::onPlaybackStarted()
{
    m_replayActivityText = u"Playing"_s;
    syncTransportChrome();
    updateLabels();
}

void ReplayBar::onPlaybackPaused()
{
    m_replayActivityText = u"Paused"_s;
    syncTransportChrome();
    updateLabels();
}

void ReplayBar::onPlaybackStopped()
{
    m_replayActivityText = u"Stopped"_s;
    syncTransportChrome();
    updateLabels();
}

void ReplayBar::onPlaybackFinished()
{
    m_replayActivityText = u"Finished"_s;
    syncTransportChrome();
    updateLabels();
}

void ReplayBar::onErrorOccurred(const QString &msg)
{
    m_replayActivityText = msg;
    syncTransportChrome();
    updateLabels();
}

void ReplayBar::onSpeedComboChanged(int)
{
    if (!m_speedCombo || !m_replay) return;
    bool ok = false;
    const double v = m_speedCombo->currentData().toDouble(&ok);
    if (ok) m_replay->setSpeed(v);
    updateLabels();
}

void ReplayBar::onReplayModeChanged(bool)
{
    syncTransportChrome();
    updateLabels();
}

// ── Private helpers ───────────────────────────────────────────────────────────

void ReplayBar::buildUi()
{
    setObjectName(u"replayBar"_s);
    setFocusPolicy(Qt::ClickFocus);
    setToolTip(
        u"Replay controls · Space: play/pause · Left/Right: step frame · "
        u"Click here first to capture keys"_s);

    applyThemeStyleSheet();
    connect(&cosmo::ThemeManager::instance(), &cosmo::ThemeManager::themeChanged,
            this, &ReplayBar::applyThemeStyleSheet);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(10, 8, 10, 8);
    outer->setSpacing(4);

    // ── Row 1: transport buttons | slider | clock ─────────────────────────────
    auto *transportRow = new QHBoxLayout();
    transportRow->setSpacing(4);
    transportRow->setContentsMargins(0, 0, 0, 0);

    auto setupBtn = [](QToolButton *b, const QString &objName, QSize iconSz) {
        b->setObjectName(objName);
        b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        b->setAutoRaise(false);
        b->setFocusPolicy(Qt::StrongFocus);
        b->setIconSize(iconSz);
    };

    m_jumpStartBtn = new QToolButton(this);
    setupBtn(m_jumpStartBtn, u"replaySkipBtn"_s, QSize(16, 16));
    m_jumpStartBtn->setToolTip(u"Jump to start  [Home key]"_s);
    m_jumpStartBtn->setIcon(style()->standardIcon(QStyle::SP_MediaSkipBackward));

    m_stepBackBtn = new QToolButton(this);
    setupBtn(m_stepBackBtn, u"replaySkipBtn"_s, QSize(16, 16));
    m_stepBackBtn->setToolTip(u"Step back one sample  [← key]"_s);
    m_stepBackBtn->setIcon(style()->standardIcon(QStyle::SP_MediaSeekBackward));

    m_playPauseBtn = new QToolButton(this);
    m_playPauseBtn->setObjectName(u"replayPlayPauseBtn"_s);
    m_playPauseBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_playPauseBtn->setAutoRaise(false);
    m_playPauseBtn->setFocusPolicy(Qt::StrongFocus);
    m_playPauseBtn->setIconSize(QSize(16, 16));
    m_playPauseBtn->setToolTip(u"Play / Pause  [Space]"_s);
    m_playPauseBtn->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));

    m_stepFwdBtn = new QToolButton(this);
    setupBtn(m_stepFwdBtn, u"replaySkipBtn"_s, QSize(16, 16));
    m_stepFwdBtn->setToolTip(u"Step forward one sample  [→ key]"_s);
    m_stepFwdBtn->setIcon(style()->standardIcon(QStyle::SP_MediaSeekForward));

    m_jumpEndBtn = new QToolButton(this);
    setupBtn(m_jumpEndBtn, u"replaySkipBtn"_s, QSize(16, 16));
    m_jumpEndBtn->setToolTip(u"Jump to end  [End key]"_s);
    m_jumpEndBtn->setIcon(style()->standardIcon(QStyle::SP_MediaSkipForward));

    m_stopBtn = new QToolButton(this);
    setupBtn(m_stopBtn, u"replayTransportBtn"_s, QSize(16, 16));
    m_stopBtn->setToolTip(u"Stop replay and reset position"_s);
    m_stopBtn->setIcon(style()->standardIcon(QStyle::SP_MediaStop));

    auto *div = new QFrame(this);
    div->setObjectName(u"replayVDiv"_s);
    div->setFixedSize(1, 24);

    m_replaySlider = new QSlider(Qt::Horizontal, this);
    m_replaySlider->setObjectName(u"replayScrubSlider"_s);
    m_replaySlider->setRange(0, 0);
    m_replaySlider->setEnabled(false);
    m_replaySlider->setSingleStep(1);
    m_replaySlider->setPageStep(10);
    m_replaySlider->setTracking(true);
    m_replaySlider->setToolTip(u"Timeline — drag to scrub through the log"_s);

    m_replayTimeLeftLabel = new QLabel(u"—"_s, this);
    m_replayTimeLeftLabel->setObjectName(u"replayClockLabel"_s);
    m_replayTimeLeftLabel->setToolTip(u"Position / Duration"_s);

    auto *clockSep = new QLabel(u"/"_s, this);
    clockSep->setObjectName(u"replayClockLabel"_s);

    m_replayTimeRightLabel = new QLabel(u"—"_s, this);
    m_replayTimeRightLabel->setObjectName(u"replayClockLabel"_s);

    transportRow->addWidget(m_jumpStartBtn, 0, Qt::AlignVCenter);
    transportRow->addWidget(m_stepBackBtn,  0, Qt::AlignVCenter);
    transportRow->addWidget(m_playPauseBtn, 0, Qt::AlignVCenter);
    transportRow->addWidget(m_stepFwdBtn,   0, Qt::AlignVCenter);
    transportRow->addWidget(m_jumpEndBtn,   0, Qt::AlignVCenter);
    transportRow->addSpacing(8);
    transportRow->addWidget(div,            0, Qt::AlignVCenter);
    transportRow->addSpacing(8);
    transportRow->addWidget(m_stopBtn,      0, Qt::AlignVCenter);
    transportRow->addSpacing(8);
    m_speedCombo = new QComboBox(this);
    m_speedCombo->setObjectName(u"replaySpeedCombo"_s);
    m_speedCombo->setToolTip(u"Playback speed (1× = real-time)"_s);
    m_speedCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_speedCombo->setMaxVisibleItems(12);
    const QList<double> speedRates{0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 3.0, 4.0};
    for (double r : speedRates) {
        const QString label = (r == static_cast<int>(r))
            ? QStringLiteral("%1×").arg(static_cast<int>(r))
            : QStringLiteral("%1×").arg(r, 0, 'f', 2);
        m_speedCombo->addItem(label, r);
    }
    m_speedCombo->setCurrentIndex(3);
    transportRow->addWidget(m_speedCombo,   0, Qt::AlignVCenter);
    transportRow->addSpacing(8);
    transportRow->addWidget(m_replaySlider, 1, Qt::AlignVCenter);
    transportRow->addSpacing(8);
    transportRow->addWidget(m_replayTimeLeftLabel,  0, Qt::AlignVCenter);
    transportRow->addWidget(clockSep,               0, Qt::AlignVCenter);
    transportRow->addWidget(m_replayTimeRightLabel, 0, Qt::AlignVCenter);
    outer->addLayout(transportRow);

    // ── Row 2: title tag | caption | speed combo ──────────────────────────────
    auto *metaRow = new QHBoxLayout();
    metaRow->setSpacing(6);
    metaRow->setContentsMargins(0, 0, 0, 0);

    m_replayBarTitle = new QLabel(u"REPLAY"_s, this);
    m_replayBarTitle->setObjectName(u"replayBarTitle"_s);
    metaRow->addWidget(m_replayBarTitle, 0, Qt::AlignVCenter);

    auto *metaSep = new QLabel(u"·"_s, this);
    metaSep->setObjectName(u"replayRateLabel"_s);
    metaRow->addWidget(metaSep, 0, Qt::AlignVCenter);

    m_replaySampleCaption = new QLabel(this);
    m_replaySampleCaption->setObjectName(u"replaySampleCaption"_s);
    metaRow->addWidget(m_replaySampleCaption, 1, Qt::AlignVCenter);

    metaRow->addStretch(1);
    outer->addLayout(metaRow);
}

void ReplayBar::updateLabels()
{
    if (!m_model) {
        if (m_replaySampleCaption)  m_replaySampleCaption->setText(u""_s);
        if (m_replayTimeLeftLabel)  m_replayTimeLeftLabel->setText(u"—"_s);
        if (m_replayTimeRightLabel) m_replayTimeRightLabel->setText(u"—"_s);
        return;
    }

    if (!m_model->replayMode()) {
        if (m_replaySampleCaption)
            m_replaySampleCaption->setText(
                QStringLiteral("%1 samples buffered · open Replay from the toolbar")
                    .arg(MetricDefs::formatIntGrouped(m_liveSampleCount)));
        if (m_replayTimeLeftLabel)  m_replayTimeLeftLabel->setText(u"—"_s);
        if (m_replayTimeRightLabel) m_replayTimeRightLabel->setText(u"—"_s);
        return;
    }

    if (!m_session || m_session->samples.empty()) {
        if (m_replaySampleCaption)  m_replaySampleCaption->setText(u"No file loaded"_s);
        if (m_replayTimeLeftLabel)  m_replayTimeLeftLabel->setText(u"—"_s);
        if (m_replayTimeRightLabel) m_replayTimeRightLabel->setText(u"—"_s);
        return;
    }

    const auto &samples = m_session->samples;
    const int n    = static_cast<int>(samples.size());
    const int head = std::clamp(m_lastTrailLength, 0, n);
    const long tRef = samples.front().timestamp;
    const bool sessionElapsed = MetricDefs::useSessionElapsedTimeAxis(
        samples.front().timestamp, samples.back().timestamp);
    const double tLogStart = MetricDefs::chartXSeconds(tRef, samples.front().timestamp, sessionElapsed);
    const double tLogEnd   = MetricDefs::chartXSeconds(tRef, samples.back().timestamp, sessionElapsed);
    const double fullDur   = std::max(0.0, tLogEnd - tLogStart);

    if (m_replaySampleCaption) {
        if (head <= 0) {
            m_replaySampleCaption->setText(
                QStringLiteral("%1 samples · drag timeline to start")
                    .arg(MetricDefs::formatIntGrouped(n)));
        } else {
            const int pct = n > 0 ? static_cast<int>(
                std::lround(100.0 * static_cast<double>(head) / static_cast<double>(n))) : 0;
            m_replaySampleCaption->setText(
                QStringLiteral("%1 / %2 samples · %3%")
                    .arg(MetricDefs::formatIntGrouped(head))
                    .arg(MetricDefs::formatIntGrouped(n))
                    .arg(pct));
        }
    }

    if (m_replayTimeLeftLabel && m_replayTimeRightLabel) {
        const double elapsed = (head > 0)
            ? (MetricDefs::chartXSeconds(tRef, samples[static_cast<std::size_t>(head - 1)].timestamp, sessionElapsed)
               - tLogStart)
            : 0.0;
        m_replayTimeLeftLabel->setText(MetricDefs::formatReplayClockHms(elapsed));
        m_replayTimeRightLabel->setText(MetricDefs::formatReplayClockHms(fullDur));
    }
}

void ReplayBar::syncTransportChrome()
{
    if (!m_playPauseBtn) return;

    const bool live     = !m_model || !m_model->replayMode();
    const bool hasLog   = m_session && !m_session->samples.empty();
    const bool canUse   = !live && hasLog && m_replay;
    const bool isPlaying = m_replay && m_replay->isPlaying();

    if (m_replayBarTitle) {
        const QString base  = live ? u"LIVE BUFFER"_s : u"REPLAY"_s;
        const bool showState = canUse && !m_replayActivityText.isEmpty()
                               && m_replayActivityText != u"Ready"_s
                               && m_replayActivityText != u"Idle"_s;
        m_replayBarTitle->setText(
            showState ? QStringLiteral("%1 · %2").arg(base, m_replayActivityText) : base);
    }

    if (style()) {
        m_playPauseBtn->setIcon(style()->standardIcon(
            isPlaying ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
    }

    m_playPauseBtn->setEnabled(canUse);
    if (m_stopBtn)      m_stopBtn->setEnabled(canUse);
    if (m_jumpStartBtn) m_jumpStartBtn->setEnabled(canUse);
    if (m_jumpEndBtn)   m_jumpEndBtn->setEnabled(canUse);
    if (m_stepBackBtn)  m_stepBackBtn->setEnabled(canUse);
    if (m_stepFwdBtn)   m_stepFwdBtn->setEnabled(canUse);
    if (m_speedCombo)   m_speedCombo->setEnabled(canUse);
    if (m_replaySlider) {
        if (live || !hasLog)
            m_replaySlider->setEnabled(false);
        else
            m_replaySlider->setEnabled(static_cast<int>(m_session->samples.size()) > 0);
    }
}
