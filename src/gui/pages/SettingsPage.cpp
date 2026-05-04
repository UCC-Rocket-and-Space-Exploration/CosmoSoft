#include "gui/pages/SettingsPage.h"

#include "gui/SettingsKeys.h"
#include "gui/Theme.h"
#include "gui/ThemeManager.h"
#include "gui/SkinLoader.h"
#include "gui/pages/EventLogPage.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QShowEvent>
#include <QSysInfo>
#include <QTabWidget>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace {

/** @brief Page-specific stylesheet for SettingsPage (ID-targeted rules only).
 *  Global widget defaults (QPushButton, QComboBox, QScrollBar, QCheckBox,
 *  QGroupBox, QTabWidget) are provided by assets/theme.qss. */
QString buildPageStyleSheet() {
    return QString(uR"(
    QWidget {
        background-color: %1;
    }
    QLabel#settingsHeading {
        font-size: 18px;
        font-weight: bold;
        color: %2;
        letter-spacing: 1px;
    }
    QLabel#settingsMutedLabel {
        color: %3;
        font-size: %4px;
    }
    QLabel#aboutAppName {
        font-size: 26px;
        font-weight: bold;
        color: %2;
        letter-spacing: 2px;
    }
    QLabel#aboutVersion {
        color: %5;
        font-size: %6px;
    }
    QLabel#aboutDesc {
        color: %7;
        font-size: %4px;
    }
    QLabel#aboutLink {
        color: %5;
        font-size: %4px;
    }
    QLabel#devSectionTitle {
        font-size: %6px;
        font-weight: bold;
        color: %2;
        letter-spacing: 1px;
    }
    QLabel#sysInfoLabel {
        color: %7;
        font-size: %4px;
        background-color: %8;
        padding: 12px 16px;
        border-radius: %9px;
    }
    QCheckBox#debugModeCheck {
        font-size: %6px;
        font-weight: bold;
        color: %5;
        spacing: 10px;
    }
    QCheckBox#debugModeCheck::indicator {
        width: 18px;
        height: 18px;
        border: 1px solid %3;
        border-radius: 3px;
        background-color: %8;
    }
    QCheckBox#debugModeCheck::indicator:checked {
        background-color: %5;
        border-color: %5;
    }
    QFrame#divider {
        color: %3;
    }
)"_s)
    .arg(Theme::kBgBase())         // %1
    .arg(Theme::kTextPrimary())    // %2
    .arg(Theme::kTextMuted())      // %3
    .arg(Theme::kFontSizeBase)     // %4
    .arg(Theme::kAccentLink())     // %5
    .arg(Theme::kFontSizeMd)       // %6
    .arg(Theme::kTextMid())        // %7
    .arg(Theme::kBgDark())         // %8
    .arg(Theme::kRadiusSm);        // %9
}

