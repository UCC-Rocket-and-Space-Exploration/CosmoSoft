#include "gui/widgets/ReplayBar.h"

#include "gui/FlightDataModel.h"
#include "gui/FlightReplayController.h"
#include "gui/Theme.h"
#include "gui/ThemeManager.h"
#include "gui/widgets/MetricDefs.h"
#include "services/preview/FlightPreviewCache.h"

#include "domain/FlightSession.h"

#include <QColor>
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
#include <utility>

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
    const auto textPri      = Theme::kTextPrimary();
    const auto textMuted    = Theme::kTextMuted();
    const auto bgPanel      = Theme::kBgPanel();
    const auto bgButton     = Theme::kBgButton();
    const auto btnHover     = Theme::kBtnHover();
    const auto btnPressed   = Theme::kBtnPressed();
    const auto borderPanel  = Theme::kBorderPanel();
    const auto borderDefault = Theme::kBorderDefault();
    const auto borderLight  = Theme::kBorderLight();
    const auto accent       = Theme::kAccentLink();
    const auto fontMono     = QString::fromUtf8(Theme::kFontMono);
    const auto success      = Theme::kSuccess();
    const auto successBg    = Theme::kSuccessBg();
    const auto warning      = Theme::kWarning();
    const auto warningBg    = Theme::kWarningBg();
    const auto info         = Theme::kInfo();
    const auto infoBg       = Theme::kInfoBg();

    setStyleSheet(QString(uR"(
        QFrame#replayBar {
            background-color: %1;
            border: 1px solid %2;
            border-radius: %3px;
        }

        /* Navigation button group container */
        QFrame#navButtonGroup {
            background-color: %20;
            border: none;
            border-radius: 6px;
        }

        /* Navigation buttons - 32×32px */
        QToolButton#replaySkipBtn {
            background: %4;
            border: 1px solid %5;
            border-radius: 4px;
            padding: 2px;
            min-width: 32px; max-width: 32px;
            min-height: 32px; max-height: 32px;
        }
        QToolButton#replaySkipBtn:hover {
            background: %6;
            border-color: %7;
        }
        QToolButton#replaySkipBtn:pressed {
            background: %8;
        }
        QToolButton#replaySkipBtn:disabled {
            color: %9;
            border-color: %2;
        }

        /* Play/Pause button - 40×40px, prominent */
        QToolButton#replayPlayPauseBtn {
            background: %4;
            border: 2px solid %10;
            border-radius: 6px;
            padding: 2px;
            min-width: 40px; max-width: 40px;
            min-height: 40px; max-height: 40px;
            color: %11;
        }
        QToolButton#replayPlayPauseBtn:hover {
            background: %6;
            border-color: %10;
        }
        QToolButton#replayPlayPauseBtn:pressed {
            background: %8;
        }
        QToolButton#replayPlayPauseBtn:disabled {
            color: %9;
            border-color: %2;
        }

        /* Stop button - 30×30px, de-emphasized */
        QToolButton#replayTransportBtn {
            background: %4;
            border: 1px solid %2;
            border-radius: 4px;
            padding: 2px;
            min-width: 30px; max-width: 30px;
            min-height: 30px; max-height: 30px;
            color: %9;
        }
        QToolButton#replayTransportBtn:hover {
            background: %6;
            border-color: %7;
        }
        QToolButton#replayTransportBtn:pressed {
            background: %8;
        }
        QToolButton#replayTransportBtn:disabled {
            color: %9;
            border-color: %2;
        }

        /* Enhanced slider - 8px groove */
        QSlider#replayScrubSlider::groove:horizontal {
            height: 8px;
            background: %2;
            border-radius: 4px;
        }
        QSlider#replayScrubSlider::handle:horizontal {
            background: %10;
            width: 14px;
            height: 14px;
            margin: -3px 0;
            border-radius: 7px;
            border: none;
        }
        QSlider#replayScrubSlider::handle:horizontal:hover {
            width: 16px;
            height: 16px;
            margin: -4px 0;
        }
        QSlider#replayScrubSlider::sub-page:horizontal {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                                        stop:0 %10, stop:1 %21);
            border-radius: 4px;
        }
        QSlider#replayScrubSlider:disabled::groove:horizontal {
            background: %2;
        }
        QSlider#replayScrubSlider:disabled::handle:horizontal {
            background: %5;
        }

        /* Speed combo */
        QComboBox#replaySpeedCombo {
            background: %4;
            border: 1px solid %5;
            border-radius: 12px;
            padding: 4px 8px 4px 10px;
            min-height: 24px;
            min-width: 50px;
            font-size: %12px;
            font-family: %13;
            font-weight: 600;
            color: %11;
        }
        QComboBox#replaySpeedCombo:hover {
            background: %6;
            border-color: %7;
        }
        QComboBox#replaySpeedCombo::drop-down {
            border: none;
            width: 20px;
        }
        QComboBox#replaySpeedCombo QAbstractItemView {
            background-color: %1;
            color: %11;
            selection-background-color: %14;
            border: 1px solid %5;
        }

        /* Status pill */
        QLabel#replayStatusPill {
            font-size: 9px;
            font-family: %13;
            padding: 2px 6px;
            border-radius: 8px;
            font-weight: 600;
        }
        QLabel#replayStatusPill[state="playing"] {
            background-color: %15;
            color: %16;
        }
        QLabel#replayStatusPill[state="paused"] {
            background-color: %17;
            color: %18;
        }
        QLabel#replayStatusPill[state="stopped"] {
            background-color: %19;
            color: %22;
        }

        /* Clock labels */
        QLabel#replayClockLabel {
            font-size: %12px;
            font-family: %13;
            color: %11;
            letter-spacing: 0.5px;
            min-width: 36px;
        }

        /* Vertical divider */
        QFrame#replayVDiv {
            background-color: %2;
            border: none;
        }

        /* Caption labels */
        QLabel#replayTitleLabel {
            font-size: 10px;
            font-family: %13;
            color: %9;
            letter-spacing: 1.5px;
            font-weight: 600;
        }
        QLabel#replayCaption {
            font-size: 10px;
            font-family: %13;
            color: %9;
        }
    )"_s)
        .arg(bgPanel)           // %1
        .arg(borderPanel)       // %2
        .arg(Theme::kRadiusMd)  // %3
        .arg(bgButton)          // %4
        .arg(borderDefault)     // %5
        .arg(btnHover)          // %6
        .arg(borderLight)       // %7
        .arg(btnPressed)        // %8
        .arg(textMuted)         // %9
        .arg(accent)            // %10
        .arg(textPri)           // %11
        .arg(Theme::kFontSizeBase) // %12
        .arg(fontMono)          // %13
        .arg(Theme::kSelectBg()) // %14
        .arg(successBg)         // %15
        .arg(success)           // %16
        .arg(warningBg)         // %17
        .arg(warning)           // %18
        .arg(infoBg)            // %19
        .arg(QColor(bgPanel).lighter(105).name()) // %20 - nav group bg
        .arg(QColor(accent).lighter(140).name())  // %21 - slider gradient end
        .arg(info));            // %22
}

