#include "pages/SettingsPage.h"

#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

SettingsPage::SettingsPage(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 32, 32, 32);
    layout->setSpacing(18);

    auto *heading = new QLabel(u"Settings"_s, this);
    heading->setStyleSheet(u"font-size: 26px; font-weight: bold;"_s);
    layout->addWidget(heading);

    auto *intro = new QLabel(
            u"Adjust mission control preferences. These widgets are placeholders until backend wiring is connected."_s,
            this);
    intro->setWordWrap(true);
    intro->setStyleSheet(u"color: #4a5568;"_s);
    layout->addWidget(intro);

    auto *card = new QFrame(this);
    card->setObjectName(u"settingsCard"_s);
    card->setStyleSheet(uR"(
        QFrame#settingsCard {
            background: #f7fafc;
            border: 1px solid #d1d9e6;
            border-radius: 10px;
            padding: 24px;
        }
    )"_s);
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setSpacing(12);

    auto *cardTitle = new QLabel(u"General Controls"_s, card);
    cardTitle->setStyleSheet(u"font-size: 18px; font-weight: 600;"_s);
    cardLayout->addWidget(cardTitle);

    auto *note = new QLabel(
            u"Preferences will live here soon. For now, use the toolbar settings button to manage UI tweaks."_s,
            card);
    note->setWordWrap(true);
    note->setStyleSheet(u"color: #4a5568;"_s);
    cardLayout->addWidget(note);

    layout->addWidget(card);
    layout->addStretch(1);
}
