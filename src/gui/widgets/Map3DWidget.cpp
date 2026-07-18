#include "gui/widgets/Map3DWidget.h"
#include "gui/ThemeManager.h"
#include "services/preview/FlightPreviewCache.h"

#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QNativeGestureEvent>
#include <QStandardPaths>
#include <QVBoxLayout>

#if __has_include(<QWebChannel>) && __has_include(<QWebEngineProfile>) && __has_include(<QWebEnginePage>) && __has_include(<QWebEngineSettings>) && __has_include(<QWebEngineUrlRequestInterceptor>) && __has_include(<QWebEngineView>)
#define COSMO_HAS_WEBENGINE 1
#include <QWebChannel>
#include <QWebEngineProfile>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineView>
#else
#define COSMO_HAS_WEBENGINE 0
#endif
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <utility>

static bool isValidCoord(double lat, double lon) {
    return std::isfinite(lat) && std::isfinite(lon)
        && std::abs(lat) <= 90.0 && std::abs(lon) <= 180.0;
}

#if COSMO_HAS_WEBENGINE

/**
 * @class LockedMapWebView
 * @brief QWebEngineView variant that keeps the embedded map at page zoom 100%.
 */
class LockedMapWebView : public QWebEngineView {
public:
    explicit LockedMapWebView(QWidget *parent = nullptr)
        : QWebEngineView(parent) {
        if (auto *app = QApplication::instance()) {
            app->installEventFilter(this);
        }
    }

    ~LockedMapWebView() override {
        if (auto *app = QApplication::instance()) {
            app->removeEventFilter(this);
        }
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (shouldBlockPageZoom(watched, event)) {
            resetZoomFactor();
            event->accept();
            return true;
        }
        return QWebEngineView::eventFilter(watched, event);
    }

    bool event(QEvent *event) override {
        if (shouldBlockPageZoom(this, event)) {
            resetZoomFactor();
            event->accept();
            return true;
        }
        return QWebEngineView::event(event);
    }

    void keyPressEvent(QKeyEvent *event) override {
        if (isBrowserZoomShortcut(event)) {
            resetZoomFactor();
            event->accept();
            return;
        }
        QWebEngineView::keyPressEvent(event);
    }

private:
    [[nodiscard]] bool shouldBlockPageZoom(QObject *watched, QEvent *event) const {
        if (!isMapTarget(watched)) {
            return false;
        }
        switch (event->type()) {
        case QEvent::NativeGesture: {
            const auto *gesture = static_cast<QNativeGestureEvent *>(event);
            return gesture->gestureType() == Qt::ZoomNativeGesture
                || gesture->gestureType() == Qt::SmartZoomNativeGesture;
        }
        case QEvent::KeyPress:
            return isBrowserZoomShortcut(static_cast<QKeyEvent *>(event));
        default:
            return false;
        }
    }

    [[nodiscard]] bool isBrowserZoomShortcut(const QKeyEvent *event) const {
        const bool hasZoomModifier =
            event->modifiers().testFlag(Qt::ControlModifier)
            || event->modifiers().testFlag(Qt::MetaModifier);
        if (!hasZoomModifier) {
            return false;
        }

        switch (event->key()) {
        case Qt::Key_0:
        case Qt::Key_Equal:
        case Qt::Key_Minus:
        case Qt::Key_Plus:
        case Qt::Key_Underscore:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] bool isMapTarget(QObject *watched) const {
        for (QObject *obj = watched; obj != nullptr; obj = obj->parent()) {
            if (obj == this || obj == page()) {
                return true;
            }
        }

        const auto *widget = qobject_cast<QWidget *>(watched);
        return widget && (widget == this || isAncestorOf(widget));
    }

    void resetZoomFactor() {
        if (!qFuzzyCompare(zoomFactor(), 1.0)) {
            setZoomFactor(1.0);
        }
    }
};

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
    connect(m_bridge, &Map3DBridge::followChanged,
            this, &Map3DWidget::setCameraFollow);

    m_webView = new LockedMapWebView(this);
    m_webView->setContextMenuPolicy(Qt::NoContextMenu);
    m_webView->setPage(page);
    m_webView->setZoomFactor(1.0);
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
        R"({"bg_base":"%1","bg_dark":"%2","bg_panel":"%3","text_primary":"%4",)"
        R"("text_dim":"%5","text_muted":"%6","border_subtle":"%7","accent_link":"%8"})")
        .arg(p.bg_base, p.bg_dark, p.bg_panel, p.text_primary,
             p.text_dim, p.text_muted, p.border_subtle, p.accent_link);
    runJs(QStringLiteral("applyTheme(%1)").arg(json));
}

