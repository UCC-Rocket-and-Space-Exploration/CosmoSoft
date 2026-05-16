#include "gui/pages/LiveTelemetryPage.h"

#include "gui/Theme.h"
#include "gui/ThemeManager.h"

#include <QLabel>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

LiveTelemetryPage::LiveTelemetryPage(QWidget *parent)
    : QWidget(parent) {
    setObjectName(u"liveTelemetryPage"_s);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_placeholderLabel = new QLabel(u"Live Telemetry \u2014 coming soon"_s, this);
    m_placeholderLabel->setObjectName(u"liveTelemetryPlaceholder"_s);
    m_placeholderLabel->setAlignment(Qt::AlignCenter);

    root->addWidget(m_placeholderLabel, 1);

    refreshStyleSheet();
    connect(&cosmo::ThemeManager::instance(), &cosmo::ThemeManager::themeChanged,
            this, &LiveTelemetryPage::refreshStyleSheet);
}

void LiveTelemetryPage::refreshStyleSheet() {
    setStyleSheet(
        QString(uR"(
        #liveTelemetryPage {
            background-color: %1;
        }
        #liveTelemetryPlaceholder {
            color: %2;
            font-family: %3;
            font-size: 16px;
        }
    )"_s)
            .arg(Theme::kBgBase())
            .arg(Theme::kTextMuted())
            .arg(Theme::kFontMono));
}
