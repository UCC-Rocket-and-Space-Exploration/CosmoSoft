/**
 * @file MainWindow.h
 * @brief Top-level application window for CosmoSoft.
 *
 * MainWindow owns the mission toolbar, connection bar, telemetry strip, and the
 * page stack (DashboardPage).  It also owns the
 * live-telemetry pipeline (SerialWorker → BlockingQueue → ParserWorker →
 * FlightDataModel) and the replay pipeline (FlightReplayController).
 *
 * Responsibilities:
 *  - Serial port management: scan, connect, disconnect.
 *  - Flight-log loading (CSV / .telem) via QtConcurrent and QProgressDialog.
 *  - Session recording and export via FlightLogManager.
 *  - Routing parsed samples and errors to the data model.
 *  - Keeping the toolbar, connection bar, and telemetry strip in sync with the
 *    active page and connection/replay state.
 */

#ifndef COSMO_SOFT_MAINWINDOW_H
#define COSMO_SOFT_MAINWINDOW_H

#include <QMainWindow>
#include <QString>

#include <memory>
#include <utility>
#include <vector>

#include "domain/FlightSession.h"
#include "services/BlockingQueue.h"

class QAction;
class QComboBox;
class QLabel;
class QMenu;
class QStackedWidget;
class QTimer;
class DashboardPage;
class SettingsPage;
class FlightDataModel;
class FlightReplayController;
class FlightLogManager;
class ParserWorker;
class SerialWorker;
class IComms;

/** @brief Top-level application window. */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    /**
     * @brief Display a transient message in the status bar.
     * @param message Text to show.
     * @param timeout Duration in milliseconds; 0 = persistent until next message.
     */
    void showStatusMessage(const QString &message, int timeout = 0);

public slots:
    /** @brief Receives parser and serial error strings and shows them in the status bar. */
    void onParserError(const QString &message);

private slots:
    void updateMissionClock();
    void updateDataRateLabel();
    void applyPendingReplayTelemetryStrip();
    void refreshSerialPorts();
    void startSerial(const QString &portName, int baud);
    void stopSerial();
    void onReplayPositionChanged(int trailLength);
    void onOpenReplayFile();
    void onExportSession();
    void onClearFlightData();
    void onShowAbout();
    void onThemeChanged();

private:
    void setupActions();
    void setupMenuBar();
    void setupToolbar();
    void setupDataBar();
    void setupConnectionBar();
    [[nodiscard]] QString buildToolbarStyleSheet();
    [[nodiscard]] QString buildDataBarStyleSheet();
    void loadSerialPrefsToUi();
    void persistSerialPrefs();
    void setupPages();
    void openSettingsWindow();
    void updateTopBarsForCurrentPage();
    void syncTelemetryStrip();
    void applyReplayTelemetrySample(int trailLength);

    /**
     * @brief Appends a log entry to the persistent buffer and forwards it to
     *        the settings window (if open).
     * @param isError When true the entry is rendered as an error (red).
     * @param text    Human-readable message.
     */
    void appendToLog(bool isError, const QString &text);

    void addRecentFile(const QString &path);
    void rebuildRecentFilesMenu();

    // ── Menu bar ─────────────────────────────────────────────────────────────
    QMenu *m_recentFilesMenu = nullptr;

    // ── Toolbar actions ───────────────────────────────────────────────────────
    QAction *m_openSettingsAction    = nullptr;

    // ── Page stack ────────────────────────────────────────────────────────────
    QStackedWidget *m_pages           = nullptr;
    DashboardPage  *m_flightDataPage  = nullptr;
    SettingsPage   *m_settingsWindow  = nullptr;

    // ── Toolbar labels ────────────────────────────────────────────────────────
    QLabel *m_brandLabel        = nullptr;
    QLabel *m_missionMetaLabel  = nullptr;
    QLabel *m_toolbarPageLabel  = nullptr;
    QTimer *m_missionClockTimer = nullptr;

    // ── Connection bar ────────────────────────────────────────────────────────
    QWidget  *m_connectionBar       = nullptr;
    QLabel   *m_connectionPageLabel = nullptr;
    QWidget  *m_serialControlBlock  = nullptr;
    QComboBox *m_portCombo          = nullptr;
    QComboBox *m_baudCombo          = nullptr;

    // ── Telemetry strip ───────────────────────────────────────────────────────
    QWidget *m_dataBar             = nullptr;
    QLabel  *m_dataStripPageLabel  = nullptr;
    QLabel  *m_dataLinkStatusLabel = nullptr;
    QLabel  *m_dataRateLabel       = nullptr;
    QLabel  *m_droppedBadgeLabel   = nullptr;

    // ── Data model and replay ─────────────────────────────────────────────────
    std::unique_ptr<FlightDataModel>        m_flightModel;
    std::unique_ptr<FlightReplayController> m_replay;
    std::unique_ptr<FlightLogManager>       m_logManager;
    FlightSession m_loadedSession;

    // ── Live-telemetry pipeline ───────────────────────────────────────────────
    BlockingQueue<std::vector<uint8_t>> m_rawQueue{512};
    std::unique_ptr<ParserWorker>  m_parserWorker;
    std::unique_ptr<SerialWorker>  m_serialWorker;
    std::unique_ptr<IComms>        m_comms;

    // ── Timers and rate tracking ──────────────────────────────────────────────
    QTimer *m_dataRateTimer                = nullptr;
    QTimer *m_replayTelemetryCoalesceTimer = nullptr;
    int     m_pendingReplayTelemetryTrail  = 0;
    qint64  m_prevBytesForRate             = 0;

    /** Last dropped-packet count seen; used to suppress redundant badge updates. */
    std::size_t m_lastDroppedCount = 0;

    /**
     * Persistent log buffer.  Every entry is stored here so that the settings
     * window can be closed and reopened without losing history.  The bool is
     * true for errors, false for informational entries.
     */
    std::vector<std::pair<bool, QString>> m_logEntries;

    QString m_serialPortSummary;
};

#endif // COSMO_SOFT_MAINWINDOW_H