void Map3DWidget::setReplaySession(
    std::shared_ptr<const FlightSession> session,
    std::shared_ptr<const cosmo::preview::FlightPreviewCache> preview) {
    m_session = std::move(session);
    m_preview = std::move(preview);
    m_liveSamples.clear();

    if (!m_session || m_session->samples.empty()) {
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

    const auto appendPoint = [this](QJsonArray &array, int sampleIndex) {
        if (sampleIndex < 0 || sampleIndex >= static_cast<int>(m_session->samples.size())) {
            return;
        }
        const auto &s = m_session->samples[static_cast<std::size_t>(sampleIndex)];
        QJsonObject obj;
        obj[QStringLiteral("idx")] = sampleIndex;
        obj[QStringLiteral("lat")] = s.coordinates.latitude;
        obj[QStringLiteral("lon")] = s.coordinates.longitude;
        obj[QStringLiteral("alt")] = s.altitude;
        obj[QStringLiteral("ts")] = m_preview
            ? m_preview->displaySecondAt(sampleIndex)
            : static_cast<double>(s.timestamp) / 1000.0;
        array.append(obj);
    };

    QJsonArray path2d;
    QJsonArray path3d;
    if (m_preview) {
        for (const int sampleIndex : m_preview->map2DIndices()) {
            appendPoint(path2d, sampleIndex);
        }
        for (const int sampleIndex : m_preview->map3DIndices()) {
            appendPoint(path3d, sampleIndex);
        }
    } else {
        for (int i = 0; i < static_cast<int>(m_session->samples.size()); ++i) {
            const auto &s = m_session->samples[static_cast<std::size_t>(i)];
            if (isValidCoord(s.coordinates.latitude, s.coordinates.longitude)) {
                appendPoint(path2d, i);
                appendPoint(path3d, i);
            }
        }
    }

    QJsonObject payload;
    payload[QStringLiteral("total")] = static_cast<int>(m_session->samples.size());
    payload[QStringLiteral("path")] = path2d;
    payload[QStringLiteral("path3d")] = path3d;
    const QByteArray jsonBytes = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    const QString json = QString::fromUtf8(jsonBytes);

    runJs(QStringLiteral("loadSession(%1)").arg(json));
}

void Map3DWidget::setReplayTrailLength(int trailLength) {
    if (!m_session || m_session->samples.empty() || !m_mapReady) return;
    QJsonObject current;
    const int idx = std::clamp(trailLength - 1, 0, static_cast<int>(m_session->samples.size()) - 1);
    if (trailLength > 0 && (!m_preview || m_preview->gpsSampleValid(idx))) {
        const auto &s = m_session->samples[static_cast<std::size_t>(idx)];
        if (isValidCoord(s.coordinates.latitude, s.coordinates.longitude)) {
            current[QStringLiteral("idx")] = idx;
            current[QStringLiteral("lat")] = s.coordinates.latitude;
            current[QStringLiteral("lon")] = s.coordinates.longitude;
            current[QStringLiteral("alt")] = s.altitude;
            current[QStringLiteral("ts")] = m_preview
                ? m_preview->displaySecondAt(idx)
                : static_cast<double>(s.timestamp) / 1000.0;
        }
    }
    const QString currentJson = current.isEmpty()
        ? QStringLiteral("null")
        : QString::fromUtf8(QJsonDocument(current).toJson(QJsonDocument::Compact));
    runJs(QStringLiteral("setTrailLength(%1,%2)").arg(trailLength).arg(currentJson));
}

void Map3DWidget::onSampleUpdated(const FlightSample &sample) {
    if (m_session) return;

    m_liveSamples.push_back(sample);

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
    m_session.reset();
    m_preview.reset();
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

#else

Map3DWidget::Map3DWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *label = new QLabel(QStringLiteral("3D map unavailable in this build"), this);
    label->setAlignment(Qt::AlignCenter);
    layout->addWidget(label);

    const auto applyFallbackTheme = [this, label]() {
        label->setStyleSheet(QStringLiteral(
            "QLabel { background-color: %1; color: %2; border: 1px solid %3; }")
            .arg(cosmo::ThemeManager::instance().palette().bg_panel)
            .arg(cosmo::ThemeManager::instance().palette().text_muted)
            .arg(cosmo::ThemeManager::instance().palette().border_subtle));
    };
    applyFallbackTheme();
    connect(&cosmo::ThemeManager::instance(), &cosmo::ThemeManager::themeChanged,
            this, applyFallbackTheme);
}

void Map3DWidget::runJs(const QString &) {}

void Map3DWidget::onMapReady() {}

void Map3DWidget::pushThemeToMap() {}

void Map3DWidget::setReplaySession(
    std::shared_ptr<const FlightSession> session,
    std::shared_ptr<const cosmo::preview::FlightPreviewCache> preview) {
    m_session = std::move(session);
    m_preview = std::move(preview);
    m_liveSamples.clear();

    if (!m_session || m_session->samples.empty()) {
        emit positionStatsChanged(0, 0, 0, -1, -1, 0, 0, 0, 0, 0);
    }
}

void Map3DWidget::sendPendingSession() {}

void Map3DWidget::setReplayTrailLength(int) {}

void Map3DWidget::onSampleUpdated(const FlightSample &sample) {
    if (!m_session) {
        m_liveSamples.push_back(sample);
    }
}

void Map3DWidget::onSessionReset() {
    m_session.reset();
    m_preview.reset();
    m_liveSamples.clear();
    m_sessionPending = false;
    emit positionStatsChanged(0, 0, 0, -1, -1, 0, 0, 0, 0, 0);
}

void Map3DWidget::fitPath() {}

void Map3DWidget::centerOnCurrent() {}

void Map3DWidget::setCameraFollow(bool enabled) {
    m_followEnabled = enabled;
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

#endif

#include "Map3DWidget.moc"
