#include "services/video/MapTilerTerrainProvider.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QStandardPaths>
#include <QTextDocument>
#include <QTimer>
#include <QUrlQuery>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace cosmo::video {
namespace {

[[nodiscard]] double mercator_y(double latitude) {
  const double clamped = std::clamp(latitude, -85.0, 85.0);
  const double radians = clamped * 3.14159265358979323846 / 180.0;
  return (1.0 - std::asinh(std::tan(radians)) / 3.14159265358979323846) * 0.5;
}

[[nodiscard]] int fit_zoom(const TerrainBounds &bounds, const QSize &size) {
  const double x_span =
      std::max(1e-8, std::abs(bounds.east - bounds.west) / 360.0);
  const double y_span = std::max(
      1e-8, std::abs(mercator_y(bounds.north) - mercator_y(bounds.south)));
  const double x_scale =
      static_cast<double>(std::max(1, size.width())) / (256.0 * x_span);
  const double y_scale =
      static_cast<double>(std::max(1, size.height())) / (256.0 * y_span);
  return std::clamp(
      static_cast<int>(std::floor(std::log2(std::min(x_scale, y_scale)))), 1,
      18);
}

[[nodiscard]] int tile_x(double longitude, int zoom) {
  const int count = 1 << zoom;
  return std::clamp(static_cast<int>(std::floor((longitude + 180.0) / 360.0 *
                                                static_cast<double>(count))),
                    0, count - 1);
}

[[nodiscard]] int tile_y(double latitude, int zoom) {
  const int count = 1 << zoom;
  return std::clamp(static_cast<int>(std::floor(mercator_y(latitude) *
                                                static_cast<double>(count))),
                    0, count - 1);
}

[[nodiscard]] QString plain_attribution(const QString &html) {
  QTextDocument document;
  document.setHtml(html);
  return document.toPlainText().simplified();
}

} // namespace

MapTilerTerrainProvider::MapTilerTerrainProvider(QNetworkAccessManager *network,
                                                 QObject *parent)
    : ITerrainProvider(parent), m_network(network) {
  if (!m_network) {
    m_owned_network = std::make_unique<QNetworkAccessManager>();
    m_network = m_owned_network.get();
    auto cache = std::make_unique<QNetworkDiskCache>();
    const QString cache_path =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
        QStringLiteral("/video_terrain");
    QDir().mkpath(cache_path);
    cache->setCacheDirectory(cache_path);
    cache->setMaximumCacheSize(128 * 1024 * 1024);
    m_network->setCache(cache.release());
  }
}

MapTilerTerrainProvider::~MapTilerTerrainProvider() = default;

void MapTilerTerrainProvider::request_terrain(const TerrainBounds &bounds,
                                              const QSize &texture_size,
                                              const QString &api_key) {
  cancel();
  if (api_key.trimmed().isEmpty()) {
    emit failed(tr("A MapTiler API key is required for real terrain."));
    return;
  }
  if (!texture_size.isValid() || bounds.north <= bounds.south ||
      bounds.east <= bounds.west || bounds.north > 85.0 ||
      bounds.south < -85.0) {
    emit failed(
        tr("The flight bounds cannot be rendered by the terrain provider."));
    return;
  }
  m_bounds = bounds;
  m_texture_size = texture_size.boundedTo(QSize(2048, 2048));
  m_api_key = api_key.trimmed();
  m_attempt = 0;
  m_active = true;
  begin_attempt();
}

void MapTilerTerrainProvider::cancel() {
  m_active = false;
  clear_replies();
  m_api_key.clear();
  m_imagery_bytes.clear();
  m_tile_images.clear();
  m_attribution.clear();
}

