#include "gui/widgets/Map3DWidget.h"
#include "gui/ThemeManager.h"
#include "services/preview/FlightPreviewCache.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QNativeGestureEvent>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QShowEvent>
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
#include <limits>
#include <utility>

namespace {

constexpr double kMaxAbsMapAltitude = 10'000'000.0;
constexpr double kMaxAbsMapTime = 1.0e15;
constexpr double kMaxMapDistance = 1.0e12;
constexpr int kMaxMapSampleCount = 10'000'000;
constexpr int kMapUpdateIntervalMs = 50;
constexpr int kLiveHistoryCompactTarget =
    cosmo::preview::FlightPreviewCache::kMap3DPointBudget;
constexpr int kLive3DCompactTarget =
    cosmo::preview::FlightPreviewCache::kMap3DPointBudget / 2;

[[nodiscard]] std::vector<std::size_t> cappedSourceIndices(
    std::size_t sourceSize,
    int pointBudget) {
    if (sourceSize == 0U || pointBudget <= 0) {
        return {};
    }

    const std::size_t budget = static_cast<std::size_t>(pointBudget);
    const std::size_t outputSize = std::min(sourceSize, budget);
    std::vector<std::size_t> indices;
    indices.reserve(outputSize);
    if (sourceSize <= budget || outputSize == 1U) {
        for (std::size_t index = 0; index < outputSize; ++index) {
            indices.push_back(index);
        }
        return indices;
    }

    const std::size_t sourceLast = sourceSize - 1U;
    const std::size_t outputLast = outputSize - 1U;
    const std::size_t quotient = sourceLast / outputLast;
    const std::size_t remainder = sourceLast % outputLast;
    for (std::size_t index = 0; index < outputSize; ++index) {
        indices.push_back(quotient * index + (remainder * index) / outputLast);
    }
    return indices;
}

[[nodiscard]] std::vector<int> cappedSampleIndices(
    const std::vector<int> &source,
    int pointBudget) {
    std::vector<int> result;
    const auto positions = cappedSourceIndices(source.size(), pointBudget);
    result.reserve(positions.size());
    for (const std::size_t position : positions) {
        result.push_back(source[position]);
    }
    return result;
}

[[nodiscard]] std::vector<int> cappedSequentialSampleIndices(
    std::size_t sourceSize,
    int pointBudget) {
    std::vector<int> result;
    const auto positions = cappedSourceIndices(sourceSize, pointBudget);
    result.reserve(positions.size());
    for (const std::size_t position : positions) {
        if (position > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
            break;
        }
        result.push_back(static_cast<int>(position));
    }
    return result;
}

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

bool isUsableMapPoint(const FlightSample &sample, double displayTime) {
    const double latitude = sample.coordinates.latitude;
    const double longitude = sample.coordinates.longitude;
    return isValidMapPoint(sample, displayTime)
        && (std::abs(latitude) >= 1.0e-9 || std::abs(longitude) >= 1.0e-9);
}

QVariantMap mapPointPayload(const FlightSample &sample, int sampleIndex, double displayTime) {
    if (!isUsableMapPoint(sample, displayTime)) {
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

    m_updateTimer = new QTimer(this);
    m_updateTimer->setSingleShot(true);
    m_updateTimer->setInterval(kMapUpdateIntervalMs);

    connect(m_retryButton, &QPushButton::clicked,
            this, &Map3DWidget::loadMapPage);
    connect(m_readyWatchdog, &QTimer::timeout, this, [this]() {
        showMapLoadError(tr("The map did not finish initializing. Try loading it again."));
    });
    connect(m_updateTimer, &QTimer::timeout,
            this, &Map3DWidget::flushPendingUpdates);

    connect(&cosmo::ThemeManager::instance(), &cosmo::ThemeManager::themeChanged,
            this, &Map3DWidget::pushThemeToMap);

}

void Map3DWidget::ensureMapInitialized() {
    if (m_webView) {
        return;
    }

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
    connect(page, &QWebEnginePage::loadFinished,
            this, &Map3DWidget::onMapLoadFinished);

    m_webView = new LockedMapWebView(this);
    m_webView->setContextMenuPolicy(Qt::NoContextMenu);
    m_webView->setPage(page);
    m_webView->setZoomFactor(1.0);
    static_cast<QVBoxLayout *>(layout())->insertWidget(0, m_webView);
}

void Map3DWidget::loadMapPage() {
    ensureMapInitialized();
    m_readyWatchdog->stop();
    m_mapReady = false;
    m_loadAttemptActive = true;
    m_sessionPending = m_session && !m_session->samples.empty();
    m_liveSnapshotPending = !m_liveSamples.empty();
    m_liveUpdatePending = m_liveTotalSamples > 0;
    m_followUpdatePending = true;
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
    if (!m_mapReady && m_loadAttemptActive && isVisible()) {
        m_readyWatchdog->start();
    }
}

void Map3DWidget::showMapLoadError(const QString &message) {
    m_readyWatchdog->stop();
    m_mapReady = false;
    m_loadAttemptActive = false;
    if (m_webView) {
        m_webView->hide();
    }
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
    m_liveSnapshotPending = !m_liveSamples.empty();
    m_liveUpdatePending = m_liveTotalSamples > 0;
    if (isVisible()) {
        m_bridge->publishHostVisibility(true);
        pushThemeToMap();
        scheduleMapUpdate();
    } else {
        m_bridge->publishHostVisibility(false);
    }
}

void Map3DWidget::pushThemeToMap() {
    if (!m_mapReady || !isVisible()) return;
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

void Map3DWidget::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    ensureMapInitialized();
    if (!m_mapReady && !m_loadAttemptActive) {
        loadMapPage();
    } else if (!m_mapReady && m_loadAttemptActive) {
        m_readyWatchdog->start();
    }

    m_liveSnapshotPending = !m_liveSamples.empty();
    m_liveUpdatePending = m_liveUpdatePending || m_liveTotalSamples > 0;
    if (m_mapReady) {
        m_bridge->publishHostVisibility(true);
        pushThemeToMap();
        scheduleMapUpdate();
    }
}

void Map3DWidget::hideEvent(QHideEvent *event) {
    m_updateTimer->stop();
    m_readyWatchdog->stop();
    if (m_mapReady) {
        m_bridge->publishHostVisibility(false);
    }
    QWidget::hideEvent(event);
}

void Map3DWidget::setReplaySession(
    std::shared_ptr<const FlightSession> session,
    std::shared_ptr<const cosmo::preview::FlightPreviewCache> preview) {
    m_session = std::move(session);
    const bool previewMatchesSession = m_session
        && preview
        && preview->wasBuiltFor(*m_session)
        && preview->displaySeconds().size() == m_session->samples.size();
    m_preview = previewMatchesSession ? std::move(preview) : nullptr;
    m_liveSamples.clear();
    m_liveTotalSamples = 0;
    m_lastPublishedLiveIndex = -1;
    m_publishedLive3DPointCount = 0;
    m_liveUpdatePending = false;
    m_liveSnapshotPending = true;

    if (!m_session || m_session->samples.empty()) {
        m_sessionPending = false;
        m_trailUpdatePending = false;
        m_clearPending = true;
        scheduleMapUpdate();
        emit positionStatsChanged(0, 0, 0, -1, -1, 0, 0, 0, 0, 0);
        return;
    }

    m_clearPending = false;
    m_sessionPending = true;
    const std::size_t sampleCount = std::min(
        m_session->samples.size(),
        static_cast<std::size_t>(kMaxMapSampleCount));
    m_pendingTrailLength = static_cast<int>(sampleCount);
    m_trailUpdatePending = true;
    scheduleMapUpdate();
}

void Map3DWidget::sendPendingSession() {
    m_sessionPending = false;
    if (!m_session || m_session->samples.empty()) return;

    const auto appendPoint = [this](QVariantList &array, int sampleIndex) {
        if (sampleIndex < 0
            || static_cast<std::size_t>(sampleIndex) >= m_session->samples.size()) {
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

    std::vector<int> path2dIndices;
    std::vector<int> path3dIndices;
    if (m_preview) {
        path2dIndices = cappedSampleIndices(
            m_preview->map2DIndices(),
            cosmo::preview::FlightPreviewCache::kMap2DPointBudget);
        path3dIndices = cappedSampleIndices(
            m_preview->map3DIndices(),
            cosmo::preview::FlightPreviewCache::kMap3DPointBudget);
    } else {
        const std::size_t boundedSourceSize = std::min(
            m_session->samples.size(),
            static_cast<std::size_t>(kMaxMapSampleCount));
        path2dIndices = cappedSequentialSampleIndices(
            boundedSourceSize,
            cosmo::preview::FlightPreviewCache::kMap2DPointBudget);
        path3dIndices = cappedSequentialSampleIndices(
            boundedSourceSize,
            cosmo::preview::FlightPreviewCache::kMap3DPointBudget);
    }

    QVariantList path2d;
    QVariantList path3d;
    path2d.reserve(static_cast<qsizetype>(path2dIndices.size()));
    path3d.reserve(static_cast<qsizetype>(path3dIndices.size()));
    for (const int sampleIndex : path2dIndices) {
        appendPoint(path2d, sampleIndex);
    }
    for (const int sampleIndex : path3dIndices) {
        appendPoint(path3d, sampleIndex);
    }

    const std::size_t boundedTotal = std::min(
        m_session->samples.size(),
        static_cast<std::size_t>(kMaxMapSampleCount));
    m_bridge->publishSession({
        {QStringLiteral("total"), static_cast<int>(boundedTotal)},
        {QStringLiteral("path"), path2d},
        {QStringLiteral("path3d"), path3d},
    });
}

void Map3DWidget::setReplayTrailLength(int trailLength) {
    if (!m_session || m_session->samples.empty()) return;
    const std::size_t boundedSize = std::min(
        m_session->samples.size(),
        static_cast<std::size_t>(kMaxMapSampleCount));
    m_pendingTrailLength = std::clamp(trailLength, 0, static_cast<int>(boundedSize));
    m_trailUpdatePending = true;
    scheduleMapUpdate();
}

void Map3DWidget::sendPendingTrailLength() {
    m_trailUpdatePending = false;
    if (!m_session || m_session->samples.empty()) return;

    QVariantMap current;
    const int sessionLast = static_cast<int>(std::min(
        m_session->samples.size() - 1U,
        static_cast<std::size_t>(kMaxMapSampleCount - 1)));
    const int idx = std::clamp(m_pendingTrailLength - 1, 0, sessionLast);
    if (m_pendingTrailLength > 0 && (!m_preview || m_preview->gpsSampleValid(idx))) {
        const auto &s = m_session->samples[static_cast<std::size_t>(idx)];
        const double displayTime = m_preview
            ? m_preview->displaySecondAt(idx)
            : static_cast<double>(s.timestamp) / 1000.0;
        current = mapPointPayload(s, idx, displayTime);
    }
    m_bridge->publishTrailLength(m_pendingTrailLength, current);
}

void Map3DWidget::onSampleUpdated(const FlightSample &sample) {
    onLiveSamplesReceived(QVector<FlightSample>{sample});
}

void Map3DWidget::onLiveSamplesReceived(const QVector<FlightSample> &samples) {
    if (m_session) return;

    bool totalChanged = false;
    for (const auto &sample : samples) {
        if (m_liveTotalSamples >= kMaxMapSampleCount) {
            break;
        }
        const int sampleIndex = m_liveTotalSamples;
        ++m_liveTotalSamples;
        totalChanged = true;
        if (isUsableMapPoint(sample, static_cast<double>(sample.timestamp))) {
            m_liveSamples.push_back({sample, sampleIndex});
        }
        compactLiveHistory();
    }
    if (totalChanged) {
        m_liveUpdatePending = true;
        scheduleMapUpdate();
    }
}

void Map3DWidget::compactLiveHistory() {
    const std::size_t pointBudget = static_cast<std::size_t>(
        cosmo::preview::FlightPreviewCache::kMap2DPointBudget);
    if (m_liveSamples.size() <= pointBudget) {
        return;
    }

    std::vector<IndexedLiveSample> compacted;
    const auto positions = cappedSourceIndices(m_liveSamples.size(), kLiveHistoryCompactTarget);
    compacted.reserve(positions.size());
    for (const std::size_t position : positions) {
        compacted.push_back(std::move(m_liveSamples[position]));
    }
    m_liveSamples = std::move(compacted);
    m_liveSnapshotPending = true;
}

void Map3DWidget::sendPendingLiveSamples() {
    bool replace = m_liveSnapshotPending;
    if (!replace) {
        int pending3DPoints = 0;
        for (const auto &entry : m_liveSamples) {
            if (entry.sampleIndex > m_lastPublishedLiveIndex) {
                ++pending3DPoints;
            }
        }
        if (m_publishedLive3DPointCount + pending3DPoints
            > cosmo::preview::FlightPreviewCache::kMap3DPointBudget) {
            replace = true;
        }
    }
    QVariantList path2d;
    QVariantList path3d;

    if (replace) {
        path2d.reserve(static_cast<qsizetype>(m_liveSamples.size()));
        for (const auto &entry : m_liveSamples) {
            const QVariantMap point = mapPointPayload(
                entry.sample,
                entry.sampleIndex,
                static_cast<double>(entry.sample.timestamp));
            if (!point.isEmpty()) {
                path2d.append(point);
            }
        }

        const auto positions3d = cappedSourceIndices(
            m_liveSamples.size(),
            kLive3DCompactTarget);
        path3d.reserve(static_cast<qsizetype>(positions3d.size()));
        for (const std::size_t position : positions3d) {
            const auto &entry = m_liveSamples[position];
            const QVariantMap point = mapPointPayload(
                entry.sample,
                entry.sampleIndex,
                static_cast<double>(entry.sample.timestamp));
            if (!point.isEmpty()) {
                path3d.append(point);
            }
        }
    } else {
        for (const auto &entry : m_liveSamples) {
            if (entry.sampleIndex <= m_lastPublishedLiveIndex) {
                continue;
            }
            const QVariantMap point = mapPointPayload(
                entry.sample,
                entry.sampleIndex,
                static_cast<double>(entry.sample.timestamp));
            if (!point.isEmpty()) {
                path2d.append(point);
                path3d.append(point);
            }
        }
    }

    m_bridge->publishLiveBatch({
        {QStringLiteral("replace"), replace},
        {QStringLiteral("total"), m_liveTotalSamples},
        {QStringLiteral("path"), path2d},
        {QStringLiteral("path3d"), path3d},
    });
    m_lastPublishedLiveIndex = m_liveTotalSamples - 1;
    if (replace) {
        m_publishedLive3DPointCount = static_cast<int>(path3d.size());
    } else {
        m_publishedLive3DPointCount += static_cast<int>(path3d.size());
    }
    m_liveSnapshotPending = false;
    m_liveUpdatePending = false;
}

void Map3DWidget::onSessionReset() {
    m_session.reset();
    m_preview.reset();
    m_liveSamples.clear();
    m_liveTotalSamples = 0;
    m_lastPublishedLiveIndex = -1;
    m_publishedLive3DPointCount = 0;
    m_sessionPending = false;
    m_liveUpdatePending = false;
    m_liveSnapshotPending = true;
    m_trailUpdatePending = false;
    m_clearPending = true;

    scheduleMapUpdate();
    emit positionStatsChanged(0, 0, 0, -1, -1, 0, 0, 0, 0, 0);
}

void Map3DWidget::fitPath() {
    if (m_mapReady && isVisible()) {
        m_bridge->requestFit();
    }
}

void Map3DWidget::centerOnCurrent() {
    if (m_mapReady && isVisible()) {
        m_bridge->requestCenter();
    }
}

void Map3DWidget::setCameraFollow(bool enabled) {
    m_followEnabled = enabled;
    m_followUpdatePending = true;
    scheduleMapUpdate();
}

void Map3DWidget::scheduleMapUpdate() {
    if (!m_mapReady || !isVisible() || m_updateTimer->isActive()) {
        return;
    }
    m_updateTimer->start();
}

void Map3DWidget::flushPendingUpdates() {
    if (!m_mapReady || !isVisible()) {
        return;
    }

    if (m_clearPending) {
        m_bridge->requestClear();
        m_clearPending = false;
    }
    if (m_sessionPending) {
        sendPendingSession();
    }
    if (!m_session && m_liveUpdatePending) {
        sendPendingLiveSamples();
    }
    if (m_session && m_trailUpdatePending) {
        sendPendingTrailLength();
    }
    if (m_followUpdatePending) {
        m_bridge->requestFollow(m_followEnabled);
        m_followUpdatePending = false;
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
