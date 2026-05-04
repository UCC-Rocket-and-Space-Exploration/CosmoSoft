#include "gui/widgets/Map3DWidget.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QWebChannel>
#include <QWebEngineProfile>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineView>

#include <cmath>
#include <fstream>
#include <chrono>

namespace {

bool isValidCoord(double lat, double lon) {
    return std::isfinite(lat) && std::isfinite(lon)
        && !(std::abs(lat) < 1e-9 && std::abs(lon) < 1e-9);
}

} // namespace

/**
 * @class TileCacheInterceptor
 * @brief Intercepts outgoing tile requests to serve from local disk cache.
 *
 * Tiles fetched from the network are cached to disk on first access.
 * Subsequent loads serve the cached copy without hitting the network.
 * The cache directory lives under QStandardPaths::CacheLocation / "map_tiles".
 */
class TileCacheInterceptor : public QWebEngineUrlRequestInterceptor {
    Q_OBJECT

public:
    explicit TileCacheInterceptor(QObject *parent = nullptr)
        : QWebEngineUrlRequestInterceptor(parent)
    {
        m_cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                   + QStringLiteral("/map_tiles");
        QDir().mkpath(m_cacheDir);
    }

    void interceptRequest(QWebEngineUrlRequestInfo &info) override {
        Q_UNUSED(info);
    }

    /** @brief Returns the path to the tile cache directory. */
    [[nodiscard]] QString cacheDir() const { return m_cacheDir; }

private:
    QString m_cacheDir;
};

// #region agent log helper
static void dbgLog(const char* loc, const char* msg, const std::string& extra = "") {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    std::ofstream f("/Users/hslyusar/Desktop/CosmoSoft/.cursor/debug-7994bc.log", std::ios::app);
    f << "{\"sessionId\":\"7994bc\",\"location\":\"" << loc << "\",\"message\":\"" << msg << "\",\"data\":{" << extra << "},\"timestamp\":" << ms << "}\n";
}
// #endregion

Map3DWidget::Map3DWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_tileCache = new TileCacheInterceptor(this);

    auto *profile = new QWebEngineProfile(this);
    profile->setPersistentStoragePath(m_tileCache->cacheDir());
    profile->setHttpCacheType(QWebEngineProfile::DiskHttpCache);
    profile->setHttpCacheMaximumSize(256 * 1024 * 1024);
    profile->setUrlRequestInterceptor(m_tileCache);

    // #region agent log — capture JS console messages
    class DebugPage : public QWebEnginePage {
    public:
        using QWebEnginePage::QWebEnginePage;
    protected:
        void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level, const QString &msg, int line, const QString &src) override {
            Q_UNUSED(level);
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            std::ofstream f("/Users/hslyusar/Desktop/CosmoSoft/.cursor/debug-7994bc.log", std::ios::app);
            std::string m = msg.toStdString();
            for (auto &c : m) { if (c == '"') c = '\''; if (c == '\\') c = '/'; if (c == '\n') c = ' '; }
            f << "{\"sessionId\":\"7994bc\",\"location\":\"JS-console:" << line << "\",\"message\":\"js-console\",\"data\":{\"msg\":\"" << m << "\",\"src\":\"" << src.toStdString() << "\"},\"timestamp\":" << ms << "}\n";
        }
    };
    // #endregion
    auto *page = new DebugPage(profile, this);
    page->settings()->setAttribute(QWebEngineSettings::WebGLEnabled, true);
    page->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
    page->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);

    m_bridge = new Map3DBridge(this);
    m_channel = new QWebChannel(this);
    m_channel->registerObject(QStringLiteral("map3dBridge"), m_bridge);
    page->setWebChannel(m_channel);

    connect(m_bridge, &Map3DBridge::statsUpdated,
            this, &Map3DWidget::onBridgeStatsUpdated);
    connect(m_bridge, &Map3DBridge::ready,
            this, &Map3DWidget::onMapReady);

    m_webView = new QWebEngineView(this);
    m_webView->setPage(page);
    layout->addWidget(m_webView);

    QFile htmlFile(QStringLiteral(":/map/map3d.html"));
    bool opened = htmlFile.open(QIODevice::ReadOnly);
    // #region agent log — constructor: HTML resource load
    dbgLog("Map3DWidget.cpp:ctor", "html-resource-open", "\"opened\":" + std::string(opened ? "true" : "false") + ",\"size\":" + std::to_string(opened ? htmlFile.size() : 0));
    // #endregion
    if (opened) {
        const QString html = QString::fromUtf8(htmlFile.readAll());
        page->setHtml(html, QUrl(QStringLiteral("qrc:///map/")));
    }
}

void Map3DWidget::runJs(const QString &js) {
    if (m_webView && m_webView->page()) {
        m_webView->page()->runJavaScript(js);
    }
}