void MapTilerTerrainProvider::begin_attempt() {
  if (!m_active) {
    return;
  }
  ++m_attempt;
  m_imagery_complete = false;
  m_metadata_complete = false;
  m_imagery_bytes.clear();
  m_tile_images.clear();
  m_tile_replies.clear();
  m_attribution.clear();
  m_completed_tiles = 0;

  QUrl imagery_url(QStringLiteral("https://api.maptiler.com/maps/outdoor-v2/"
                                  "static/%1,%2,%3,%4/%5x%6.png")
                       .arg(m_bounds.west, 0, 'f', 7)
                       .arg(m_bounds.south, 0, 'f', 7)
                       .arg(m_bounds.east, 0, 'f', 7)
                       .arg(m_bounds.north, 0, 'f', 7)
                       .arg(m_texture_size.width())
                       .arg(m_texture_size.height()));
  QUrlQuery imagery_query;
  imagery_query.addQueryItem(QStringLiteral("key"), m_api_key);
  imagery_query.addQueryItem(QStringLiteral("attribution"),
                             QStringLiteral("false"));
  imagery_query.addQueryItem(QStringLiteral("padding"), QStringLiteral("0"));
  imagery_url.setQuery(imagery_query);

  QUrl metadata_url(QStringLiteral(
      "https://api.maptiler.com/tiles/terrain-rgb-v2/tiles.json"));
  QUrlQuery metadata_query;
  metadata_query.addQueryItem(QStringLiteral("key"), m_api_key);
  metadata_url.setQuery(metadata_query);

  QNetworkRequest imagery_request(imagery_url);
  imagery_request.setTransferTimeout(15000);
  imagery_request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                               QNetworkRequest::PreferCache);
  QNetworkRequest metadata_request(metadata_url);
  metadata_request.setTransferTimeout(15000);
  metadata_request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                                QNetworkRequest::PreferCache);

  m_imagery_reply = m_network->get(imagery_request);
  m_metadata_reply = m_network->get(metadata_request);
  connect(m_imagery_reply, &QNetworkReply::finished, this,
          &MapTilerTerrainProvider::on_imagery_finished);
  connect(m_metadata_reply, &QNetworkReply::finished, this,
          &MapTilerTerrainProvider::on_metadata_finished);
}

void MapTilerTerrainProvider::on_imagery_finished() {
  if (!m_active || !m_imagery_reply) {
    return;
  }
  if (m_imagery_reply->error() != QNetworkReply::NoError) {
    fail_or_retry();
    return;
  }
  m_imagery_bytes = m_imagery_reply->readAll();
  m_imagery_complete = true;
  m_imagery_reply->deleteLater();
  m_imagery_reply = nullptr;
  finish_if_ready();
}

void MapTilerTerrainProvider::on_metadata_finished() {
  if (!m_active || !m_metadata_reply) {
    return;
  }
  if (m_metadata_reply->error() != QNetworkReply::NoError) {
    fail_or_retry();
    return;
  }
  const QJsonObject metadata =
      QJsonDocument::fromJson(m_metadata_reply->readAll()).object();
  const QJsonArray tile_templates =
      metadata.value(QStringLiteral("tiles")).toArray();
  const QString tile_template =
      tile_templates.isEmpty() ? QString() : tile_templates.first().toString();
  const int maximum_zoom =
      std::clamp(metadata.value(QStringLiteral("maxzoom")).toInt(14), 1, 14);
  m_attribution = plain_attribution(
      metadata.value(QStringLiteral("attribution")).toString());
  m_metadata_reply->deleteLater();
  m_metadata_reply = nullptr;
  if (tile_template.isEmpty()) {
    fail_or_retry();
    return;
  }

  m_tile_zoom = std::min(fit_zoom(m_bounds, QSize(1024, 1024)), maximum_zoom);
  for (;;) {
    m_tile_min_x = tile_x(m_bounds.west, m_tile_zoom);
    m_tile_max_x = tile_x(m_bounds.east, m_tile_zoom);
    m_tile_min_y = tile_y(m_bounds.north, m_tile_zoom);
    m_tile_max_y = tile_y(m_bounds.south, m_tile_zoom);
    const int count =
        (m_tile_max_x - m_tile_min_x + 1) * (m_tile_max_y - m_tile_min_y + 1);
    if (count <= 16 || m_tile_zoom <= 1) {
      break;
    }
    --m_tile_zoom;
  }

  const int columns = m_tile_max_x - m_tile_min_x + 1;
  const int rows = m_tile_max_y - m_tile_min_y + 1;
  const int tile_count = columns * rows;
  m_tile_images.resize(static_cast<std::size_t>(tile_count));
  m_tile_replies.resize(static_cast<std::size_t>(tile_count), nullptr);
  for (int row = 0; row < rows; ++row) {
    for (int column = 0; column < columns; ++column) {
      const int index = row * columns + column;
      QString url_text = tile_template;
      url_text.replace(QStringLiteral("{z}"), QString::number(m_tile_zoom));
      url_text.replace(QStringLiteral("{x}"),
                       QString::number(m_tile_min_x + column));
      url_text.replace(QStringLiteral("{y}"),
                       QString::number(m_tile_min_y + row));
      url_text.replace(QStringLiteral("{key}"), m_api_key);
      QUrl tile_url(url_text);
      QUrlQuery query(tile_url);
      if (!query.hasQueryItem(QStringLiteral("key"))) {
        query.addQueryItem(QStringLiteral("key"), m_api_key);
        tile_url.setQuery(query);
      }
      QNetworkRequest request(tile_url);
      request.setTransferTimeout(15000);
      request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                           QNetworkRequest::PreferCache);
      QNetworkReply *reply = m_network->get(request);
      m_tile_replies[static_cast<std::size_t>(index)] = reply;
      connect(reply, &QNetworkReply::finished, this,
              [this, index]() { on_tile_finished(index); });
    }
  }
  m_metadata_complete = true;
}

