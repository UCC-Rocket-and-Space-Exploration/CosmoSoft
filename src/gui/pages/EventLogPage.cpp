#include "gui/pages/EventLogPage.h"

#include "gui/Theme.h"
#include "gui/ThemeManager.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

EventLogPage::EventLogPage(QWidget *parent)
    : QWidget(parent) {
    setObjectName(u"eventLogPage"_s);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setObjectName(u"eventLogEdit"_s);
    m_log->setMaximumBlockCount(5000);
    m_log->setLineWrapMode(QPlainTextEdit::NoWrap);

    auto *footer = new QWidget(this);
    footer->setObjectName(u"eventLogFooter"_s);
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(12, 6, 12, 6);
    footerLayout->addStretch(1);

    m_clearBtn = new QPushButton(u"Clear log"_s, footer);
    m_clearBtn->setObjectName(u"eventLogClearBtn"_s);
    footerLayout->addWidget(m_clearBtn);

    root->addWidget(m_log, 1);
    root->addWidget(footer);

    connect(m_clearBtn, &QPushButton::clicked, this, &EventLogPage::onClearLog);

    refreshStyleSheet();
    connect(&cosmo::ThemeManager::instance(), &cosmo::ThemeManager::themeChanged,
            this, &EventLogPage::refreshStyleSheet);

    appendEntry(u"Event log started."_s);
}

void EventLogPage::refreshStyleSheet() {
    setStyleSheet(
        QString(uR"(
        #eventLogPage {
            background-color: %1;
        }
        #eventLogEdit {
            background-color: %1;
            color: %2;
            font-family: %3;
            font-size: %4px;
            border: none;
            padding: 12px 16px;
        }
        #eventLogFooter {
            background-color: %5;
            border-top: 1px solid %6;
        }
        #eventLogClearBtn {
            border: 1px solid %7;
            border-radius: %8px;
            padding: 4px 12px;
            min-height: 26px;
            background-color: %9;
            color: %2;
            font-size: %10px;
            font-family: %3;
        }
        #eventLogClearBtn:hover  { background-color: %11; }
        #eventLogClearBtn:pressed { background-color: %12; }
    )"_s)
            .arg(Theme::kBgDark())          // %1
            .arg(Theme::kTextPrimary())     // %2
            .arg(Theme::kFontMono)          // %3
            .arg(Theme::kFontSizeBase)      // %4
            .arg(Theme::kBgPanel())         // %5
            .arg(Theme::kBorderSubtle())    // %6
            .arg(Theme::kBorderDefault())   // %7
            .arg(Theme::kRadiusSm)          // %8
            .arg(Theme::kBgButton())        // %9
            .arg(Theme::kFontSizeSm)        // %10
            .arg(Theme::kBtnHover())        // %11
            .arg(Theme::kBtnPressed()));    // %12
}

void EventLogPage::appendEntry(const QString &text) {
    if (!m_log) {
        return;
    }
    const QString ts = QDateTime::currentDateTime().toString(u"HH:mm:ss"_s);
    m_log->appendHtml(
        QStringLiteral("<span style=\"color:%1\">[%2]</span>&nbsp;"
                       "<span style=\"color:%3\">%4</span>")
            .arg(Theme::kTextDim(), ts.toHtmlEscaped(),
                 Theme::kTextPrimary(), text.toHtmlEscaped()));
}

void EventLogPage::appendError(const QString &text) {
    if (!m_log) {
        return;
    }
    const QString ts = QDateTime::currentDateTime().toString(u"HH:mm:ss"_s);
    m_log->appendHtml(
        QStringLiteral("<span style=\"color:%1\">[%2]</span>&nbsp;"
                       "<span style=\"color:%3\">ERROR: %4</span>")
            .arg(Theme::kTextDim(), ts.toHtmlEscaped(),
                 Theme::kError(), text.toHtmlEscaped()));
}

void EventLogPage::onClearLog() {
    if (m_log) {
        m_log->clear();
    }
}
