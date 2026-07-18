#include "gui/widgets/Map3DWidget.h"
#include "gui/ThemeManager.h"
#include "services/preview/FlightPreviewCache.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QKeyEvent>
#include <QLabel>
#include <QNativeGestureEvent>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariantList>
#include <QWebChannel>
#include <QWebEngineProfile>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineView>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

constexpr double kMaxAbsMapAltitude = 10'000'000.0;
constexpr double kMaxAbsMapTime = 1.0e15;
constexpr double kMaxMapDistance = 1.0e12;
constexpr int kMaxMapSampleCount = 10'000'000;

bool isFiniteWithin(double value, double absoluteLimit) {
    return std::isfinite(value) && std::abs(value) <= absoluteLimit;
}

bool isValidCoord(double lat, double lon) {
    return std::isfinite(lat) && std::isfinite(lon)
        && std::abs(lat) <= 90.0 && std::abs(lon) <= 180.0;
}

bool isValidMapPoint(const FlightSample &sample, double displayTime) {
    return isValidCoord(sample.coordinates.latitude, sample.coordinates.longitude)
        && isFiniteWithin(sample.altitude, kMaxAbsMapAltitude)
        && isFiniteWithin(displayTime, kMaxAbsMapTime);
}

QVariantMap mapPointPayload(const FlightSample &sample, int sampleIndex, double displayTime) {
    if (!isValidMapPoint(sample, displayTime)) {
        return {};
    }

    return {
        {QStringLiteral("idx"), sampleIndex},
        {QStringLiteral("lat"), sample.coordinates.latitude},
        {QStringLiteral("lon"), sample.coordinates.longitude},
        {QStringLiteral("alt"), sample.altitude},
        {QStringLiteral("ts"), displayTime},
    };
}

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

/** @brief Keeps external attribution links out of the privileged map page. */
class LockedMapPage : public QWebEnginePage {
public:
    explicit LockedMapPage(QWebEngineProfile *profile, QObject *parent = nullptr)
        : QWebEnginePage(profile, parent) {}

protected:
    bool acceptNavigationRequest(
        const QUrl &url,
        NavigationType type,
        bool isMainFrame) override {
        if (!isMainFrame) {
            return true;
        }

        const QString scheme = url.scheme().toLower();
        if (scheme == QStringLiteral("qrc")
            || scheme == QStringLiteral("data")
            || scheme == QStringLiteral("about")) {
            return true;
        }

        if (type == QWebEnginePage::NavigationTypeLinkClicked
            && scheme == QStringLiteral("https")) {
            QDesktopServices::openUrl(url);
        }
        return false;
    }
};

} // namespace

/**
 * @class TileCacheInterceptor
 * @brief Restricts embedded-map network access to approved tile providers.
 *
 * Qt WebEngine's HTTP cache stores successful responses on disk. This
 * interceptor blocks all HTTP traffic and permits HTTPS only to an exact list
 * of map/elevation tile hosts.
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
        const QString scheme = url.scheme().toLower();
        if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https")) {
            // qrc:, data:, and blob: resources are local to the embedded map.
            return;
        }

        const int port = url.port(-1);
        const bool approved = scheme == QStringLiteral("https")
            && (port == -1 || port == 443)
            && url.userInfo().isEmpty()
            && url.query().isEmpty()
            && url.fragment().isEmpty()
            && isApprovedTileRequest(url, info.resourceType());
        info.block(!approved);
    }

    /** @brief Returns the path to the tile cache directory. */
    [[nodiscard]] QString cacheDir() const { return m_cacheDir; }