QWidget *makeScrollWrapper(QWidget *inner, QWidget *parent) {
    auto *scroll = new QScrollArea(parent);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(inner);
    return scroll;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────

SettingsPage::SettingsPage(QWidget *parent)
    : QWidget(parent) {
    setObjectName(u"settingsPage"_s);
    buildUi();
}

bool SettingsPage::debugModeEnabled() const {
    return m_debugModeCheck && m_debugModeCheck->isChecked();
}

void SettingsPage::buildUi() {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(0);

    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(u"settingsTabs"_s);
    m_tabs->addTab(buildGeneralTab(),   u"General"_s);
    m_tabs->addTab(buildAboutTab(),     u"About"_s);
    m_tabs->addTab(buildDeveloperTab(), u"Developer"_s);

    root->addWidget(m_tabs);

    setStyleSheet(buildPageStyleSheet());
}

// ── General tab ───────────────────────────────────────────────────────────────

QWidget *SettingsPage::buildGeneralTab() {
    auto *inner = new QWidget();
    auto *layout = new QVBoxLayout(inner);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(20);

    auto *heading = new QLabel(u"General"_s, inner);
    heading->setObjectName(u"settingsHeading"_s);
    layout->addWidget(heading);

    auto *intro = new QLabel(
        u"Appearance and audio preferences.\n"
        u"Use the connection bar on the main window for serial and flight logs."_s,
        inner);
    intro->setWordWrap(true);
    intro->setObjectName(u"settingsMutedLabel"_s);
    layout->addWidget(intro);

    auto *divider = new QFrame(inner);
    divider->setObjectName(u"divider"_s);
    divider->setFrameShape(QFrame::HLine);
    layout->addWidget(divider);

    // Skin group
    m_skinGroup = new QGroupBox(u"Skin"_s, inner);
    auto *skinForm = new QFormLayout(m_skinGroup);
    skinForm->setSpacing(10);
    skinForm->setContentsMargins(16, 20, 16, 16);
    m_skinCombo = new QComboBox(m_skinGroup);
    const auto skins = cosmo::SkinLoader::discoverAll(cosmo::ThemeManager::skinsDirectory());
    for (const auto &skin : skins) {
        m_skinCombo->addItem(skin.name, skin.id);
    }
    const auto &active_id = cosmo::ThemeManager::instance().current().id;
    for (int i = 0; i < m_skinCombo->count(); ++i) {
        if (m_skinCombo->itemData(i).toString() == active_id) {
            m_skinCombo->setCurrentIndex(i);
            break;
        }
    }
    connect(m_skinCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsPage::onSkinChanged);
    skinForm->addRow(u"Active skin:"_s, m_skinCombo);

    m_importSkinBtn = new QPushButton(u"Import .cosmo skin..."_s, m_skinGroup);
    connect(m_importSkinBtn, &QPushButton::clicked, this, &SettingsPage::onImportSkin);
    skinForm->addRow(u""_s, m_importSkinBtn);
    layout->addWidget(m_skinGroup);

    // Font group
    m_fontGroup = new QGroupBox(u"Font"_s, inner);
    auto *fontForm = new QFormLayout(m_fontGroup);
    fontForm->setSpacing(10);
    fontForm->setContentsMargins(16, 20, 16, 16);
    m_fontSizeCombo = new QComboBox(m_fontGroup);
    for (int pt = 9; pt <= 18; ++pt) {
        m_fontSizeCombo->addItem(QString::number(pt), pt);
    }
    m_fontSizeCombo->setCurrentIndex(3);
    connect(m_fontSizeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsPage::onFontSizeChanged);
    fontForm->addRow(u"UI font size (pt):"_s, m_fontSizeCombo);
    m_fontPreview = new QLabel(u"Preview: Alt 1234.5 m  ·  Temp 22.3 °C  ·  RSSI −60 dBm"_s, m_fontGroup);
    m_fontPreview->setStyleSheet(
        QString(u"color: %1; background-color: %2; padding: 8px 12px; border-radius: %3px; margin-top: 6px;"_s)
            .arg(Theme::kTextPrimary())
            .arg(Theme::kBgDark())
            .arg(Theme::kRadiusSm));
    fontForm->addRow(u""_s, m_fontPreview);
    layout->addWidget(m_fontGroup);

    // Units group
    m_unitsGroup = new QGroupBox(u"Units"_s, inner);
    auto *unitsForm = new QFormLayout(m_unitsGroup);
    unitsForm->setSpacing(10);
    unitsForm->setContentsMargins(16, 20, 16, 16);
    m_unitSystemCombo = new QComboBox(m_unitsGroup);
    m_unitSystemCombo->addItem(u"Metric (m, °C, Pa)"_s, u"metric"_s);
    m_unitSystemCombo->addItem(u"Imperial (ft, °F, psi)"_s, u"imperial"_s);
    m_unitSystemCombo->setCurrentIndex(0);
    connect(m_unitSystemCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsPage::onUnitSystemChanged);
    unitsForm->addRow(u"Unit system:"_s, m_unitSystemCombo);
    layout->addWidget(m_unitsGroup);

    // Sound group
    m_soundGroup = new QGroupBox(u"Sound"_s, inner);
    auto *soundLayout = new QVBoxLayout(m_soundGroup);
    soundLayout->setContentsMargins(16, 20, 16, 16);
    soundLayout->setSpacing(8);
    m_uiSoundsCheck = new QCheckBox(u"Enable UI sounds"_s, m_soundGroup);
    m_uiSoundsCheck->setChecked(true);
    connect(m_uiSoundsCheck, &QCheckBox::toggled, this, &SettingsPage::onSoundsToggled);
    soundLayout->addWidget(m_uiSoundsCheck);
    auto *soundHint = new QLabel(u"Reserved for future alerts and feedback tones."_s, m_soundGroup);
    soundHint->setWordWrap(true);
    soundHint->setObjectName(u"settingsMutedLabel"_s);
    soundLayout->addWidget(soundHint);
    layout->addWidget(m_soundGroup);

    layout->addStretch(1);
    return makeScrollWrapper(inner, nullptr);
}

// ── About tab ─────────────────────────────────────────────────────────────────

QWidget *SettingsPage::buildAboutTab() {
    auto *inner = new QWidget();
    auto *layout = new QVBoxLayout(inner);
    layout->setContentsMargins(32, 32, 32, 32);
    layout->setSpacing(12);

    auto *appName = new QLabel(u"CosmoSoft"_s, inner);
    appName->setObjectName(u"aboutAppName"_s);
    layout->addWidget(appName);

    auto *version = new QLabel(u"Version 0.1.0 — development build"_s, inner);
    version->setObjectName(u"aboutVersion"_s);
    layout->addWidget(version);

    auto *divider = new QFrame(inner);
    divider->setObjectName(u"divider"_s);
    divider->setFrameShape(QFrame::HLine);
    layout->addSpacing(8);
    layout->addWidget(divider);
    layout->addSpacing(8);

    auto *desc = new QLabel(
        u"Open-source ground-station software for rocket telemetry.\n"
        u"Written in C++20 with Qt 6."_s,
        inner);
    desc->setWordWrap(true);
    desc->setObjectName(u"aboutDesc"_s);
    layout->addWidget(desc);

    auto *license = new QLabel(u"Licensed under the Apache License 2.0."_s, inner);
    license->setObjectName(u"aboutDesc"_s);
    layout->addWidget(license);

    layout->addSpacing(4);

    auto *repoLabel = new QLabel(
        u"<a href=\"https://github.com/UCC-Rocket-and-Space-Exploration/CosmoSoft\">"
        u"github.com/UCC-Rocket-and-Space-Exploration/CosmoSoft</a>"_s,
        inner);
    repoLabel->setObjectName(u"aboutLink"_s);
    repoLabel->setOpenExternalLinks(true);
    repoLabel->setTextFormat(Qt::RichText);
    layout->addWidget(repoLabel);

    layout->addSpacing(24);

    auto *creditsHeading = new QLabel(u"Built with"_s, inner);
    creditsHeading->setObjectName(u"settingsHeading"_s);
    layout->addWidget(creditsHeading);

    const QString qtLine = QStringLiteral("Qt %1  ·  C++20").arg(QLatin1StringView(QT_VERSION_STR));
    auto *techLabel = new QLabel(qtLine, inner);
    techLabel->setObjectName(u"aboutDesc"_s);
    layout->addWidget(techLabel);

    layout->addStretch(1);
    return makeScrollWrapper(inner, nullptr);
}

// ── Developer tab ─────────────────────────────────────────────────────────────

QWidget *SettingsPage::buildDeveloperTab() {
    auto *outer = new QWidget();
    auto *outerLayout = new QVBoxLayout(outer);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    // ── Header bar with debug toggle ──────────────────────────────────────────
    auto *header = new QWidget(outer);
    header->setObjectName(u"devHeader"_s);
    header->setStyleSheet(
        QString(u"QWidget#devHeader { background-color: #252528; border-bottom: 1px solid %1; }"_s)
            .arg(Theme::kBorderSubtle()));
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(20, 14, 20, 14);
    headerLayout->setSpacing(16);

    m_debugModeCheck = new QCheckBox(u"Debug mode"_s, header);
    m_debugModeCheck->setObjectName(u"debugModeCheck"_s);
    m_debugModeCheck->setToolTip(
        u"Enable debug mode to expose verbose system information and developer tools."_s);
    connect(m_debugModeCheck, &QCheckBox::toggled, this, &SettingsPage::onDebugModeToggled);
    headerLayout->addWidget(m_debugModeCheck);

    auto *headerHint = new QLabel(u"Enable to unlock verbose info and developer tools."_s, header);
    headerHint->setObjectName(u"settingsMutedLabel"_s);
    headerLayout->addWidget(headerHint);
    headerLayout->addStretch(1);

    outerLayout->addWidget(header);

    // ── Scrollable content below the header ───────────────────────────────────
    auto *inner = new QWidget(outer);
    auto *innerLayout = new QVBoxLayout(inner);
    innerLayout->setContentsMargins(20, 20, 20, 20);
    innerLayout->setSpacing(20);

    // System information group
    m_sysInfoGroup = new QGroupBox(u"System Information"_s, inner);
    auto *sysLayout = new QVBoxLayout(m_sysInfoGroup);
    sysLayout->setContentsMargins(16, 20, 16, 16);

    const QString sysText = QStringLiteral(
        "App version   :  0.1.0 (development)\n"
        "Qt version    :  %1\n"
        "OS            :  %2 %3\n"
        "Architecture  :  %4\n"
        "Build type    :  %5\n"
        "Settings org  :  CosmoSoft\n"
        "Settings app  :  cosmo-soft"
    ).arg(
        QLatin1StringView(QT_VERSION_STR),
        QSysInfo::productType(),
        QSysInfo::productVersion(),
        QSysInfo::currentCpuArchitecture(),
#ifdef QT_DEBUG
        u"Debug"_s
#else
        u"Release"_s
#endif
    );

    m_sysInfoLabel = new QLabel(sysText, m_sysInfoGroup);
    m_sysInfoLabel->setObjectName(u"sysInfoLabel"_s);
    m_sysInfoLabel->setWordWrap(false);
    m_sysInfoLabel->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    sysLayout->addWidget(m_sysInfoLabel);
    innerLayout->addWidget(m_sysInfoGroup);

    // Developer helper / cheat-sheet group
    auto *helperGroup = new QGroupBox(u"Developer Reference"_s, inner);
    auto *helperLayout = new QVBoxLayout(helperGroup);
    helperLayout->setContentsMargins(16, 20, 16, 16);
    helperLayout->setSpacing(6);

    auto *helperText = new QPlainTextEdit(helperGroup);
    helperText->setReadOnly(true);
    helperText->setMinimumHeight(200);
    helperText->setStyleSheet(
        QString(u"QPlainTextEdit { background-color: %1; color: #b0c4b0; font-family: %2; font-size: %3px; border: none; padding: 12px; }"_s)
            .arg(Theme::kBgDark())
            .arg(Theme::kFontMono)
            .arg(Theme::kFontSizeBase));
    helperText->setPlainText(
        u"─── Workflow ───────────────────────────────────────────────\n"
        u"  1. Select serial port + baud on the Monitoring connection bar.\n"
        u"  2. Click Connect to start the live telemetry pipeline.\n"
        u"  3. Or click Open log… to load a .telem / .csv flight log.\n"
        u"  4. Switch to Flight data for charts and replay.\n"
        u"  5. Click Export session… to write a timestamped text log.\n"
        u"\n"
        u"─── File formats ───────────────────────────────────────────\n"
        u"  .telem   AltOS binary framed telemetry (Framer + Parser)\n"
        u"  .csv     Theseus CSV: time_s, lat, lon, alt_m, ...\n"
        u"\n"
        u"─── Architecture overview ──────────────────────────────────\n"
        u"  SerialWorker  → BlockingQueue → ParserWorker\n"
        u"                                   ↓\n"
        u"  FlightDataModel ← appendSample (Qt::QueuedConnection)\n"
        u"  FlightLogManager ← appendSample (for export)\n"
        u"\n"
        u"─── QSettings location (macOS) ─────────────────────────────\n"
        u"  ~/Library/Preferences/CosmoSoft.cosmo-soft.plist\n"
        u"\n"
        u"─── QSettings keys ─────────────────────────────────────────\n"
        u"  window/mainGeometry         Main window geometry\n"
        u"  window/settingsGeometry     Settings window geometry\n"
        u"  serial/port                 Last serial device path\n"
        u"  serial/baud                 Last baud rate\n"
        u"  paths/replayDir             Last replay directory\n"
        u"  ui/dashboardSplitterState   Dashboard splitter\n"
        u"  ui/fontPointSize            App font pt size\n"
        u"  ui/soundsEnabled            Sounds toggle\n"
        u"  ui/debugMode                Debug mode toggle\n"
        u"  ui/settingsActiveTab        Last active settings tab\n"
        u"\n"
        u"─── Useful build commands ──────────────────────────────────\n"
        u"  cmake --preset debug\n"
        u"  cmake --build build/debug --target CosmoSoft -j\n"
        u"  cmake --preset release\n"
        u"  cmake --build build/release --target CosmoSoft -j\n"_s);
    helperLayout->addWidget(helperText);
    innerLayout->addWidget(helperGroup);

    innerLayout->addStretch(1);

    auto *scroll = new QScrollArea(outer);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(inner);
    outerLayout->addWidget(scroll, 1);

    // ── Event log pinned at bottom ────────────────────────────────────────────
    auto *logHeader = new QWidget(outer);
    logHeader->setStyleSheet(
        QString(u"background-color: #252528; border-top: 1px solid %1; border-bottom: none;"_s)
            .arg(Theme::kBorderSubtle()));
    auto *logHeaderRow = new QHBoxLayout(logHeader);
    logHeaderRow->setContentsMargins(20, 8, 20, 8);
    auto *logTitle = new QLabel(u"Event Log"_s, logHeader);
    logTitle->setObjectName(u"devSectionTitle"_s);
    logHeaderRow->addWidget(logTitle);
    logHeaderRow->addStretch(1);
    outerLayout->addWidget(logHeader);

    m_eventLog = new EventLogPage(outer);
    m_eventLog->setMinimumHeight(200);
    outerLayout->addWidget(m_eventLog);

    return outer;
}

// ── Slot implementations ──────────────────────────────────────────────────────

void SettingsPage::appendLogEntry(const QString &text) {
    if (m_eventLog) {
        m_eventLog->appendEntry(text);
    }
}

void SettingsPage::appendLogError(const QString &text) {
    if (m_eventLog) {
        m_eventLog->appendError(text);
    }
}

void SettingsPage::applyFontPointSize(int pt) {
    if (pt < 6 || pt > 48) {
        return;
    }
    QFont f = qApp->font();
    f.setPointSize(pt);
    qApp->setFont(f);
}

void SettingsPage::onFontSizeChanged(int index) {
    if (!m_fontSizeCombo || index < 0) {
        return;
    }
    const int pt = m_fontSizeCombo->itemData(index).toInt();
    if (pt > 0) {
        applyFontPointSize(pt);
        QSettings s(kSettingsOrg, kSettingsApp);
        s.setValue(kSettingsFontSize, pt);
        if (m_fontPreview) {
            QFont previewFont = m_fontPreview->font();
            previewFont.setPointSize(pt);
            m_fontPreview->setFont(previewFont);
        }
    }
}

void SettingsPage::onUnitSystemChanged(int index) {
    if (!m_unitSystemCombo || index < 0) {
        return;
    }
    const QString system = m_unitSystemCombo->itemData(index).toString();
    QSettings s(kSettingsOrg, kSettingsApp);
    s.setValue(kSettingsUnitSystem, system);
    emit unitSystemChanged(system);
}

void SettingsPage::onSoundsToggled(bool enabled) {
    QSettings s(kSettingsOrg, kSettingsApp);
    s.setValue(kSettingsSoundsEnabled, enabled);
}

void SettingsPage::onDebugModeToggled(bool enabled) {
    if (m_sysInfoGroup) {
        m_sysInfoGroup->setVisible(enabled);
    }
    QSettings s(kSettingsOrg, kSettingsApp);
    s.setValue(kSettingsDebugMode, enabled);
    emit debugModeChanged(enabled);
}

void SettingsPage::onSkinChanged(int index) {
    const auto skin_id = m_skinCombo->itemData(index).toString();
    const auto skins = cosmo::SkinLoader::discoverAll(cosmo::ThemeManager::skinsDirectory());
    for (const auto &skin : skins) {
        if (skin.id == skin_id) {
            cosmo::ThemeManager::instance().setActiveSkin(skin);
            break;
        }
    }
}

void SettingsPage::onImportSkin() {
    const auto path = QFileDialog::getOpenFileName(
        this, u"Import CosmoSoft Skin"_s, QString(),
        u"CosmoSoft Skins (*.cosmo);;Zip Archives (*.zip)"_s);
    if (path.isEmpty()) return;

    const auto dest = cosmo::ThemeManager::skinsDirectory();
    auto theme = cosmo::SkinLoader::importArchive(path, dest);
    if (!theme) return;

    m_skinCombo->addItem(theme->name, theme->id);
    m_skinCombo->setCurrentIndex(m_skinCombo->count() - 1);
}

void SettingsPage::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    loadFromSettings();
}

