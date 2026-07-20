#include "gui/pages/EventLogPage.h"

#include "gui/Theme.h"
#include "gui/ThemeManager.h"

#include <QColor>
#include <QDateTime>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QVBoxLayout>

#include <algorithm>

using namespace Qt::StringLiterals;

EventLogPage::EventLogPage(QWidget *parent)
    : QWidget(parent) {
    setObjectName(u"eventLogPage"_s);
    setAccessibleName(u"Event log"_s);
    setAccessibleDescription(
        u"Chronological session events and errors. Use the Clear log button to remove all entries."_s);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setObjectName(u"eventLogEdit"_s);
    m_log->setAccessibleName(u"Event log entries"_s);
    m_log->setAccessibleDescription(
        u"Read-only chronological session log. Error entries begin with ERROR."_s);
    m_log->setFocusPolicy(Qt::StrongFocus);
    m_log->setTabChangesFocus(true);
    m_log->setMaximumBlockCount(static_cast<int>(kMaxRetainedEntries));
    m_log->setLineWrapMode(QPlainTextEdit::NoWrap);
    setFocusProxy(m_log);

    auto *footer = new QWidget(this);
    footer->setObjectName(u"eventLogFooter"_s);
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(12, 6, 12, 6);
    footerLayout->addStretch(1);

    m_clearBtn = new QPushButton(u"Clear log"_s, footer);
    m_clearBtn->setObjectName(u"eventLogClearBtn"_s);
    m_clearBtn->setAccessibleName(u"Clear event log"_s);
    m_clearBtn->setAccessibleDescription(
        u"Remove all currently retained event and error entries."_s);
    m_clearBtn->setToolTip(u"Clear all event log entries"_s);
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
        #eventLogEdit:focus {
            border: 2px solid %13;
            padding: 10px 14px;
        }
        #eventLogClearBtn:focus {
            border: 2px solid %13;
        }
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
            .arg(Theme::kBtnPressed())      // %12
            .arg(Theme::kFocusRing()));     // %13

    rebuildLog();
}

void EventLogPage::appendEntry(const QString &text) {
    append(text, Severity::Information);
}

void EventLogPage::appendError(const QString &text) {
    append(text, Severity::Error);
}

void EventLogPage::append(const QString &text, const Severity severity) {
    if (!m_log) {
        return;
    }

    LogEntry entry{QDateTime::currentDateTime(), text, severity};
    m_entries.push_back(entry);
    if (m_entries.size() > kMaxRetainedEntries) {
        m_entries.pop_front();
    }

    QScrollBar *scroll_bar = m_log->verticalScrollBar();
    const bool was_at_end = scroll_bar == nullptr
        || scroll_bar->value() >= scroll_bar->maximum();
    QTextCursor cursor(m_log->document());
    cursor.movePosition(QTextCursor::End);
    renderEntry(entry, cursor);
    if (was_at_end && scroll_bar != nullptr) {
        scroll_bar->setValue(scroll_bar->maximum());
    }
}

void EventLogPage::renderEntry(const LogEntry &entry, QTextCursor &cursor) {
    QTextDocument *document = cursor.document();
    if (!document->isEmpty()) {
        cursor.insertBlock();
    }

    QTextCharFormat timestamp_format;
    timestamp_format.setForeground(QColor(Theme::kTextDim()));
    cursor.insertText(
        u"[%1] "_s.arg(entry.timestamp.toString(u"HH:mm:ss"_s)),
        timestamp_format);

    QTextCharFormat message_format;
    message_format.setForeground(QColor(
        entry.severity == Severity::Error
            ? Theme::kError()
            : Theme::kTextPrimary()));
    const QString prefix = entry.severity == Severity::Error
        ? u"ERROR: "_s
        : QString{};
    QString display_text = entry.text;
    // Keep one document block per retained entry so the view and semantic
    // deque apply the same bound even when a diagnostic contains line breaks.
    display_text.replace(u"\r\n"_s, u" "_s);
    display_text.replace(u'\r', u' ');
    display_text.replace(u'\n', u' ');
    display_text.replace(u'\u2028', u' ');
    display_text.replace(u'\u2029', u' ');
    cursor.insertText(prefix + display_text, message_format);
}

void EventLogPage::rebuildLog() {
    if (!m_log) {
        return;
    }

    QScrollBar *scroll_bar = m_log->verticalScrollBar();
    QScrollBar *horizontal_scroll_bar = m_log->horizontalScrollBar();
    const int previous_value = scroll_bar != nullptr ? scroll_bar->value() : 0;
    const int previous_horizontal_value = horizontal_scroll_bar != nullptr
        ? horizontal_scroll_bar->value()
        : 0;
    const bool was_at_end = scroll_bar == nullptr
        || previous_value >= scroll_bar->maximum();
    const QTextCursor previous_cursor = m_log->textCursor();
    const bool updates_were_enabled = m_log->updatesEnabled();
    if (updates_were_enabled) {
        m_log->setUpdatesEnabled(false);
    }

    m_log->clear();
    QTextCursor render_cursor(m_log->document());
    render_cursor.beginEditBlock();
    for (const LogEntry &entry : m_entries) {
        renderEntry(entry, render_cursor);
    }
    render_cursor.endEditBlock();

    const int maximum_cursor_position = std::max(
        0, m_log->document()->characterCount() - 1);
    QTextCursor restored_cursor(m_log->document());
    restored_cursor.setPosition(std::clamp(
        previous_cursor.anchor(), 0, maximum_cursor_position));
    restored_cursor.setPosition(
        std::clamp(previous_cursor.position(), 0, maximum_cursor_position),
        QTextCursor::KeepAnchor);
    m_log->setTextCursor(restored_cursor);

    if (scroll_bar != nullptr) {
        scroll_bar->setValue(
            was_at_end
                ? scroll_bar->maximum()
                : std::min(previous_value, scroll_bar->maximum()));
    }
    if (horizontal_scroll_bar != nullptr) {
        horizontal_scroll_bar->setValue(std::min(
            previous_horizontal_value, horizontal_scroll_bar->maximum()));
    }
    if (updates_were_enabled) {
        m_log->setUpdatesEnabled(true);
        m_log->update();
    }
}

void EventLogPage::onClearLog() {
    if (m_log) {
        m_entries.clear();
        m_log->clear();
    }
}