private:
    static bool isApprovedTileRequest(
        const QUrl &url,
        QWebEngineUrlRequestInfo::ResourceType resourceType) {
        const QString host = url.host().toLower();
        const QString path = url.path(QUrl::FullyEncoded);

        static const QSet<QString> cartoHosts = {
            QStringLiteral("a.basemaps.cartocdn.com"),
            QStringLiteral("b.basemaps.cartocdn.com"),
            QStringLiteral("c.basemaps.cartocdn.com"),
            QStringLiteral("d.basemaps.cartocdn.com"),
        };
        static const QSet<QString> topoHosts = {
            QStringLiteral("a.tile.opentopomap.org"),
            QStringLiteral("b.tile.opentopomap.org"),
            QStringLiteral("c.tile.opentopomap.org"),
        };
        static const QRegularExpression cartoPath(
            QStringLiteral(R"(^/(?:dark_all|voyager)/\d+/\d+/\d+(?:@2x)?\.png$)"));
        static const QRegularExpression topoPath(
            QStringLiteral(R"(^/\d+/\d+/\d+\.png$)"));
        static const QRegularExpression arcGisPath(QStringLiteral(
            R"(^/ArcGIS/rest/services/World_Imagery/MapServer/tile/\d+/\d+/\d+$)"));
        static const QRegularExpression terrariumPath(QStringLiteral(
            R"(^/elevation-tiles-prod/terrarium/\d+/\d+/\d+\.png$)"));

        if (resourceType == QWebEngineUrlRequestInfo::ResourceTypeImage) {
            return (cartoHosts.contains(host) && cartoPath.match(path).hasMatch())
                || (topoHosts.contains(host) && topoPath.match(path).hasMatch())
                || (host == QStringLiteral("server.arcgisonline.com")
                    && arcGisPath.match(path).hasMatch())
                || (host == QStringLiteral("s3.amazonaws.com")
                    && terrariumPath.match(path).hasMatch());
        }
        return false;
    }

    QString m_cacheDir;
};

namespace {

struct MapProfileResources {
    QWebEngineProfile *profile = nullptr;
    TileCacheInterceptor *interceptor = nullptr;
};

MapProfileResources &sharedMapProfileResources() {
    static MapProfileResources resources = []() {
        auto *owner = QApplication::instance();
        auto *profile = new QWebEngineProfile(QStringLiteral("CosmoSoftMap"), owner);
        auto *interceptor = new TileCacheInterceptor(profile);
        const QString cacheRoot = interceptor->cacheDir();
        profile->setPersistentStoragePath(cacheRoot + QStringLiteral("/profile"));
        profile->setCachePath(cacheRoot + QStringLiteral("/http"));
        profile->setHttpCacheType(QWebEngineProfile::DiskHttpCache);
        profile->setHttpCacheMaximumSize(128 * 1024 * 1024);
        profile->setUrlRequestInterceptor(interceptor);
        return MapProfileResources{profile, interceptor};
    }();
    return resources;
}

} // namespace

Map3DWidget::Map3DWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto &mapProfile = sharedMapProfileResources();
    m_tileCache = mapProfile.interceptor;

    auto *page = new LockedMapPage(mapProfile.profile, this);
    page->settings()->setAttribute(QWebEngineSettings::WebGLEnabled, true);
    page->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
    page->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, false);

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

    m_loadError = new QWidget(this);
    auto *errorLayout = new QVBoxLayout(m_loadError);
    errorLayout->setContentsMargins(24, 24, 24, 24);
    errorLayout->addStretch();
    m_loadErrorLabel = new QLabel(m_loadError);
    m_loadErrorLabel->setAlignment(Qt::AlignCenter);
    m_loadErrorLabel->setWordWrap(true);
    m_loadErrorLabel->setAccessibleName(tr("Map loading error"));
    errorLayout->addWidget(m_loadErrorLabel);
    m_retryButton = new QPushButton(tr("Retry map"), m_loadError);
    m_retryButton->setAccessibleName(tr("Retry loading the map"));
    errorLayout->addWidget(m_retryButton, 0, Qt::AlignHCenter);
    errorLayout->addStretch();
    m_loadError->hide();
    layout->addWidget(m_loadError);

    m_readyWatchdog = new QTimer(this);
    m_readyWatchdog->setSingleShot(true);
    m_readyWatchdog->setInterval(5000);

    connect(m_retryButton, &QPushButton::clicked,
            this, &Map3DWidget::loadMapPage);
    connect(m_readyWatchdog, &QTimer::timeout, this, [this]() {
        showMapLoadError(tr("The map did not finish initializing. Try loading it again."));
    });
    connect(page, &QWebEnginePage::loadFinished,
            this, &Map3DWidget::onMapLoadFinished);

    connect(&cosmo::ThemeManager::instance(), &cosmo::ThemeManager::themeChanged,
            this, &Map3DWidget::pushThemeToMap);

    loadMapPage();
}

