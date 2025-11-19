#include "pages/FlightDataPage.h"

#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

SettingsPage::SettingsPage(QWidget *parent)
        : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(12);

    auto *heading = new QLabel(u"Ground Station Settings"_s, this);
    heading->setStyleSheet(u"font-size: 22px; font-weight: bold;"_s);
    layout->addWidget(heading);

    auto *intro = new QLabel(
            u"Adjust serial connection parameters and UI preferences. "
            u"Everything is mocked until the backend is wired."_s,
            this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *serialLabel = new QLabel(u"Serial Connection"_s, this);
    serialLabel->setStyleSheet(u"font-weight: bold;"_s);
    layout->addWidget(serialLabel);

    auto *serialPanel = new QWidget(this);
    auto *serialGrid = new QGridLayout(serialPanel);
    serialGrid->setColumnStretch(1, 1);
    serialGrid->addWidget(new QLabel(u"Port"_s, serialPanel), 0, 0);
    serialGrid->addWidget(new QComboBox(serialPanel), 0, 1);
    serialGrid->addWidget(new QLabel(u"Baud"_s, serialPanel), 1, 0);
    serialGrid->addWidget(new QComboBox(serialPanel), 1, 1);
    serialGrid->addWidget(new QPushButton(u"Scan Ports"_s, serialPanel), 2, 0);
    serialGrid->addWidget(new QPushButton(u"Connect"_s, serialPanel), 2, 1);
    serialPanel->setStyleSheet(u"background-color: #f5f7fa; border: 1px solid #d9e2ec; border-radius: 6px; padding: 12px;"_s);
    layout->addWidget(serialPanel);

    auto *appearanceLabel = new QLabel(u"Appearance"_s, this);
    appearanceLabel->setStyleSheet(u"font-weight: bold;"_s);
    layout->addWidget(appearanceLabel);

    auto *appearancePanel = new QWidget(this);
    auto *appearanceGrid = new QGridLayout(appearancePanel);
    appearanceGrid->addWidget(new QLabel(u"Theme"_s, appearancePanel), 0, 0);
    appearanceGrid->addWidget(new QComboBox(appearancePanel), 0, 1);
    appearanceGrid->addWidget(new QLabel(u"Font Size"_s, appearancePanel), 1, 0);
    auto *fontSlider = new QSlider(Qt::Horizontal, appearancePanel);
    fontSlider->setRange(10, 24);
    fontSlider->setValue(14);
    appearanceGrid->addWidget(fontSlider, 1, 1);
    appearancePanel->setStyleSheet(u"background-color: #f5f7fa; border: 1px solid #d9e2ec; border-radius: 6px; padding: 12px;"_s);
    layout->addWidget(appearancePanel);

    auto *footer = new QLabel(
            u"Hint: connect these widgets to backend services once serial plumbing is ready."_s,
            this);
    footer->setStyleSheet(u"color: #52606d; font-style: italic;"_s);
    footer->setWordWrap(true);
    layout->addWidget(footer);

    layout->addStretch(1);
}
