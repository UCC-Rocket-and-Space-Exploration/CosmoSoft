#include "pages/DashboardPage.h"

#include <QLabel>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

DashboardPage::DashboardPage(QWidget *parent)
        : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(12);

    auto *heading = new QLabel(u"Flight Overview"_s, this);
    heading->setStyleSheet(u"font-size: 22px; font-weight: bold;"_s);
    heading->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(heading);

    auto *missionSummary = new QLabel(
            u"• Mission Name: Demo Launch\n"
            u"• Vehicle Status: Nominal\n"
            u"• Last Telemetry Packet: <pending backend>"_s,
            this);
    missionSummary->setStyleSheet(u"background-color: #1a2238; color: #f0f4ff; padding: 12px; border-radius: 8px;"_s);
    missionSummary->setWordWrap(true);
    layout->addWidget(missionSummary);

    auto *environmentSummary = new QLabel(
            u"Env Snapshot\n"
            u"- Temperature: <placeholder>\n"
            u"- Pressure: <placeholder>\n"
            u"- Wind Shift: <placeholder>"_s,
            this);
    environmentSummary->setStyleSheet(u"background-color: #243b53; color: #d9e2ec; padding: 12px; border-radius: 8px;"_s);
    environmentSummary->setWordWrap(true);
    layout->addWidget(environmentSummary);

    auto *nextSteps = new QLabel(
            u"Next Actions Checklist\n"
            u"1. Verify payload state.\n"
            u"2. Arm telemetry recorder.\n"
            u"3. Confirm launch window."_s,
            this);
    nextSteps->setStyleSheet(u"background-color: #102a43; color: #cbd2d9; padding: 12px; border-radius: 8px;"_s);
    nextSteps->setWordWrap(true);
    layout->addWidget(nextSteps);

    layout->addStretch(1);
}
