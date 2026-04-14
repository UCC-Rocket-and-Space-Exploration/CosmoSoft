#include "gui/pages/SettingsPage.h"

#include <QApplication>
#include <QCloseEvent>
#include <QCheckBox>
#include <QComboBox>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QSettings>
#include <QShowEvent>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace {
constexpr auto kOrg = "CosmoSoft";
constexpr auto kApp = "cosmo-soft";
} // namespace

SettingsPage::SettingsPage(QWidget *parent)
    : QWidget(parent) {
    setObjectName(u"settingsPage"_s);
    buildUi();
}

void SettingsPage::buildUi() {
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(u"settingsScroll"_s);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *inner = new QWidget(scroll);
    inner->setObjectName(u"settingsInner"_s);
    auto *layout = new QVBoxLayout(inner);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(20);

    auto *heading = new QLabel(u"Settings"_s, inner);
    heading->setObjectName(u"settingsHeading"_s);
    layout->addWidget(heading);

    auto *intro = new QLabel(
        u"Appearance and audio preferences. On the main window, use the connection bar (under the toolbar) for serial and flight logs."_s,
        inner);
    intro->setWordWrap(true);
    intro->setObjectName(u"settingsMutedLabel"_s);
    layout->addWidget(intro);

    m_fontGroup = new QGroupBox(u"Font"_s, inner);
    auto *fontForm = new QFormLayout(m_fontGroup);
    fontForm->setSpacing(10);
    m_fontSizeCombo = new QComboBox(m_fontGroup);
    for (int pt = 9; pt <= 18; ++pt) {
        m_fontSizeCombo->addItem(QString::number(pt), pt);
    }
    m_fontSizeCombo->setCurrentIndex(3);
    connect(m_fontSizeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SettingsPage::onFontSizeChanged);
    fontForm->addRow(u"UI font size (pt):"_s, m_fontSizeCombo);
    layout->addWidget(m_fontGroup);

    m_soundGroup = new QGroupBox(u"Sound"_s, inner);
    auto *soundLayout = new QVBoxLayout(m_soundGroup);
    m_uiSoundsCheck = new QCheckBox(u"Enable UI sounds (when available)"_s, m_soundGroup);
    m_uiSoundsCheck->setChecked(true);
    connect(m_uiSoundsCheck, &QCheckBox::toggled, this, &SettingsPage::onSoundsToggled);
    soundLayout->addWidget(m_uiSoundsCheck);
    auto *soundHint = new QLabel(
        u"Reserved for future alerts and feedback tones."_s,
        m_soundGroup);
    soundHint->setWordWrap(true);
    soundHint->setObjectName(u"settingsMutedLabel"_s);
    soundLayout->addWidget(soundHint);
    layout->addWidget(m_soundGroup);

    layout->addStretch(1);

    scroll->setWidget(inner);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(scroll);

    setStyleSheet(uR"(
        #settingsPage {
            background-color: #1f1f1f;
            color: #f8f8f8;
            font-family: "Red Hat Mono", "Courier New", "Roboto Mono", monospace;
        }
        #settingsScroll {
            border: none;
            background-color: #1f1f1f;
        }
        #settingsInner {
            background-color: #1f1f1f;
        }
        QLabel#settingsHeading {
            font-size: 22px;
            font-weight: bold;
            color: #f8f8f8;
        }
        QLabel#settingsMutedLabel {
            color: #9aa7b8;
        }
        QGroupBox {
            font-weight: 600;
            color: #f8f8f8;
            border: 1px solid #4d4d4d;
            border-radius: 8px;
            margin-top: 12px;
            padding-top: 12px;
            background-color: #2b2d33;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 12px;
            padding: 0 6px;
            color: #f8f8f8;
        }
        QLabel {
            color: #f8f8f8;
        }
        QComboBox, QLineEdit {
            background-color: #1a1a1a;
            color: #f5f5f5;
            border: 2px solid #4d4d4d;
            border-radius: 4px;
            padding: 4px 8px;
        }
        QComboBox::drop-down {
            border: none;
            width: 24px;
        }
        QComboBox QAbstractItemView {
            background-color: #2b2d33;
            color: #f5f5f5;
            selection-background-color: #4b4b4b;
            border: 1px solid #4d4d4d;
        }
        QCheckBox {
            color: #f8f8f8;
            spacing: 8px;
        }
        QCheckBox::indicator {
            width: 18px;
            height: 18px;
        }
        QPushButton {
            border: 1px solid #6a6a6a;
            border-radius: 4px;
            padding: 4px 10px;
            min-height: 28px;
            background-color: #3d3f47;
            color: #f0f0f0;
            font-size: 11px;
        }
        QPushButton:hover {
            background-color: #4d4f57;
        }
        QPushButton:pressed {
            background-color: #2d2f37;
        }
        QScrollBar:vertical {
            background: #2a2a2a;
            width: 12px;
            margin: 0;
        }
        QScrollBar::handle:vertical {
            background: #5a5a5a;
            min-height: 24px;
            border-radius: 4px;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0;
        }
    )"_s);
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
        QSettings s(kOrg, kApp);
        s.setValue(u"ui/fontPointSize"_s, pt);
    }
}

void SettingsPage::onSoundsToggled(bool enabled) {
    QSettings s(kOrg, kApp);
    s.setValue(u"ui/soundsEnabled"_s, enabled);
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
    QSettings s(kOrg, kApp);
    const int pt = s.value(u"ui/fontPointSize"_s, 12).toInt();
    if (m_fontSizeCombo) {
        const int idx = m_fontSizeCombo->findData(pt);
        if (idx >= 0) {
            m_fontSizeCombo->blockSignals(true);
            m_fontSizeCombo->setCurrentIndex(idx);
            m_fontSizeCombo->blockSignals(false);
        }
        applyFontPointSize(pt);
    }
    if (m_uiSoundsCheck) {
        m_uiSoundsCheck->setChecked(s.value(u"ui/soundsEnabled"_s, true).toBool());
    }

    const QByteArray geo = s.value(u"window/settingsGeometry"_s).toByteArray();
    if (!geo.isEmpty()) {
        restoreGeometry(geo);
    }
}

void SettingsPage::saveToSettings() {
    QSettings s(kOrg, kApp);
    if (m_fontSizeCombo) {
        const int pt = m_fontSizeCombo->currentData().toInt();
        if (pt > 0) {
            s.setValue(u"ui/fontPointSize"_s, pt);
        }
    }
    if (m_uiSoundsCheck) {
        s.setValue(u"ui/soundsEnabled"_s, m_uiSoundsCheck->isChecked());
    }
    s.setValue(u"window/settingsGeometry"_s, saveGeometry());
}
