/**
 * @file Map3DWidget.h
 * @brief 3D globe map widget embedding CesiumJS via QWebEngineView.
 *
 * Map3DWidget provides the same public API as the former MapChartWidget so it
 * can be dropped into DashboardPage as a replacement.  Communication with the
 * CesiumJS viewer running inside the web engine is handled through a
 * QWebChannel bridge object (Map3DBridge).
 *
 * Supports:
 *  - Live mode: samples pushed one at a time via onSampleUpdated().
 *  - Replay mode: full session loaded via setReplaySession(), trail controlled
 *    by setReplayTrailLength().
 *  - Camera follow: auto-tracks the rocket entity during replay.
 *  - Offline tile caching via QWebEngineUrlRequestInterceptor.
 */

#ifndef COSMO_SOFT_MAP3DWIDGET_H
#define COSMO_SOFT_MAP3DWIDGET_H

#include <QWidget>

#include <vector>

#include "domain/FlightSample.h"
#include "domain/FlightSession.h"

class QWebEngineView;
class QWebChannel;
class Map3DBridge;
class TileCacheInterceptor;

/**
 * @class Map3DWidget
 * @brief QWebEngineView wrapper rendering a CesiumJS 3D globe with flight path.
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
    void setReplaySession(const FlightSession *session);

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

    /** @brief Clears all data and resets to idle state. */
    void onSessionReset();

private slots:
    void onBridgeStatsUpdated(double lat, double lon, double alt,
                              double distFromLaunch, double bearing,
                              double pathLength,
                              int totalSamples, int validGpsSamples,
                              double launchLat, double launchLon);
    void onMapReady();
    void pushThemeToMap();

private:
    void runJs(const QString &js);
    void sendPendingSession();

    QWebEngineView       *m_webView   = nullptr;
    QWebChannel          *m_channel   = nullptr;
    Map3DBridge          *m_bridge    = nullptr;
    TileCacheInterceptor *m_tileCache = nullptr;

    const FlightSession  *m_session   = nullptr;
    bool m_mapReady   = false;
    bool m_sessionPending = false;
    bool m_followEnabled  = false;

    std::vector<FlightSample> m_liveSamples;
    int m_totalLiveSamples = 0;
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

    /** @brief Emitted by JS when the CesiumJS viewer is fully initialized. */
    void ready();

    /** @brief Emitted when the user toggles camera follow from the in-map button. */
    void followChanged(bool enabled);

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
