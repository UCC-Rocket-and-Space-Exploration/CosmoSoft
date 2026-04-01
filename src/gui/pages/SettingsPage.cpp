#include "gui/pages/SettingsPage.h"

#include <QCloseEvent>
#include <QDir>
#include <QFrame>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
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

    m_connectionGroup = new QGroupBox(u"Serial connection"_s, inner);
    auto *connForm = new QFormLayout(m_connectionGroup);
    connForm->setSpacing(10);

    auto *portRow = new QHBoxLayout();
    m_portCombo = new QComboBox(m_connectionGroup);
    m_portCombo->setMinimumWidth(260);
    m_portCombo->setEditable(true);
    auto *refreshBtn = new QPushButton(u"Refresh ports"_s, m_connectionGroup);
    connect(refreshBtn, &QPushButton::clicked, this, &SettingsPage::refreshPortsRequested);
    portRow->addWidget(m_portCombo, 1);
    portRow->addWidget(refreshBtn);
    connForm->addRow(u"Port:"_s, portRow);

    m_baudCombo = new QComboBox(m_connectionGroup);
    m_baudCombo->addItems({u"9600"_s, u"19200"_s, u"38400"_s, u"57600"_s, u"115200"_s, u"921600"_s});
    m_baudCombo->setCurrentText(u"115200"_s);
    connForm->addRow(u"Baud rate:"_s, m_baudCombo);

    m_linkStatus = new QLabel(u"Disconnected."_s, m_connectionGroup);
    m_linkStatus->setObjectName(u"settingsMutedLabel"_s);
    connForm->addRow(u"Status:"_s, m_linkStatus);

    auto *btnRow = new QHBoxLayout();
    auto *connectBtn = new QPushButton(u"Connect"_s, m_connectionGroup);
    auto *disconnectBtn = new QPushButton(u"Disconnect"_s, m_connectionGroup);
    btnRow->addWidget(connectBtn);
    btnRow->addWidget(disconnectBtn);
    btnRow->addStretch(1);
    connForm->addRow(btnRow);

    connect(connectBtn, &QPushButton::clicked, this, [this]() {
        saveConnectionFields();
        const QString port = m_portCombo->currentText().trimmed();
        const int baud = m_baudCombo->currentText().toInt();
        emit connectRequested(port, baud);
    });
    connect(disconnectBtn, &QPushButton::clicked, this, &SettingsPage::disconnectRequested);

    connect(m_portCombo, &QComboBox::editTextChanged, this, &SettingsPage::saveConnectionFields);
    connect(m_baudCombo, &QComboBox::currentTextChanged, this, &SettingsPage::saveConnectionFields);

    layout->addWidget(m_connectionGroup);

    m_replayGroup = new QGroupBox(u"Flight replay"_s, inner);
    auto *replayLayout = new QVBoxLayout(m_replayGroup);
    replayLayout->setSpacing(10);

    auto *replayHint = new QLabel(
        u"Load Theseus CSV or TELEM logs for offline playback on the Flight data page."_s,
        m_replayGroup);
    replayHint->setWordWrap(true);
    replayHint->setObjectName(u"settingsMutedLabel"_s);
    replayLayout->addWidget(replayHint);

    auto *dirRow = new QHBoxLayout();
    m_replayDirEdit = new QLineEdit(m_replayGroup);
    m_replayDirEdit->setPlaceholderText(u"Default folder for file picker"_s);
    auto *browseDirBtn = new QPushButton(u"Choose folder…"_s, m_replayGroup);
    connect(browseDirBtn, &QPushButton::clicked, this, [this]() {
        const QString start = m_replayDirEdit->text().trimmed();
        const QString dir = QFileDialog::getExistingDirectory(
            this,
            u"Replay files folder"_s,
            start.isEmpty() ? QDir::homePath() : start);
        if (!dir.isEmpty()) {
            m_replayDirEdit->setText(dir);
            QSettings s(kOrg, kApp);
            s.setValue(u"paths/replayDir"_s, dir);
        }
    });
    dirRow->addWidget(m_replayDirEdit, 1);
    dirRow->addWidget(browseDirBtn);
    replayLayout->addLayout(dirRow);

    auto *replayBtnRow = new QHBoxLayout();
    auto *openBtn = new QPushButton(u"Open flight log…"_s, m_replayGroup);
    auto *clearBtn = new QPushButton(u"Clear loaded flight"_s, m_replayGroup);
    connect(openBtn, &QPushButton::clicked, this, &SettingsPage::openReplayFileRequested);
    connect(clearBtn, &QPushButton::clicked, this, &SettingsPage::clearFlightDataRequested);
    replayBtnRow->addWidget(openBtn);
    replayBtnRow->addWidget(clearBtn);
    replayBtnRow->addStretch(1);
    replayLayout->addLayout(replayBtnRow);

    layout->addWidget(m_replayGroup);

    m_appGroup = new QGroupBox(u"Application"_s, inner);
    auto *appLayout = new QVBoxLayout(m_appGroup);
    auto *appNote = new QLabel(u"Preferences are stored automatically when this window closes."_s, m_appGroup);
    appNote->setWordWrap(true);
    appNote->setObjectName(u"settingsMutedLabel"_s);
    appLayout->addWidget(appNote);
    layout->addWidget(m_appGroup);

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
        QPushButton {
            border: 2px solid #cfcfcf;
            border-radius: 4px;
            padding: 6px 14px;
            background-color: #4d4f57;
            color: #f0f0f0;
        }
        QPushButton:hover {
            background-color: #5c5e66;
        }
        QPushButton:pressed {
            background-color: #3d3f47;
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

QString SettingsPage::replayDirectory() const {
    if (!m_replayDirEdit) {
        return {};
    }
    return m_replayDirEdit->text().trimmed();
}

void SettingsPage::setPortNames(const QStringList &ports) {
    if (!m_portCombo) {
        return;
    }
    const QString prev = m_portCombo->currentText();
    m_portCombo->clear();
    m_portCombo->addItems(ports);
    if (!prev.isEmpty()) {
        const int idx = m_portCombo->findText(prev);
        if (idx >= 0) {
            m_portCombo->setCurrentIndex(idx);
        } else {
            m_portCombo->setEditText(prev);
        }
    }
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
    const QString port = s.value(u"serial/port"_s).toString();
    if (!port.isEmpty() && m_portCombo) {
        const int idx = m_portCombo->findText(port);
        if (idx >= 0) {
            m_portCombo->setCurrentIndex(idx);
        } else {
            m_portCombo->setEditText(port);
        }
    }
    const QString baud = s.value(u"serial/baud"_s, u"115200"_s).toString();
    if (m_baudCombo) {
        const int bi = m_baudCombo->findText(baud);
        if (bi >= 0) {
            m_baudCombo->setCurrentIndex(bi);
        }
    }
    const QString rdir = s.value(u"paths/replayDir"_s, QDir::homePath()).toString();
    if (m_replayDirEdit) {
        m_replayDirEdit->setText(rdir);
    }

    const QByteArray geo = s.value(u"window/settingsGeometry"_s).toByteArray();
    if (!geo.isEmpty()) {
        restoreGeometry(geo);
    }
}

void SettingsPage::saveToSettings() {
    QSettings s(kOrg, kApp);
    if (m_portCombo) {
        s.setValue(u"serial/port"_s, m_portCombo->currentText().trimmed());
    }
    if (m_baudCombo) {
        s.setValue(u"serial/baud"_s, m_baudCombo->currentText());
    }
    if (m_replayDirEdit) {
        s.setValue(u"paths/replayDir"_s, m_replayDirEdit->text().trimmed());
    }
    s.setValue(u"window/settingsGeometry"_s, saveGeometry());
}

void SettingsPage::setSerialLinkStatus(const QString &text) {
    if (m_linkStatus) {
        m_linkStatus->setText(text);
    }
}

void SettingsPage::saveConnectionFields() {
    QSettings s(kOrg, kApp);
    if (m_portCombo) {
        s.setValue(u"serial/port"_s, m_portCombo->currentText().trimmed());
    }
    if (m_baudCombo) {
        s.setValue(u"serial/baud"_s, m_baudCombo->currentText());
    }
}
