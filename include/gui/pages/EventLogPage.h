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

#include <QWidget>

class QPlainTextEdit;
class QPushButton;

/**
 * @class EventLogPage
 * @brief Presents a monospace, timestamped log of session events and errors.
 *
 * The widget owns a QPlainTextEdit in read-only mode.  New entries are prefixed
 * with a local timestamp (HH:mm:ss) and coloured red for errors or grey for
 * informational events.  A "Clear" button at the bottom empties the log.
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
    QPlainTextEdit *m_log       = nullptr;
    QPushButton    *m_clearBtn  = nullptr;
};

#endif // COSMO_SOFT_EVENTLOGPAGE_H