void Map3DWidget::loadMapPage() {
    m_readyWatchdog->stop();
    m_mapReady = false;
    m_loadAttemptActive = true;
    m_sessionPending = m_session && !m_session->samples.empty();
    m_loadError->hide();
    m_webView->show();

    QFile htmlFile(QStringLiteral(":/map/map3d.html"));
    if (!htmlFile.open(QIODevice::ReadOnly)) {
        showMapLoadError(tr("The embedded map resource could not be opened."));
        return;
    }

    const QString html = QString::fromUtf8(htmlFile.readAll());
    m_webView->page()->setHtml(html, QUrl(QStringLiteral("qrc:///map/")));
}

void Map3DWidget::onMapLoadFinished(bool succeeded) {
    // A timed-out or superseded attempt may still emit loadFinished.  Keep the
    // retry placeholder authoritative until the user starts a fresh attempt.
    if (!m_loadAttemptActive) {
        return;
    }
    if (!succeeded) {
        showMapLoadError(tr("The map failed to load. Check the application resources and try again."));
        return;
    }

    m_loadError->hide();
    m_webView->show();
    if (!m_mapReady && m_loadAttemptActive) {
        m_readyWatchdog->start();
    }
}

void Map3DWidget::showMapLoadError(const QString &message) {
    m_readyWatchdog->stop();
    m_mapReady = false;
    m_loadAttemptActive = false;
    m_webView->hide();
    m_loadErrorLabel->setText(message);
    m_loadError->show();
}

void Map3DWidget::onMapReady() {
    m_readyWatchdog->stop();
    if (!m_loadAttemptActive) {
        return;
    }
    if (m_mapReady) {
        return;
    }
    m_loadAttemptActive = false;
    m_mapReady = true;
    pushThemeToMap();
    if (m_sessionPending) {
        sendPendingSession();
    }
    if (!m_liveSamples.empty()) {
        for (int i = 0; i < static_cast<int>(m_liveSamples.size()); ++i) {
            const auto &sample = m_liveSamples[static_cast<std::size_t>(i)];
            const QVariantMap point = mapPointPayload(
                sample, i, static_cast<double>(sample.timestamp));
            if (!point.isEmpty()) {
                m_bridge->publishLivePoint(point);
            }
        }
    }
    m_bridge->requestFollow(m_followEnabled);
}

void Map3DWidget::pushThemeToMap() {
    if (!m_mapReady) return;
    const auto &p = cosmo::ThemeManager::instance().palette();
    m_bridge->publishTheme({
        {QStringLiteral("bg_base"), p.bg_base},
        {QStringLiteral("bg_dark"), p.bg_dark},
        {QStringLiteral("bg_panel"), p.bg_panel},
        {QStringLiteral("text_primary"), p.text_primary},
        {QStringLiteral("text_dim"), p.text_dim},
        {QStringLiteral("text_muted"), p.text_muted},
        {QStringLiteral("border_subtle"), p.border_subtle},
        {QStringLiteral("accent_link"), p.accent_link},
    });
}