void Map3DWidget::onMapReady() {
    m_mapReady = true;
    // #region agent log — H4: C++ onMapReady
    dbgLog("Map3DWidget.cpp:onMapReady", "onMapReady-called", "\"sessionPending\":" + std::string(m_sessionPending ? "true" : "false") + ",\"liveSamples\":" + std::to_string(m_liveSamples.size()));
    // #endregion
    if (m_sessionPending) {
        sendPendingSession();
    }
    if (!m_liveSamples.empty()) {
        for (const auto &s : m_liveSamples) {
            if (isValidCoord(s.coordinates.latitude, s.coordinates.longitude)) {
                runJs(QStringLiteral("addPoint(%1,%2,%3,%4)")
                    .arg(s.coordinates.latitude, 0, 'f', 9)
                    .arg(s.coordinates.longitude, 0, 'f', 9)
                    .arg(s.altitude, 0, 'f', 2)
                    .arg(s.timestamp));
            }
        }
    }
}

void Map3DWidget::setReplaySession(const FlightSession *session) {
    // #region agent log — setReplaySession entry
    dbgLog("Map3DWidget.cpp:setReplaySession", "called",
        "\"hasSession\":" + std::string(session ? "true" : "false") +
        ",\"sampleCount\":" + std::to_string(session ? session->samples.size() : 0) +
        ",\"mapReady\":" + std::string(m_mapReady ? "true" : "false"));
    // #endregion
    m_session = session;
    m_totalLiveSamples = 0;
    m_liveSamples.clear();

    if (!session || session->samples.empty()) {
        if (m_mapReady) {
            runJs(QStringLiteral("clearAll()"));
        }
        emit positionStatsChanged(0, 0, 0, -1, -1, 0, 0, 0, 0, 0);
        return;
    }

    if (m_mapReady) {
        sendPendingSession();
    } else {
        m_sessionPending = true;
    }
}

void Map3DWidget::sendPendingSession() {
    m_sessionPending = false;
    if (!m_session || m_session->samples.empty()) {
        // #region agent log
        dbgLog("Map3DWidget.cpp:sendPendingSession", "no-session-or-empty");
        // #endregion
        return;
    }

    QJsonArray arr;
    for (const auto &s : m_session->samples) {
        QJsonObject obj;
        obj[QStringLiteral("lat")] = s.coordinates.latitude;
        obj[QStringLiteral("lon")] = s.coordinates.longitude;
        obj[QStringLiteral("alt")] = s.altitude;
        obj[QStringLiteral("ts")]  = static_cast<double>(s.timestamp);
        arr.append(obj);
    }

    const QByteArray jsonBytes = QJsonDocument(arr).toJson(QJsonDocument::Compact);
    const QString json = QString::fromUtf8(jsonBytes);

    QString escaped = json;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escaped.replace(QLatin1Char('\''), QStringLiteral("\\'"));
    // #region agent log — H1: C++ sendPendingSession
    {
        int validGps = 0;
        for (const auto &s : m_session->samples)
            if (isValidCoord(s.coordinates.latitude, s.coordinates.longitude)) ++validGps;
        dbgLog("Map3DWidget.cpp:sendPendingSession", "sending-loadSession",
            "\"sampleCount\":" + std::to_string(m_session->samples.size()) +
            ",\"validGps\":" + std::to_string(validGps) +
            ",\"jsonLen\":" + std::to_string(escaped.size()));
    }
    // #endregion
    runJs(QStringLiteral("loadSession('%1')").arg(escaped));
}

void Map3DWidget::setReplayTrailLength(int trailLength) {
    if (!m_session || !m_mapReady) return;
    runJs(QStringLiteral("setTrailLength(%1)").arg(trailLength));
}

void Map3DWidget::onSampleUpdated(const FlightSample &sample) {
    if (m_session) return;

    m_liveSamples.push_back(sample);
    ++m_totalLiveSamples;

    if (!m_mapReady) return;

    if (isValidCoord(sample.coordinates.latitude, sample.coordinates.longitude)) {
        runJs(QStringLiteral("addPoint(%1,%2,%3,%4)")
            .arg(sample.coordinates.latitude, 0, 'f', 9)
            .arg(sample.coordinates.longitude, 0, 'f', 9)
            .arg(sample.altitude, 0, 'f', 2)
            .arg(sample.timestamp));
    }
}

void Map3DWidget::onSessionReset() {
    m_session = nullptr;
    m_totalLiveSamples = 0;
    m_liveSamples.clear();
    m_sessionPending = false;

    if (m_mapReady) {
        runJs(QStringLiteral("clearAll()"));
    }
    emit positionStatsChanged(0, 0, 0, -1, -1, 0, 0, 0, 0, 0);
}

void Map3DWidget::fitPath() {
    if (m_mapReady) {
        runJs(QStringLiteral("fitCamera()"));
    }
}

void Map3DWidget::centerOnCurrent() {
    if (m_mapReady) {
        runJs(QStringLiteral("centerOnCurrent()"));
    }
}

void Map3DWidget::setCameraFollow(bool enabled) {
    m_followEnabled = enabled;
    if (m_mapReady) {
        runJs(QStringLiteral("followRocket(%1)").arg(enabled ? "true" : "false"));
    }
}

void Map3DWidget::onBridgeStatsUpdated(double lat, double lon, double alt,
                                        double distFromLaunch, double bearing,
                                        double pathLength,
                                        int totalSamples, int validGpsSamples,
                                        double launchLat, double launchLon)
{
    emit positionStatsChanged(lat, lon, alt, distFromLaunch, bearing,
                              pathLength, totalSamples, validGpsSamples,
                              launchLat, launchLon);
}

#include "Map3DWidget.moc"
