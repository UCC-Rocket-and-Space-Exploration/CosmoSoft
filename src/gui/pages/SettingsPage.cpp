#include "gui/pages/SettingsPage.h"

#include "gui/SettingsKeys.h"
#include "gui/Theme.h"
#include "gui/ThemeManager.h"
#include "gui/SkinLoader.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QSysInfo>
#include <QVBoxLayout>

#include <QtConcurrent/QtConcurrentRun>

using namespace Qt::StringLiterals;

namespace {

QWidget *makeScrollWrapper(QWidget *inner) {
    auto *scroll = new QScrollArea;
    scroll->setObjectName(u"sectionScroll"_s);
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

    connect(&cosmo::ThemeManager::instance(), &cosmo::ThemeManager::themeChanged,
            this, &SettingsPage::onThemeChanged);
}

bool SettingsPage::debugModeEnabled() const {
    return m_debugModeCheck && m_debugModeCheck->isChecked();
}

// ── Main layout ──────────────────────────────────────────────────────────────

void SettingsPage::buildUi() {
    setMinimumSize(560, 480);

    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Sidebar navigation
    m_nav = new QListWidget(this);
    m_nav->setObjectName(u"settingsNav"_s);
    m_nav->setFixedWidth(170);
    m_nav->setFrameShape(QFrame::NoFrame);
    m_nav->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_nav->setSpacing(2);

    m_nav->addItem(u"Appearance"_s);
    m_nav->addItem(u"Data"_s);
    m_nav->addItem(u"Developer"_s);
    m_nav->addItem(u"About"_s);

    m_nav->setCurrentRow(0);

    // Content stack
    m_pages = new QStackedWidget(this);
    m_pages->setObjectName(u"settingsContent"_s);

    m_pages->addWidget(makeScrollWrapper(buildAppearanceSection()));
    m_pages->addWidget(makeScrollWrapper(buildDataSection()));
    m_pages->addWidget(makeScrollWrapper(buildDeveloperSection()));
    m_pages->addWidget(makeScrollWrapper(buildAboutSection()));

    connect(m_nav, &QListWidget::currentRowChanged,
            m_pages, &QStackedWidget::setCurrentIndex);

    root->addWidget(m_nav);
    root->addWidget(m_pages, 1);

    refreshStyleSheet();
}

// ── Appearance section ───────────────────────────────────────────────────────

QWidget *SettingsPage::buildAppearanceSection() {
    auto *inner = new QWidget;
    auto *layout = new QVBoxLayout(inner);
    layout->setContentsMargins(28, 28, 28, 28);
    layout->setSpacing(20);

    // ── Skin group ────────────────────────────────────────────────────────────
    m_skinGroup = new QGroupBox(u"Skin"_s, inner);
    auto *skinLayout = new QVBoxLayout(m_skinGroup);
    skinLayout->setContentsMargins(16, 20, 16, 16);
    skinLayout->setSpacing(12);

    auto *skinLabel = new QLabel(u"Active skin"_s, m_skinGroup);
    skinLabel->setObjectName(u"fieldLabel"_s);
    skinLayout->addWidget(skinLabel);

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
    skinLayout->addWidget(m_skinCombo);

    m_importSkinBtn = new QPushButton(u"Import .cosmo skin\u2026"_s, m_skinGroup);
    m_importSkinBtn->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    connect(m_importSkinBtn, &QPushButton::clicked, this, &SettingsPage::onImportSkin);
    skinLayout->addWidget(m_importSkinBtn);

    layout->addWidget(m_skinGroup);


    return inner;
}

// ── Data section ─────────────────────────────────────────────────────────────

QWidget *SettingsPage::buildDataSection() {
    auto *inner = new QWidget;
    auto *layout = new QVBoxLayout(inner);
    layout->setContentsMargins(28, 28, 28, 28);
    layout->setSpacing(20);


    // ── Sound group ───────────────────────────────────────────────────────────
    m_soundGroup = new QGroupBox(u"Sound"_s, inner);
    auto *soundLayout = new QVBoxLayout(m_soundGroup);
    soundLayout->setContentsMargins(16, 20, 16, 16);
    soundLayout->setSpacing(10);

    m_uiSoundsCheck = new QCheckBox(u"Enable UI sounds"_s, m_soundGroup);
    m_uiSoundsCheck->setChecked(true);
    connect(m_uiSoundsCheck, &QCheckBox::toggled, this, &SettingsPage::onSoundsToggled);
    soundLayout->addWidget(m_uiSoundsCheck);

    auto *soundHint = new QLabel(
        u"Reserved for future alerts and feedback tones."_s, m_soundGroup);
    soundHint->setWordWrap(true);
    soundHint->setObjectName(u"mutedLabel"_s);
    soundLayout->addWidget(soundHint);

    layout->addWidget(m_soundGroup);
    layout->addStretch(1);

    return inner;
}

// ── Developer section ────────────────────────────────────────────────────────

QWidget *SettingsPage::buildDeveloperSection() {
    auto *inner = new QWidget;
    auto *layout = new QVBoxLayout(inner);
    layout->setContentsMargins(28, 28, 28, 28);
    layout->setSpacing(20);

    // ── Debug toggle ──────────────────────────────────────────────────────────
    m_debugModeCheck = new QCheckBox(u"Enable debug mode"_s, inner);
    m_debugModeCheck->setObjectName(u"debugModeCheck"_s);
    m_debugModeCheck->setToolTip(
        u"Expose verbose system information and developer reference."_s);
    connect(m_debugModeCheck, &QCheckBox::toggled,
            this, &SettingsPage::onDebugModeToggled);
    layout->addWidget(m_debugModeCheck);

    auto *debugHint = new QLabel(
        u"Enables system information and developer helpers below."_s, inner);
    debugHint->setObjectName(u"mutedLabel"_s);
    debugHint->setWordWrap(true);
    layout->addWidget(debugHint);

    auto *divider = new QFrame(inner);
    divider->setObjectName(u"divider"_s);
    divider->setFrameShape(QFrame::HLine);
    layout->addWidget(divider);

    // ── System information group ──────────────────────────────────────────────
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
    m_sysInfoLabel->setTextInteractionFlags(
        Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    sysLayout->addWidget(m_sysInfoLabel);
    layout->addWidget(m_sysInfoGroup);

    // ── Developer reference ───────────────────────────────────────────────────
    auto *helperGroup = new QGroupBox(u"Developer Reference"_s, inner);
    auto *helperLayout = new QVBoxLayout(helperGroup);
    helperLayout->setContentsMargins(16, 20, 16, 16);
    helperLayout->setSpacing(6);

    auto *helperText = new QPlainTextEdit(helperGroup);
    helperText->setObjectName(u"devReference"_s);
    helperText->setReadOnly(true);
    helperText->setMinimumHeight(200);
    helperText->setPlainText(
        u"\u2500\u2500\u2500 Workflow \u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\n"
        u"  1. Select serial port + baud on the Monitoring connection bar.\n"
        u"  2. Click Connect to start the live telemetry pipeline.\n"
        u"  3. Or click Open log\u2026 to load a .telem / .csv / .xlsx flight log.\n"
        u"  4. Switch to Flight data for charts and replay.\n"
        u"  5. Click Export session\u2026 to write a timestamped text log.\n"
        u"\n"
        u"\u2500\u2500\u2500 File formats \u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\n"
        u"  .telem   AltOS binary framed telemetry (Framer + Parser)\n"
        u"  .csv     Theseus CSV: time_s, lat, lon, alt_m, ...\n"
        u"  .xlsx    Excel workbook with telemetry columns on the first worksheet\n"
        u"\n"
        u"\u2500\u2500\u2500 Architecture overview \u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\n"
        u"  SerialWorker  \u2192 BlockingQueue \u2192 ParserWorker\n"
        u"                                   \u2193\n"
        u"  FlightDataModel \u2190 appendSample (Qt::QueuedConnection)\n"
        u"  FlightLogManager \u2190 appendSample (for export)\n"
        u"\n"
        u"\u2500\u2500\u2500 QSettings location (macOS) \u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\n"
        u"  ~/Library/Preferences/CosmoSoft.cosmo-soft.plist\n"
        u"\n"
        u"\u2500\u2500\u2500 QSettings keys \u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\n"
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
        u"\u2500\u2500\u2500 Useful build commands \u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\n"
        u"  cmake --preset debug\n"
        u"  cmake --build build/debug --target CosmoSoft -j\n"
        u"  cmake --preset release\n"
        u"  cmake --build build/release --target CosmoSoft -j\n"_s);
    helperLayout->addWidget(helperText);
    layout->addWidget(helperGroup);

    layout->addStretch(1);

    return inner;
}

// ── About section ────────────────────────────────────────────────────────────

QWidget *SettingsPage::buildAboutSection() {
    auto *inner = new QWidget;
    auto *layout = new QVBoxLayout(inner);
    layout->setContentsMargins(32, 32, 32, 32);
    layout->setSpacing(12);

    auto *appName = new QLabel(u"CosmoSoft"_s, inner);
    appName->setObjectName(u"aboutAppName"_s);
    layout->addWidget(appName);

    auto *version = new QLabel(u"Version 0.1.0 \u2014 development build"_s, inner);
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
    license->setObjectName(u"aboutLicense"_s);
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
    creditsHeading->setObjectName(u"sectionHeading"_s);
    layout->addWidget(creditsHeading);

    const QString qtLine =
        QStringLiteral("Qt %1  \u00b7  C++20").arg(QLatin1StringView(QT_VERSION_STR));
    auto *techLabel = new QLabel(qtLine, inner);
    techLabel->setObjectName(u"aboutDesc"_s);
    layout->addWidget(techLabel);

    layout->addStretch(1);

    return inner;
}

// ── Theme-reactive stylesheet ────────────────────────────────────────────────

void SettingsPage::refreshStyleSheet() {
    const QString sheet = QString(uR"(
    QWidget#settingsPage {
        background-color: %1;
    }

    /* ── Sidebar ─────────────────────────────────────────────── */
    QListWidget#settingsNav {
        background-color: %2;
        border-right: 1px solid %3;
        padding: 12px 8px;
        font-size: %4px;
    }
    QListWidget#settingsNav::item {
        color: %5;
        padding: 10px 14px;
        border-radius: %6px;
        margin: 1px 0px;
    }
    QListWidget#settingsNav::item:selected {
        color: %7;
        background-color: %8;
        font-weight: bold;
    }
    QListWidget#settingsNav::item:hover:!selected {
        background-color: %9;
        color: %7;
    }

    /* ── Content area ────────────────────────────────────────── */
    QScrollArea#sectionScroll {
        background-color: %1;
    }
    QScrollArea#sectionScroll > QWidget > QWidget {
        background-color: %1;
    }

    /* ── Group boxes ─────────────────────────────────────────── */
    QGroupBox {
        font-size: %10px;
        font-weight: bold;
        color: %7;
        border: 1px solid %3;
        border-radius: %11px;
        margin-top: 8px;
        padding-top: 16px;
    }
    QGroupBox::title {
        subcontrol-origin: margin;
        subcontrol-position: top left;
        left: 12px;
        padding: 0 6px;
        color: %7;
    }

    /* ── Labels ──────────────────────────────────────────────── */
    QLabel#fieldLabel {
        color: %5;
        font-size: %4px;
        font-weight: bold;
    }
    QLabel#mutedLabel {
        color: %5;
        font-size: %4px;
    }
    QLabel#sysInfoLabel {
        color: %12;
        font-size: %4px;
        background-color: %2;
        padding: 12px 16px;
        border-radius: %6px;
    }
    /* ── About labels ────────────────────────────────────────── */
    QLabel#aboutAppName {
        font-size: 26px;
        font-weight: bold;
        color: %7;
        letter-spacing: 2px;
    }
    QLabel#aboutVersion {
        color: %13;
        font-size: %10px;
    }
    QLabel#aboutDesc, QLabel#aboutLicense {
        color: %12;
        font-size: %4px;
    }
    QLabel#aboutLink {
        color: %13;
        font-size: %4px;
    }
    QLabel#sectionHeading {
        font-size: 18px;
        font-weight: bold;
        color: %7;
        letter-spacing: 1px;
    }

    /* ── Debug mode checkbox ─────────────────────────────────── */
    QCheckBox#debugModeCheck {
        font-size: %10px;
        font-weight: bold;
        color: %13;
        spacing: 10px;
    }
    QCheckBox#debugModeCheck::indicator {
        width: 18px;
        height: 18px;
        border: 1px solid %5;
        border-radius: 3px;
        background-color: %2;
    }
    QCheckBox#debugModeCheck::indicator:checked {
        background-color: %13;
        border-color: %13;
    }

    /* ── Developer reference ─────────────────────────────────── */
    QPlainTextEdit#devReference {
        background-color: %2;
        color: %15;
        font-family: %14;
        font-size: %4px;
        border: none;
        padding: 12px;
    }

    /* ── Dividers ────────────────────────────────────────────── */
    QFrame#divider {
        color: %3;
    }
)"_s)
    .arg(Theme::kBgBase())          // %1
    .arg(Theme::kBgDark())          // %2
    .arg(Theme::kBorderSubtle())    // %3
    .arg(Theme::kFontSizeBase)      // %4
    .arg(Theme::kTextMuted())       // %5
    .arg(Theme::kRadiusSm)          // %6
    .arg(Theme::kTextPrimary())     // %7
    .arg(Theme::kBgPanel())         // %8
    .arg(Theme::kBgInput())         // %9
    .arg(Theme::kFontSizeMd)        // %10
    .arg(Theme::kRadiusMd)          // %11
    .arg(Theme::kTextMid())         // %12
    .arg(Theme::kAccentLink())      // %13
    .arg(Theme::kFontMono)          // %14
    .arg(Theme::kSuccess());        // %15

    setStyleSheet(sheet);
}