void Map3DWidget::setReplaySession(
    std::shared_ptr<const FlightSession> session,
    std::shared_ptr<const cosmo::preview::FlightPreviewCache> preview) {
    m_session = std::move(session);
    m_preview = std::move(preview);
    m_liveSamples.clear();

    if (!m_session || m_session->samples.empty()) {
        if (m_mapReady) {
            m_bridge->requestClear();
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

    const auto appendPoint = [this](QVariantList &array, int sampleIndex) {
        if (sampleIndex < 0 || sampleIndex >= static_cast<int>(m_session->samples.size())) {
            return;
        }
        const auto &s = m_session->samples[static_cast<std::size_t>(sampleIndex)];
        const double displayTime = m_preview
            ? m_preview->displaySecondAt(sampleIndex)
            : static_cast<double>(s.timestamp) / 1000.0;
        const QVariantMap point = mapPointPayload(s, sampleIndex, displayTime);
        if (!point.isEmpty()) {
            array.append(point);
        }
    };

    QVariantList path2d;
    QVariantList path3d;
    if (m_preview) {
        for (const int sampleIndex : m_preview->map2DIndices()) {
            appendPoint(path2d, sampleIndex);
        }
        for (const int sampleIndex : m_preview->map3DIndices()) {
            appendPoint(path3d, sampleIndex);
        }
    } else {
        for (int i = 0; i < static_cast<int>(m_session->samples.size()); ++i) {
            appendPoint(path2d, i);
            appendPoint(path3d, i);
        }
    }

    m_bridge->publishSession({
        {QStringLiteral("total"), static_cast<int>(m_session->samples.size())},
        {QStringLiteral("path"), path2d},
        {QStringLiteral("path3d"), path3d},
    });
}

void Map3DWidget::setReplayTrailLength(int trailLength) {
    if (!m_session || m_session->samples.empty() || !m_mapReady) return;
    QVariantMap current;
    const int idx = std::clamp(trailLength - 1, 0, static_cast<int>(m_session->samples.size()) - 1);
    if (trailLength > 0 && (!m_preview || m_preview->gpsSampleValid(idx))) {
        const auto &s = m_session->samples[static_cast<std::size_t>(idx)];
        const double displayTime = m_preview
            ? m_preview->displaySecondAt(idx)
            : static_cast<double>(s.timestamp) / 1000.0;
        current = mapPointPayload(s, idx, displayTime);
    }
    m_bridge->publishTrailLength(trailLength, current);
}

void Map3DWidget::onSampleUpdated(const FlightSample &sample) {
    if (m_session) return;

    m_liveSamples.push_back(sample);

    if (!m_mapReady) return;

    const QVariantMap point = mapPointPayload(
        sample, static_cast<int>(m_liveSamples.size()) - 1,
        static_cast<double>(sample.timestamp));
    if (!point.isEmpty()) {
        m_bridge->publishLivePoint(point);
    }
}

void Map3DWidget::onSessionReset() {
    m_session.reset();
    m_preview.reset();
    m_liveSamples.clear();
    m_sessionPending = false;

    if (m_mapReady) {
        m_bridge->requestClear();
    }
    emit positionStatsChanged(0, 0, 0, -1, -1, 0, 0, 0, 0, 0);
}

void Map3DWidget::fitPath() {
    if (m_mapReady) {
        m_bridge->requestFit();
    }
}

void Map3DWidget::centerOnCurrent() {
    if (m_mapReady) {
        m_bridge->requestCenter();
    }
}

void Map3DWidget::setCameraFollow(bool enabled) {
    m_followEnabled = enabled;
    if (m_mapReady) {
        m_bridge->requestFollow(enabled);
    }
}

void Map3DWidget::onBridgeStatsUpdated(double lat, double lon, double alt,
                                        double distFromLaunch, double bearing,
                                        double pathLength,
                                        int totalSamples, int validGpsSamples,
                                        double launchLat, double launchLon)
{
    const bool countsValid = totalSamples >= 0 && totalSamples <= kMaxMapSampleCount
        && validGpsSamples >= 0 && validGpsSamples <= totalSamples;
    const bool statsValid = isValidCoord(lat, lon)
        && isValidCoord(launchLat, launchLon)
        && isFiniteWithin(alt, kMaxAbsMapAltitude)
        && std::isfinite(distFromLaunch) && distFromLaunch >= -1.0
        && distFromLaunch <= kMaxMapDistance
        && std::isfinite(bearing) && bearing >= -1.0 && bearing <= 360.0
        && std::isfinite(pathLength) && pathLength >= 0.0
        && pathLength <= kMaxMapDistance
        && countsValid;
    if (!statsValid) {
        return;
    }
    emit positionStatsChanged(lat, lon, alt, distFromLaunch, bearing,
                              pathLength, totalSamples, validGpsSamples,
                              launchLat, launchLon);
}

#include "Map3DWidget.moc"
