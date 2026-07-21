/**
 * @file EventLogPage.h
 * @brief Scrollable event and error log page for CosmoSoft.
 *
 * EventLogPage maintains a read-only, timestamped log of:
 *  - Parser decode errors and serial I/O errors
 *  - Session lifecycle events (connection opened/closed, file loaded, export saved)
 *
 * Entries are appended via appendEntry() which can be called from any thread
 * using Qt::QueuedConnection.
 */

#ifndef COSMO_SOFT_EVENTLOGPAGE_H
#define COSMO_SOFT_EVENTLOGPAGE_H

#include <QDateTime>
#include <QString>
#include <QWidget>

#include <cstddef>
#include <deque>

class QPlainTextEdit;
class QPushButton;
class QTextCursor;

/**
 * @class EventLogPage
 * @brief Presents a monospace, timestamped log of session events and errors.
 *
 * The widget owns a QPlainTextEdit in read-only mode. New entries are retained
 * as timestamp, message, and severity data so their colours can be rebuilt when
 * the active theme changes. At most 2,000 single-line, size-bounded entries are
 * retained. A "Clear" button empties both the retained entries and the
 * rendered log.
 */
class EventLogPage : public QWidget {
    Q_OBJECT

public:
    explicit EventLogPage(QWidget *parent = nullptr);
    ~EventLogPage() override = default;

public slots:
    /**
     * @brief Appends a timestamped informational entry to the log.
     * @param text Human-readable event description.
     */
    void appendEntry(const QString &text);

    /**
     * @brief Appends a timestamped error entry to the log (shown in red).
     * @param text Human-readable error description.
     */
    void appendError(const QString &text);

private slots:
    void onClearLog();
    void refreshStyleSheet();

private:
    enum class Severity {
        Information,
        Error,
    };

    struct LogEntry {
        QDateTime timestamp;
        QString text;
        Severity severity = Severity::Information;
    };

    static constexpr std::size_t kMaxRetainedEntries = 2000U;

    void append(const QString &text, Severity severity);
    void renderEntry(const LogEntry &entry, QTextCursor &cursor);
    void rebuildLog();

    QPlainTextEdit *m_log       = nullptr;
    QPushButton    *m_clearBtn  = nullptr;
    std::deque<LogEntry> m_entries;
};

#endif // COSMO_SOFT_EVENTLOGPAGE_H
