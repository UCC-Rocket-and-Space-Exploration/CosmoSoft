/**
 * @file Map3DWidget.h
 * @brief Flight-map widget embedding Leaflet and Three.js via QWebEngineView.
 *
 * Map3DWidget provides the same public API as the former MapChartWidget so it
 * can be dropped into DashboardPage as a replacement.  Communication with the
 * map renderer running inside the web engine is handled through a
 * QWebChannel bridge object (Map3DBridge).
 *
 * Supports:
 *  - Live mode: samples pushed in batches via onLiveSamplesReceived().
 *  - Replay mode: full session loaded via setReplaySession(), trail controlled
 *    by setReplayTrailLength().
 *  - Camera follow: auto-tracks the rocket entity during replay.
 *  - Offline tile caching via QWebEngineUrlRequestInterceptor.
 */

#ifndef COSMO_SOFT_MAP3DWIDGET_H
#define COSMO_SOFT_MAP3DWIDGET_H

#include <QWidget>
#include <QVector>
#include <QVariantMap>

#include <memory>
#include <vector>

#include "domain/FlightSample.h"
#include "domain/FlightSession.h"

namespace cosmo::preview {
class FlightPreviewCache;
}

class QWebEngineView;
class QWebChannel;
class QLabel;
class QPushButton;
class QTimer;
class QHideEvent;
class QShowEvent;
class Map3DBridge;
class TileCacheInterceptor;

/**
 * @class Map3DWidget
 * @brief QWebEngineView wrapper rendering 2D/3D flight paths.
 */
class Map3DWidget : public QWidget {
    Q_OBJECT

public:
    explicit Map3DWidget(QWidget *parent = nullptr);
    ~Map3DWidget() override = default;

    /**
     * @brief Switches to replay mode with the given session.
     * Pass nullptr to return to live mode and clear the map.
     */
    void setReplaySession(std::shared_ptr<const FlightSession> session,
                          std::shared_ptr<const cosmo::preview::FlightPreviewCache> preview);

    /** @brief Rebuilds the visible trail to show the first @p trailLength samples. */
    void setReplayTrailLength(int trailLength);

    /** @brief Fits the camera to the full extent of the recorded path. */
    void fitPath();

    /** @brief Flies the camera to the current rocket position. */
    void centerOnCurrent();

    /** @brief Enables or disables automatic camera tracking of the rocket. */
    void setCameraFollow(bool enabled);

signals:
    /**
     * @brief Emitted whenever the rocket position changes.
     *
     * Signal signature matches the former MapChartWidget for drop-in compatibility.
     */
    void positionStatsChanged(double lat, double lon, double alt,
                              double distFromLaunch, double bearing,
                              double pathLength,
                              int totalSamples, int validGpsSamples,
                              double launchLat, double launchLon);

public slots:
    /** @brief Appends a live sample; ignored while a replay session is loaded. */
    void onSampleUpdated(const FlightSample &sample);

    /** @brief Appends a batch of live samples with one Qt signal delivery. */
    void onLiveSamplesReceived(const QVector<FlightSample> &samples);

    /** @brief Clears all data and resets to idle state. */
    void onSessionReset();

private slots:
    void onBridgeStatsUpdated(double lat, double lon, double alt,
                              double distFromLaunch, double bearing,
                              double pathLength,
                              int totalSamples, int validGpsSamples,
                              double launchLat, double launchLon);
    void onMapReady();
    void onMapLoadFinished(bool succeeded);
    void pushThemeToMap();
    void flushPendingUpdates();

private:
    struct IndexedLiveSample {
        FlightSample sample;
        int sampleIndex = 0;
    };

    void ensureMapInitialized();
    void loadMapPage();
    void showMapLoadError(const QString &message);
    void sendPendingSession();
    void sendPendingLiveSamples();
    void sendPendingTrailLength();
    void scheduleMapUpdate();
    void compactLiveHistory();

protected:
    /** @brief Lazily initializes and catches up the embedded map when shown. */
    void showEvent(QShowEvent *event) override;

    /** @brief Suspends map updates and 3D animation while hidden. */
    void hideEvent(QHideEvent *event) override;

private:
    QWebEngineView       *m_webView   = nullptr;
    QWebChannel          *m_channel   = nullptr;
    Map3DBridge          *m_bridge    = nullptr;
    TileCacheInterceptor *m_tileCache = nullptr;
    QWidget              *m_loadError = nullptr;
    QLabel               *m_loadErrorLabel = nullptr;
    QPushButton          *m_retryButton = nullptr;
    QTimer               *m_readyWatchdog = nullptr;
    QTimer               *m_updateTimer = nullptr;

