/**
 * @file MainWindow.h
 * @brief Top-level application window for CosmoSoft.
 *
 * MainWindow owns the mission toolbar, connection bar, telemetry strip, and the
 * page stack (Dashboard, Live Telemetry, and Event Log). It also owns the
 * live-telemetry pipeline (SerialWorker → bounded decoder worker → batched
 * FlightDataModel updates) and the replay pipeline (FlightReplayController).
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

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "domain/FlightSession.h"
#include "services/preview/FlightPreviewCache.h"

class QAction;
class QComboBox;
class QLabel;
class QMenu;
class QProgressDialog;
class QPushButton;
class QResizeEvent;
class QStackedWidget;
class QTimer;
class QToolBar;
class QToolButton;
class DashboardPage;
class EventLogPage;
class LiveTelemetryPage;
class SettingsPage;
class FlightDataModel;
class FlightReplayController;
class FlightLogManager;
class SerialWorker;
class IComms;

namespace cosmo::telemetry {
class LineTelemetryBatchMailbox;
class LineTelemetryDecodeWorker;
}

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

protected:
    /** @brief Reflow toolbar controls when the application window is resized. */
    void resizeEvent(QResizeEvent *event) override;

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
    void onScanLiveDevices();
    void onConnectLiveDevice(const QString &portName, int baud);
    void onDisconnectLiveDevice();
    void onStartLiveDemo();

private:
    void setupActions();
    void setupMenuBar();
    void setupToolbar();
    void setupConnectionBar();
    [[nodiscard]] QString buildToolbarStyleSheet();
    [[nodiscard]] QString buildActionBarStyleSheet();
    void updateSettingsIcon();
    void updateToolbarLayout();
    void setupPages();
    void openSettingsWindow();
    void applyReplayTelemetrySample(int trailLength);
    void cancelFlightLogLoad(bool showStatus);

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
     * @brief Appends a semantic entry to the bounded Event Log page.
     * @param isError When true the entry uses the active theme's error style.
     * @param text    Human-readable message.
     */
    void appendToLog(bool isError, const QString &text);

    /** @brief Play a rate-limited platform alert when UI sounds are enabled. */
    void playErrorFeedback();

    /** @brief Retain one live sample or report the recording-memory bound once. */
    void recordLiveSample(const FlightSample &sample);

    void addRecentFile(const QString &path);
    void rebuildRecentFilesMenu();
    void updateBreadcrumb(const QString &context = QString());
    void stopFakeTransmission(bool completed = false);
    void refreshFakeTransmissionButton();
    void startSerial(const QString &portName, int baud);
    void stopSerial();
    void prepareLiveSession(const QString &context);
    void drainLiveTelemetryBatches(std::uint64_t generation);

    // ── Menu bar ─────────────────────────────────────────────────────────────
    QMenu *m_recentFilesMenu = nullptr;

    // ── Toolbar actions ───────────────────────────────────────────────────────
    QAction *m_openSettingsAction    = nullptr;
    QAction *m_dashboardAction       = nullptr;
    QAction *m_liveTelemetryAction   = nullptr;
    QAction *m_eventLogAction        = nullptr;

    // ── Page stack ────────────────────────────────────────────────────────────
    QStackedWidget     *m_pages             = nullptr;
    DashboardPage      *m_flightDataPage    = nullptr;
    LiveTelemetryPage  *m_liveTelemetryPage = nullptr;
    EventLogPage        *m_eventLogPage      = nullptr;
    SettingsPage       *m_settingsWindow    = nullptr;

    // ── Toolbar labels ────────────────────────────────────────────────────────
    QLabel *m_brandLabel        = nullptr;
    QLabel *m_missionMetaLabel  = nullptr;
    QTimer *m_missionClockTimer = nullptr;
    QToolBar *m_missionToolbar = nullptr;
    QWidget *m_toolbarContent = nullptr;
    QWidget *m_brandBlock = nullptr;
    QToolButton *m_liveNavButton = nullptr;
    QToolButton *m_dashboardNavButton = nullptr;
    QToolButton *m_eventLogNavButton = nullptr;
    QToolButton *m_settingsNavButton = nullptr;
    int m_toolbarLayoutMode = -1;

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
    std::shared_ptr<const FlightSession> m_loadedSession;
    std::shared_ptr<const cosmo::preview::FlightPreviewCache> m_loadedPreview;

    // Each asynchronous operation has an independent generation. Completion
    // handlers accept results only from the most recently started generation.
    std::uint64_t m_loadGeneration = 0;
    std::uint64_t m_exportGeneration = 0;
    std::uint64_t m_scanGeneration = 0;
    std::shared_ptr<std::atomic_bool> m_loadCancelFlag;
    std::shared_ptr<std::atomic_bool> m_scanCancelFlag;
    QProgressDialog *m_loadProgress = nullptr;
    bool m_exportInProgress = false;

    // ── Live-telemetry pipeline ───────────────────────────────────────────────
    std::unique_ptr<cosmo::telemetry::LineTelemetryBatchMailbox> m_liveBatchMailbox;
    std::unique_ptr<cosmo::telemetry::LineTelemetryDecodeWorker> m_lineDecodeWorker;
    std::unique_ptr<SerialWorker> m_serialWorker;
    std::unique_ptr<IComms> m_comms;
    std::uint64_t m_liveGeneration = 0;
    QString m_serialPortSummary;

    // ── Timers ────────────────────────────────────────────────────────────────
    QTimer *m_replayTelemetryCoalesceTimer = nullptr;
    int     m_pendingReplayTelemetryTrail  = 0;
    QTimer *m_fakeTransmissionTimer = nullptr;
    std::vector<FlightSample> m_fakeTransmissionSamples;
    std::vector<qint64> m_fakeTransmissionByteCounts;
    std::size_t m_fakeTransmissionIndex = 0;

    bool m_uiSoundsEnabled = true;
    bool m_hasPlayedErrorFeedback = false;
    bool m_recordingLimitReported = false;
    std::chrono::steady_clock::time_point m_lastErrorFeedback;
};

#endif // COSMO_SOFT_MAINWINDOW_H