void SettingsPage::closeEvent(QCloseEvent *event) {
    saveToSettings();
    QWidget::closeEvent(event);
}

void SettingsPage::loadFromSettings() {
    QSettings s(kSettingsOrg, kSettingsApp);

    const int pt = s.value(kSettingsFontSize, 12).toInt();
    if (m_fontSizeCombo) {
        const int idx = m_fontSizeCombo->findData(pt);
        if (idx >= 0) {
            m_fontSizeCombo->blockSignals(true);
            m_fontSizeCombo->setCurrentIndex(idx);
            m_fontSizeCombo->blockSignals(false);
        }
        applyFontPointSize(pt);
    }
    if (m_unitSystemCombo) {
        const QString system = s.value(kSettingsUnitSystem, u"metric"_s).toString();
        const int unitIdx = m_unitSystemCombo->findData(system);
        if (unitIdx >= 0) {
            m_unitSystemCombo->blockSignals(true);
            m_unitSystemCombo->setCurrentIndex(unitIdx);
            m_unitSystemCombo->blockSignals(false);
        }
    }
    if (m_uiSoundsCheck) {
        m_uiSoundsCheck->setChecked(s.value(kSettingsSoundsEnabled, true).toBool());
    }

    const bool debugOn = s.value(kSettingsDebugMode, false).toBool();
    if (m_debugModeCheck) {
        m_debugModeCheck->blockSignals(true);
        m_debugModeCheck->setChecked(debugOn);
        m_debugModeCheck->blockSignals(false);
    }
    if (m_sysInfoGroup) {
        m_sysInfoGroup->setVisible(debugOn);
    }

    const int activeTab = s.value(kSettingsActiveTab, 0).toInt();
    if (m_tabs && activeTab >= 0 && activeTab < m_tabs->count()) {
        m_tabs->setCurrentIndex(activeTab);
    }

    const QByteArray geo = s.value(kSettingsWindowSettingsGeo).toByteArray();
    if (!geo.isEmpty()) {
        restoreGeometry(geo);
    }
}

void SettingsPage::saveToSettings() {
    QSettings s(kSettingsOrg, kSettingsApp);
    if (m_fontSizeCombo) {
        const int pt = m_fontSizeCombo->currentData().toInt();
        if (pt > 0) {
            s.setValue(kSettingsFontSize, pt);
        }
    }
    if (m_unitSystemCombo) {
        s.setValue(kSettingsUnitSystem, m_unitSystemCombo->currentData().toString());
    }
    if (m_uiSoundsCheck) {
        s.setValue(kSettingsSoundsEnabled, m_uiSoundsCheck->isChecked());
    }
    if (m_debugModeCheck) {
        s.setValue(kSettingsDebugMode, m_debugModeCheck->isChecked());
    }
    if (m_tabs) {
        s.setValue(kSettingsActiveTab, m_tabs->currentIndex());
    }
    s.setValue(kSettingsWindowSettingsGeo, saveGeometry());
}