void SettingsPage::onThemeChanged() {
    refreshStyleSheet();
}

// ── Slot implementations ─────────────────────────────────────────────────────

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

    startSkinImport(path, false);
}

void SettingsPage::startSkinImport(const QString &archive_path, bool replace_existing) {
    if (m_importSkinBtn) {
        m_importSkinBtn->setEnabled(false);
        m_importSkinBtn->setText(u"Importing…"_s);
    }

    const auto dest = cosmo::ThemeManager::skinsDirectory();
    auto *watcher = new QFutureWatcher<cosmo::SkinImportResult>(this);
    connect(watcher, &QFutureWatcher<cosmo::SkinImportResult>::finished,
            this, [this, watcher, archive_path]() {
        const cosmo::SkinImportResult result = watcher->result();
        watcher->deleteLater();

        if (m_importSkinBtn) {
            m_importSkinBtn->setEnabled(true);
            m_importSkinBtn->setText(u"Import .cosmo skin…"_s);
        }

        if (result.status == cosmo::SkinImportStatus::AlreadyExists) {
            QMessageBox prompt(QMessageBox::Question,
                               u"Replace installed skin?"_s,
                               result.error_message,
                               QMessageBox::Cancel,
                               this);
            auto *replace_button = prompt.addButton(u"Replace"_s, QMessageBox::AcceptRole);
            prompt.setDefaultButton(QMessageBox::Cancel);
            prompt.exec();
            if (prompt.clickedButton() == replace_button) {
                startSkinImport(archive_path, true);
            }
            return;
        }

        if (!result.succeeded()) {
            QMessageBox::warning(
                this,
                u"Could not import skin"_s,
                result.error_message.isEmpty()
                    ? u"The selected archive is not a valid CosmoSoft skin."_s
                    : result.error_message);
            return;
        }

        const auto &theme = *result.theme;
        int skin_index = m_skinCombo->findData(theme.id);
        {
            const QSignalBlocker blocker(m_skinCombo);
            if (skin_index < 0) {
                m_skinCombo->addItem(theme.name, theme.id);
                skin_index = m_skinCombo->count() - 1;
            } else {
                m_skinCombo->setItemText(skin_index, theme.name);
            }
            m_skinCombo->setCurrentIndex(skin_index);
        }
        cosmo::ThemeManager::instance().setActiveSkin(theme);
    });

    watcher->setFuture(QtConcurrent::run(
        [archive_path, dest, replace_existing]() {
            return cosmo::SkinLoader::importArchiveDetailed(
                archive_path, dest, replace_existing);
        }));
}

// ── Persistence ──────────────────────────────────────────────────────────────

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

    const int activeRow = s.value(kSettingsActiveTab, 0).toInt();
    if (m_nav && activeRow >= 0 && activeRow < m_nav->count()) {
        m_nav->setCurrentRow(activeRow);
    }

    const QByteArray geo = s.value(kSettingsWindowSettingsGeo).toByteArray();
    if (!geo.isEmpty()) {
        restoreGeometry(geo);
    }
}

void SettingsPage::saveToSettings() {
    QSettings s(kSettingsOrg, kSettingsApp);
    if (m_uiSoundsCheck) {
        s.setValue(kSettingsSoundsEnabled, m_uiSoundsCheck->isChecked());
    }
    if (m_debugModeCheck) {
        s.setValue(kSettingsDebugMode, m_debugModeCheck->isChecked());
    }
    if (m_nav) {
        s.setValue(kSettingsActiveTab, m_nav->currentRow());
    }
    s.setValue(kSettingsWindowSettingsGeo, saveGeometry());
}