// ── Public API ────────────────────────────────────────────────────────────────

void ReplayBar::setSession(
    std::shared_ptr<const FlightSession> session,
    std::shared_ptr<const cosmo::preview::FlightPreviewCache> preview)
{
    m_session = std::move(session);
    m_preview = std::move(preview);
    const int n = m_session ? static_cast<int>(m_session->samples.size()) : 0;

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
    outer->setContentsMargins(Theme::kSpaceMd, Theme::kSpaceBase,
                              Theme::kSpaceMd, Theme::kSpaceBase);
    outer->setSpacing(Theme::kSpaceSm);  // 6px between Row 1 and Row 2

    // ── Row 1: transport buttons | slider | clock ─────────────────────────────
    auto *transportRow = new QHBoxLayout();
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
    m_jumpStartBtn->setFixedSize(32, 32);
    m_jumpStartBtn->setToolTip(u"Jump to start  [Home key]"_s);
    m_jumpStartBtn->setAccessibleName(u"Jump to start"_s);
    m_jumpStartBtn->setIcon(style()->standardIcon(QStyle::SP_MediaSkipBackward));

    m_stepBackBtn = new QToolButton(this);
    setupBtn(m_stepBackBtn, u"replaySkipBtn"_s, QSize(16, 16));
    m_stepBackBtn->setFixedSize(32, 32);
    m_stepBackBtn->setToolTip(u"Step back one sample  [← key]"_s);
    m_stepBackBtn->setAccessibleName(u"Step backward"_s);
    m_stepBackBtn->setIcon(style()->standardIcon(QStyle::SP_MediaSeekBackward));

    m_playPauseBtn = new QToolButton(this);
    m_playPauseBtn->setObjectName(u"replayPlayPauseBtn"_s);
    m_playPauseBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_playPauseBtn->setAutoRaise(false);
    m_playPauseBtn->setFocusPolicy(Qt::StrongFocus);
    m_playPauseBtn->setIconSize(QSize(20, 20));
    m_playPauseBtn->setFixedSize(40, 40);
    m_playPauseBtn->setToolTip(u"Play / Pause  [Space]"_s);
    m_playPauseBtn->setAccessibleName(u"Play or pause replay"_s);
    m_playPauseBtn->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));

    m_stepFwdBtn = new QToolButton(this);
    setupBtn(m_stepFwdBtn, u"replaySkipBtn"_s, QSize(16, 16));
    m_stepFwdBtn->setFixedSize(32, 32);
    m_stepFwdBtn->setToolTip(u"Step forward one sample  [→ key]"_s);
    m_stepFwdBtn->setAccessibleName(u"Step forward"_s);
    m_stepFwdBtn->setIcon(style()->standardIcon(QStyle::SP_MediaSeekForward));

    m_jumpEndBtn = new QToolButton(this);
    setupBtn(m_jumpEndBtn, u"replaySkipBtn"_s, QSize(16, 16));
    m_jumpEndBtn->setFixedSize(32, 32);
    m_jumpEndBtn->setToolTip(u"Jump to end  [End key]"_s);
    m_jumpEndBtn->setAccessibleName(u"Jump to end"_s);
    m_jumpEndBtn->setIcon(style()->standardIcon(QStyle::SP_MediaSkipForward));

    m_stopBtn = new QToolButton(this);
    setupBtn(m_stopBtn, u"replayTransportBtn"_s, QSize(14, 14));
    m_stopBtn->setFixedSize(30, 30);
    m_stopBtn->setToolTip(u"Stop replay and reset position"_s);
    m_stopBtn->setAccessibleName(u"Stop replay"_s);
    m_stopBtn->setIcon(style()->standardIcon(QStyle::SP_MediaStop));

    // Create navigation button group container
    m_navButtonGroup = new QFrame(this);
    m_navButtonGroup->setObjectName(u"navButtonGroup"_s);
    auto *navLayout = new QHBoxLayout(m_navButtonGroup);
    navLayout->setContentsMargins(4, 4, 4, 4);
    navLayout->setSpacing(Theme::kSpaceXs);  // 4px
    navLayout->addWidget(m_jumpStartBtn);
    navLayout->addWidget(m_stepBackBtn);
    navLayout->addWidget(m_stepFwdBtn);
    navLayout->addWidget(m_jumpEndBtn);

    auto *div = new QFrame(this);
    div->setObjectName(u"replayVDiv"_s);
    div->setFixedSize(1, 28);  // Taller to match larger buttons

    m_replaySlider = new QSlider(Qt::Horizontal, this);
    m_replaySlider->setObjectName(u"replayScrubSlider"_s);
    m_replaySlider->setRange(0, 0);
    m_replaySlider->setEnabled(false);
    m_replaySlider->setSingleStep(1);
    m_replaySlider->setPageStep(10);
    m_replaySlider->setTracking(true);
    m_replaySlider->setToolTip(u"Timeline — drag to scrub through the log"_s);
    m_replaySlider->setAccessibleName(u"Replay position"_s);

    m_replayTimeLeftLabel = new QLabel(u"—"_s, this);
    m_replayTimeLeftLabel->setObjectName(u"replayClockLabel"_s);
    m_replayTimeLeftLabel->setToolTip(u"Position / Duration"_s);

    auto *clockSep = new QLabel(u"/"_s, this);
    clockSep->setObjectName(u"replayClockLabel"_s);

    m_replayTimeRightLabel = new QLabel(u"—"_s, this);
    m_replayTimeRightLabel->setObjectName(u"replayClockLabel"_s);

    transportRow->setSpacing(0);  // Manual control via addSpacing

    // Navigation group
    transportRow->addWidget(m_navButtonGroup, 0, Qt::AlignVCenter);
    transportRow->addSpacing(Theme::kSpaceMd);  // 12px

    // Playback controls
    transportRow->addWidget(m_playPauseBtn, 0, Qt::AlignVCenter);
    transportRow->addSpacing(Theme::kSpaceSm);  // 6px
    transportRow->addWidget(m_stopBtn, 0, Qt::AlignVCenter);
    transportRow->addSpacing(Theme::kSpaceMd);  // 12px

    // Divider
    transportRow->addWidget(div, 0, Qt::AlignVCenter);
    transportRow->addSpacing(Theme::kSpaceMd);  // 12px

    // Speed control
    m_speedCombo = new QComboBox(this);
    m_speedCombo->setObjectName(u"replaySpeedCombo"_s);
    m_speedCombo->setToolTip(u"Playback speed (1× = real-time)"_s);
    m_speedCombo->setAccessibleName(u"Playback speed"_s);
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
    transportRow->addWidget(m_speedCombo, 0, Qt::AlignVCenter);
    transportRow->addSpacing(Theme::kSpaceBase);  // 8px

    // Timeline slider
    transportRow->addWidget(m_replaySlider, 1, Qt::AlignVCenter);
    transportRow->addSpacing(Theme::kSpaceBase);  // 8px

    // Clock labels
    transportRow->addWidget(m_replayTimeLeftLabel, 0, Qt::AlignVCenter);
    transportRow->addWidget(clockSep, 0, Qt::AlignVCenter);
    transportRow->addWidget(m_replayTimeRightLabel, 0, Qt::AlignVCenter);
    outer->addLayout(transportRow);

    // ── Row 2: title tag | caption | speed combo ──────────────────────────────
    auto *metaRow = new QHBoxLayout();
    metaRow->setSpacing(6);
    metaRow->setContentsMargins(0, 0, 0, 0);

    m_replayBarTitle = new QLabel(u"REPLAY"_s, this);
    m_replayBarTitle->setObjectName(u"replayTitleLabel"_s);
    metaRow->addWidget(m_replayBarTitle, 0, Qt::AlignVCenter);

    metaRow->addSpacing(4);

    // Status pill (hidden until replay is active)
    m_statusPill = new QLabel(this);
    m_statusPill->setObjectName(u"replayStatusPill"_s);
    m_statusPill->hide();
    metaRow->addWidget(m_statusPill, 0, Qt::AlignVCenter);

    metaRow->addSpacing(4);

    auto *metaSep = new QLabel(u"·"_s, this);
    metaSep->setObjectName(u"replayCaption"_s);
    metaRow->addWidget(metaSep, 0, Qt::AlignVCenter);

    m_replaySampleCaption = new QLabel(this);
    m_replaySampleCaption->setObjectName(u"replayCaption"_s);
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
    const double fullDur = m_preview
        ? m_preview->durationSeconds()
        : std::max(
              0.0,
              static_cast<double>(samples.back().timestamp - samples.front().timestamp) / 1000.0);

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
        const double elapsed = (head > 0 && m_preview)
            ? m_preview->displaySecondAt(head - 1)
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
        const QString base = live ? u"LIVE BUFFER"_s : u"REPLAY"_s;
        m_replayBarTitle->setText(base);
    }

    // Update status pill based on playback state
    if (m_statusPill) {
        if (!canUse) {
            m_statusPill->hide();
        } else {
            m_statusPill->show();

            if (isPlaying) {
                m_statusPill->setProperty("state", "playing");
                m_statusPill->setText(u"Playing"_s);
            } else if (m_replayActivityText.contains(u"Paused"_s, Qt::CaseInsensitive)) {
                m_statusPill->setProperty("state", "paused");
                m_statusPill->setText(u"Paused"_s);
            } else {
                m_statusPill->setProperty("state", "stopped");
                m_statusPill->setText(u"Stopped"_s);
            }

            // Repolish to apply new state styling
            m_statusPill->style()->unpolish(m_statusPill);
            m_statusPill->style()->polish(m_statusPill);
        }
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