void MapTilerTerrainProvider::on_tile_finished(int index) {
  if (!m_active || index < 0 ||
      index >= static_cast<int>(m_tile_replies.size())) {
    return;
  }
  QNetworkReply *reply = m_tile_replies[static_cast<std::size_t>(index)];
  if (!reply) {
    return;
  }
  if (reply->error() != QNetworkReply::NoError) {
    fail_or_retry();
    return;
  }
  const QImage tile = QImage::fromData(reply->readAll());
  if (tile.isNull()) {
    fail_or_retry();
    return;
  }
  m_tile_images[static_cast<std::size_t>(index)] =
      tile.convertToFormat(QImage::Format_RGB888);
  reply->deleteLater();
  m_tile_replies[static_cast<std::size_t>(index)] = nullptr;
  ++m_completed_tiles;
  finish_if_ready();
}

void MapTilerTerrainProvider::finish_if_ready() {
  if (!m_active || !m_imagery_complete || !m_metadata_complete ||
      m_completed_tiles != static_cast<int>(m_tile_images.size())) {
    return;
  }
  TerrainPackage terrain;
  terrain.texture = QImage::fromData(m_imagery_bytes);
  if (terrain.texture.isNull() || m_tile_images.empty()) {
    fail_or_retry();
    return;
  }

  const int columns = m_tile_max_x - m_tile_min_x + 1;
  const int rows = m_tile_max_y - m_tile_min_y + 1;
  const QSize tile_size = m_tile_images.front().size();
  if (!tile_size.isValid()) {
    fail_or_retry();
    return;
  }
  QImage encoded_dem(columns * tile_size.width(), rows * tile_size.height(),
                     QImage::Format_RGB888);
  encoded_dem.fill(Qt::black);
  QPainter painter(&encoded_dem);
  for (int row = 0; row < rows; ++row) {
    for (int column = 0; column < columns; ++column) {
      const QImage &tile =
          m_tile_images[static_cast<std::size_t>(row * columns + column)];
      if (tile.size() != tile_size) {
        fail_or_retry();
        return;
      }
      painter.drawImage(column * tile_size.width(), row * tile_size.height(),
                        tile);
    }
  }
  painter.end();

  const double world_tiles = static_cast<double>(1 << m_tile_zoom);
  const double world_pixels =
      world_tiles * static_cast<double>(tile_size.width());
  const double left = (m_bounds.west + 180.0) / 360.0 * world_pixels -
                      static_cast<double>(m_tile_min_x * tile_size.width());
  const double right = (m_bounds.east + 180.0) / 360.0 * world_pixels -
                       static_cast<double>(m_tile_min_x * tile_size.width());
  const double top = mercator_y(m_bounds.north) * world_pixels -
                     static_cast<double>(m_tile_min_y * tile_size.height());
  const double bottom = mercator_y(m_bounds.south) * world_pixels -
                        static_cast<double>(m_tile_min_y * tile_size.height());
  const QRect crop(std::clamp(static_cast<int>(std::floor(left)), 0,
                              encoded_dem.width() - 1),
                   std::clamp(static_cast<int>(std::floor(top)), 0,
                              encoded_dem.height() - 1),
                   std::max(1, static_cast<int>(std::ceil(right - left))),
                   std::max(1, static_cast<int>(std::ceil(bottom - top))));
  const QRect bounded_crop = crop.intersected(encoded_dem.rect());
  if (!bounded_crop.isValid()) {
    fail_or_retry();
    return;
  }

  constexpr int mesh_size = 128;
  std::array<double, mesh_size * mesh_size> elevations{};
  double minimum = std::numeric_limits<double>::infinity();
  double maximum = -std::numeric_limits<double>::infinity();
  for (int y = 0; y < mesh_size; ++y) {
    const int source_y = bounded_crop.top() +
                         static_cast<int>(std::llround(
                             static_cast<double>(y) *
                             static_cast<double>(bounded_crop.height() - 1) /
                             static_cast<double>(mesh_size - 1)));
    for (int x = 0; x < mesh_size; ++x) {
      const int source_x = bounded_crop.left() +
                           static_cast<int>(std::llround(
                               static_cast<double>(x) *
                               static_cast<double>(bounded_crop.width() - 1) /
                               static_cast<double>(mesh_size - 1)));
      const QColor color = encoded_dem.pixelColor(source_x, source_y);
      const double elevation =
          -10000.0 + (static_cast<double>(color.red()) * 65536.0 +
                      static_cast<double>(color.green()) * 256.0 +
                      static_cast<double>(color.blue())) *
                         0.1;
      elevations[static_cast<std::size_t>(y * mesh_size + x)] = elevation;
      minimum = std::min(minimum, elevation);
      maximum = std::max(maximum, elevation);
    }
  }
  const double span = std::max(1.0, maximum - minimum);
  terrain.height_map = QImage(mesh_size, mesh_size, QImage::Format_Grayscale8);
  for (int y = 0; y < mesh_size; ++y) {
    for (int x = 0; x < mesh_size; ++x) {
      const double elevation =
          elevations[static_cast<std::size_t>(y * mesh_size + x)];
      const int gray = std::clamp(
          static_cast<int>(std::lround((elevation - minimum) / span * 255.0)),
          0, 255);
      terrain.height_map.setPixelColor(x, y, QColor(gray, gray, gray));
    }
  }
  terrain.height_span_m = span;
  terrain.bounds = m_bounds;
  terrain.attribution = m_attribution;
  if (!terrain.attribution.contains(QStringLiteral("MapTiler"),
                                    Qt::CaseInsensitive)) {
    terrain.attribution.prepend(QStringLiteral("© MapTiler "));
  }
  if (!terrain.attribution.contains(QStringLiteral("OpenStreetMap"),
                                    Qt::CaseInsensitive)) {
    terrain.attribution.append(QStringLiteral(" © OpenStreetMap contributors"));
  }
  terrain.real_terrain = true;
  m_active = false;
  m_api_key.clear();
  m_tile_images.clear();
  m_tile_replies.clear();
  emit ready(terrain);
}

void MapTilerTerrainProvider::fail_or_retry() {
  if (!m_active) {
    return;
  }
  clear_replies();
  if (m_attempt < 2) {
    QTimer::singleShot(500, this, &MapTilerTerrainProvider::begin_attempt);
    return;
  }
  m_active = false;
  m_api_key.clear();
  emit failed(tr("Real terrain could not be downloaded or validated. Retry or "
                 "use the private scene."));
}

void MapTilerTerrainProvider::clear_replies() {
  for (QNetworkReply **reply : {&m_imagery_reply, &m_metadata_reply}) {
    if (*reply) {
      (*reply)->disconnect(this);
      (*reply)->abort();
      (*reply)->deleteLater();
      *reply = nullptr;
    }
  }
  for (QNetworkReply *reply : m_tile_replies) {
    if (reply) {
      reply->disconnect(this);
      reply->abort();
      reply->deleteLater();
    }
  }
  m_tile_replies.clear();
  m_tile_images.clear();
  m_completed_tiles = 0;
}

} // namespace cosmo::video
