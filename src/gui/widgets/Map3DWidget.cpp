#include "gui/widgets/Map3DWidget.h"
#include "gui/ThemeManager.h"

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
        const QUrl url = info.requestUrl();
        if (url.host().contains(QStringLiteral("basemaps.cartocdn.com"))
            || url.host().contains(QStringLiteral("tile"))) {
            info.setHttpHeader("Cache-Control", "max-age=604800");
        }
    }

    /** @brief Returns the path to the tile cache directory. */
    [[nodiscard]] QString cacheDir() const { return m_cacheDir; }

private:
    QString m_cacheDir;
};

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

    auto *page = new QWebEnginePage(profile, this);
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
    if (htmlFile.open(QIODevice::ReadOnly)) {
        const QString html = QString::fromUtf8(htmlFile.readAll());
        page->setHtml(html, QUrl(QStringLiteral("qrc:///map/")));
    }

    connect(&cosmo::ThemeManager::instance(), &cosmo::ThemeManager::themeChanged,
            this, &Map3DWidget::pushThemeToMap);
}

void Map3DWidget::runJs(const QString &js) {
    if (m_webView && m_webView->page()) {
        m_webView->page()->runJavaScript(js);
    }
}

void Map3DWidget::onMapReady() {
    m_mapReady = true;
    pushThemeToMap();
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

void Map3DWidget::pushThemeToMap() {
    if (!m_mapReady) return;
    const auto &p = cosmo::ThemeManager::instance().palette();
    const auto json = QStringLiteral(
        R"({"bg_base":"%1","bg_dark":"%2","text_primary":"%3","text_dim":"%4",)"
        R"("border_subtle":"%5","accent_link":"%6"})")
        .arg(p.bg_base, p.bg_dark, p.text_primary, p.text_dim, p.border_subtle, p.accent_link);
    runJs(QStringLiteral("applyTheme(%1)").arg(json));
}

void Map3DWidget::setReplaySession(const FlightSession *session) {
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
    if (!m_session || m_session->samples.empty()) return;

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
