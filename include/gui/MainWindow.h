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

#include <deque>
#include <memory>
#include <utility>
#include <vector>

#include "domain/FlightSession.h"
#include "services/BlockingQueue.h"

class QAction;
class QComboBox;
class QGraphicsDropShadowEffect;
class QLabel;
class QMenu;
class QPushButton;
class QStackedWidget;
class QTimer;
class DashboardPage;
class LiveTelemetryPage;
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
    void applyPendingReplayTelemetryStrip();
    void onReplayPositionChanged(int trailLength);
    void onOpenReplayFile();
    void onExportSession();
    void onClearFlightData();
    void onShowAbout();
    void onThemeChanged();
    void onToggleFakeTransmission();
    void pushFakeTransmissionSample();

private:
    void setupActions();
    void setupMenuBar();
    void setupToolbar();
    void setupConnectionBar();
    [[nodiscard]] QString buildToolbarStyleSheet();
    [[nodiscard]] QString buildActionBarStyleSheet();
    void setupPages();
    void openSettingsWindow();
    void applyReplayTelemetrySample(int trailLength);

    /**
     * @brief Load a flight log asynchronously, showing a progress dialog.
     *
     * Handles CSV and .telem formats via QtConcurrent.  On success the loaded
     * session is installed into the model, replay controller, and dashboard.
     * The file is also added to the recent-files list.
     *
     * @param path Absolute path to the flight-log file.
     */
    void loadFlightLogAsync(const QString &path);

    /**
     * @brief Appends a log entry to the persistent in-memory buffer.
     * @param isError When true the entry is rendered as an error (red).
     * @param text    Human-readable message.
     */
    void appendToLog(bool isError, const QString &text);

    void addRecentFile(const QString &path);
    void rebuildRecentFilesMenu();
    void updateBreadcrumb(const QString &context = QString());
    void updateBrandShadowColor();
    void stopFakeTransmission(bool completed = false);
    void refreshFakeTransmissionButton();

    // ── Menu bar ─────────────────────────────────────────────────────────────
    QMenu *m_recentFilesMenu = nullptr;

    // ── Toolbar actions ───────────────────────────────────────────────────────
    QAction *m_openSettingsAction    = nullptr;
    QAction *m_dashboardAction       = nullptr;
    QAction *m_liveTelemetryAction   = nullptr;

    // ── Page stack ────────────────────────────────────────────────────────────
    QStackedWidget     *m_pages             = nullptr;
    DashboardPage      *m_flightDataPage    = nullptr;
    LiveTelemetryPage  *m_liveTelemetryPage = nullptr;
    SettingsPage       *m_settingsWindow    = nullptr;

    // ── Toolbar labels ────────────────────────────────────────────────────────
    QLabel *m_brandLabel        = nullptr;
    QLabel *m_missionMetaLabel  = nullptr;
    QTimer *m_missionClockTimer = nullptr;
    QGraphicsDropShadowEffect *m_brandShadow = nullptr;

    // ── Connection bar ────────────────────────────────────────────────────────
    QWidget  *m_connectionBar       = nullptr;
    QLabel   *m_connectionPageLabel = nullptr;
    QPushButton *m_openLogBtn     = nullptr;
    QPushButton *m_clearFlightBtn = nullptr;
    QPushButton *m_exportBtn      = nullptr;
    QPushButton *m_fakeTransmissionBtn = nullptr;

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

    // ── Timers ────────────────────────────────────────────────────────────────
    QTimer *m_replayTelemetryCoalesceTimer = nullptr;
    int     m_pendingReplayTelemetryTrail  = 0;
    QTimer *m_fakeTransmissionTimer = nullptr;
    std::vector<FlightSample> m_fakeTransmissionSamples;
    std::vector<qint64> m_fakeTransmissionByteCounts;
    std::size_t m_fakeTransmissionIndex = 0;

    /**
     * Persistent log buffer.  Every entry is stored here so that the settings
     * window can be closed and reopened without losing history.  The bool is
     * true for errors, false for informational entries.
     */
    std::deque<std::pair<bool, QString>> m_logEntries;
};

#endif // COSMO_SOFT_MAINWINDOW_H