    std::shared_ptr<const FlightSession> m_session;
    std::shared_ptr<const cosmo::preview::FlightPreviewCache> m_preview;
    bool m_mapReady   = false;
    bool m_loadAttemptActive = false;
    bool m_sessionPending = false;
    bool m_clearPending = false;
    bool m_liveUpdatePending = false;
    bool m_liveSnapshotPending = true;
    bool m_trailUpdatePending = false;
    bool m_followUpdatePending = true;
    bool m_followEnabled  = false;
    int m_pendingTrailLength = 0;
    int m_lastPublishedLiveIndex = -1;
    int m_publishedLive3DPointCount = 0;
    int m_liveTotalSamples = 0;

    std::vector<IndexedLiveSample> m_liveSamples;
};

/**
 * @class Map3DBridge
 * @brief QObject exposed to JS via QWebChannel for bidirectional C++/JS calls.
 */
class Map3DBridge : public QObject {
    Q_OBJECT

public:
    explicit Map3DBridge(QObject *parent = nullptr) : QObject(parent) {}

signals:
    /** @brief Emitted by JS when stats are recomputed after a map update. */
    void statsUpdated(double lat, double lon, double alt,
                      double distFromLaunch, double bearing,
                      double pathLength,
                      int totalSamples, int validGpsSamples,
                      double launchLat, double launchLon);

    /** @brief Emitted by JS when the embedded map is fully initialized. */
    void ready();

    /** @brief Emitted when the user toggles camera follow from the in-map button. */
    void followChanged(bool enabled);

    /** @brief Sends a validated theme palette to the embedded map. */
    void themeChanged(const QVariantMap &theme);

    /** @brief Sends a complete replay-session payload to the embedded map. */
    void sessionLoaded(const QVariantMap &session);

    /** @brief Updates the replay trail and its exact current point. */
    void trailLengthChanged(int trailLength, const QVariantMap &currentPoint);

    /** @brief Appends or replaces a bounded batch of validated live points. */
    void liveBatchAdded(const QVariantMap &batch);

    /** @brief Notifies the page whether its host widget is visible. */
    void hostVisibilityChanged(bool visible);

    /** @brief Requests that the embedded map clear all flight data. */
    void clearRequested();

    /** @brief Requests that the embedded map fit the complete path. */
    void fitRequested();

    /** @brief Requests that the embedded map center on the current point. */
    void centerRequested();

    /** @brief Requests a camera-follow state change. */
    void followRequested(bool enabled);

public:
    /** @brief Publishes a theme without evaluating JavaScript source code. */
    void publishTheme(const QVariantMap &theme) { emit themeChanged(theme); }

    /** @brief Publishes a replay session without evaluating JavaScript source code. */
    void publishSession(const QVariantMap &session) { emit sessionLoaded(session); }

    /** @brief Publishes a replay position without evaluating JavaScript source code. */
    void publishTrailLength(int trailLength, const QVariantMap &currentPoint) {
        emit trailLengthChanged(trailLength, currentPoint);
    }

    /** @brief Publishes one bounded live batch without evaluating JavaScript source code. */
    void publishLiveBatch(const QVariantMap &batch) { emit liveBatchAdded(batch); }

    /** @brief Publishes host visibility so the renderer can suspend animation. */
    void publishHostVisibility(bool visible) { emit hostVisibilityChanged(visible); }

    /** @brief Publishes a map-clear command. */
    void requestClear() { emit clearRequested(); }

    /** @brief Publishes a fit-path command. */
    void requestFit() { emit fitRequested(); }

    /** @brief Publishes a center-current command. */
    void requestCenter() { emit centerRequested(); }

    /** @brief Publishes a camera-follow command. */
    void requestFollow(bool enabled) { emit followRequested(enabled); }

public slots:
    /** @brief Called from JS to push updated position stats to C++. */
    void onStatsUpdated(double lat, double lon, double alt,
                        double distFromLaunch, double bearing,
                        double pathLength,
                        int totalSamples, int validGpsSamples,
                        double launchLat, double launchLon) {
        emit statsUpdated(lat, lon, alt, distFromLaunch, bearing,
                          pathLength, totalSamples, validGpsSamples,
                          launchLat, launchLon);
    }

    /** @brief Called from JS when the viewer is ready to receive data. */
    void mapReady() { emit ready(); }

    /** @brief Called from JS when the user toggles the follow button on the map. */
    void onFollowChanged(bool enabled) { emit followChanged(enabled); }
};

#endif // COSMO_SOFT_MAP3DWIDGET_H
