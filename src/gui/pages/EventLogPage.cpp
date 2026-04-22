#include "gui/pages/EventLogPage.h"

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

    setStyleSheet(uR"(
        #eventLogPage {
            background-color: #161618;
        }
        #eventLogEdit {
            background-color: #161618;
            color: #d0d0d0;
            font-family: "Red Hat Mono", "Courier New", "Roboto Mono", monospace;
            font-size: 12px;
            border: none;
            padding: 12px 16px;
        }
        #eventLogFooter {
            background-color: #1e1e20;
            border-top: 1px solid rgba(255, 255, 255, 0.06);
        }
        #eventLogClearBtn {
            border: 1px solid #5a5a5a;
            border-radius: 4px;
            padding: 4px 12px;
            min-height: 26px;
            background-color: #2e2e32;
            color: #c0c0c0;
            font-size: 11px;
            font-family: "Red Hat Mono", "Courier New", monospace;
        }
        #eventLogClearBtn:hover  { background-color: #3a3a3e; }
        #eventLogClearBtn:pressed { background-color: #222224; }
    )"_s);

    appendEntry(u"Event log started."_s);
}

void EventLogPage::appendEntry(const QString &text) {
    if (!m_log) {
        return;
    }
    const QString ts = QDateTime::currentDateTime().toString(u"HH:mm:ss"_s);
    m_log->appendHtml(
        QStringLiteral("<span style=\"color:#6a8fa0\">[%1]</span>&nbsp;"
                       "<span style=\"color:#d0d0d0\">%2</span>")
            .arg(ts.toHtmlEscaped(), text.toHtmlEscaped()));
}

void EventLogPage::appendError(const QString &text) {
    if (!m_log) {
        return;
    }
    const QString ts = QDateTime::currentDateTime().toString(u"HH:mm:ss"_s);
    m_log->appendHtml(
        QStringLiteral("<span style=\"color:#6a8fa0\">[%1]</span>&nbsp;"
                       "<span style=\"color:#e05555\">ERROR: %2</span>")
            .arg(ts.toHtmlEscaped(), text.toHtmlEscaped()));
}

void EventLogPage::onClearLog() {
    if (m_log) {
        m_log->clear();
    }
}
