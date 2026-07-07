#include <catch2/catch_test_macros.hpp>

#include "services/video/MapTilerTerrainProvider.h"

#include <QBuffer>
#include <QEventLoop>
#include <QImage>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

#include <algorithm>
#include <cstring>
#include <memory>

namespace {

class CountingNetworkAccessManager final : public QNetworkAccessManager {
public:
  int request_count = 0;

protected:
  QNetworkReply *createRequest(Operation operation,
                               const QNetworkRequest &request,
                               QIODevice *outgoing_data) override {
    ++request_count;
    return QNetworkAccessManager::createRequest(operation, request,
                                                outgoing_data);
  }
};

class FakeReply final : public QNetworkReply {
public:
  FakeReply(const QNetworkRequest &request, QByteArray body, QObject *parent)
      : QNetworkReply(parent), m_body(std::move(body)) {
    setRequest(request);
    setUrl(request.url());
    open(QIODevice::ReadOnly);
    QTimer::singleShot(0, this, [this]() {
      setFinished(true);
      emit readyRead();
      emit finished();
    });
  }

  void abort() override {}

  qint64 bytesAvailable() const override {
    return static_cast<qint64>(m_body.size()) - m_offset +
           QNetworkReply::bytesAvailable();
  }

protected:
  qint64 readData(char *data, qint64 maximum_size) override {
    if (m_offset >= m_body.size())
      return -1;
    const qint64 count =
        std::min(maximum_size, static_cast<qint64>(m_body.size()) - m_offset);
    std::memcpy(data, m_body.constData() + m_offset,
                static_cast<std::size_t>(count));
    m_offset += count;
    return count;
  }

private:
  QByteArray m_body;
  qint64 m_offset = 0;
};

[[nodiscard]] QByteArray png_bytes(const QImage &image) {
  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  image.save(&buffer, "PNG");
  return bytes;
}

class SuccessfulMapTilerNetwork final : public QNetworkAccessManager {
public:
  int request_count = 0;

protected:
  QNetworkReply *createRequest(Operation, const QNetworkRequest &request,
                               QIODevice *) override {
    ++request_count;
    QByteArray body;
    const QString path = request.url().path();
    if (path.endsWith(QStringLiteral("tiles.json"))) {
      body = QByteArrayLiteral(R"({
                "tiles": ["https://fake.test/terrain/{z}/{x}/{y}.webp?key={key}"],
                "maxzoom": 2,
                "attribution": "&copy; MapTiler test metadata"
            })");
    } else if (path.contains(QStringLiteral("/terrain/"))) {
      QImage terrain(16, 16, QImage::Format_RGB888);
      for (int y = 0; y < terrain.height(); ++y) {
        for (int x = 0; x < terrain.width(); ++x) {
          const int encoded = 100000 + x * 100 + y;
          terrain.setPixelColor(x, y,
                                QColor((encoded >> 16) & 0xff,
                                       (encoded >> 8) & 0xff, encoded & 0xff));
        }
      }
      body = png_bytes(terrain);
    } else {
      QImage imagery(64, 64, QImage::Format_RGB32);
      imagery.fill(QColor(30, 60, 90));
      body = png_bytes(imagery);
    }
    return std::make_unique<FakeReply>(request, body, this).release();
  }
};

class InvalidMapTilerNetwork final : public QNetworkAccessManager {
public:
  int request_count = 0;

protected:
  QNetworkReply *createRequest(Operation, const QNetworkRequest &request,
                               QIODevice *) override {
    ++request_count;
    return std::make_unique<FakeReply>(
               request, QByteArrayLiteral("invalid response"), this)
        .release();
  }
};

} // namespace

TEST_CASE("MapTilerTerrainProvider makes no request without a key",
          "[video][terrain]") {
  CountingNetworkAccessManager network;
  cosmo::video::MapTilerTerrainProvider provider(&network);
  QString failure;
  QObject::connect(&provider, &cosmo::video::ITerrainProvider::failed,
                   &provider,
                   [&](const QString &message) { failure = message; });

  provider.request_terrain({51.0, -1.0, 51.1, -0.9}, QSize(1280, 720),
                           QString());

  REQUIRE(network.request_count == 0);
  REQUIRE_FALSE(failure.isEmpty());
}

TEST_CASE(
    "MapTilerTerrainProvider rejects polar bounds without exposing the key",
    "[video][terrain]") {
  CountingNetworkAccessManager network;
  cosmo::video::MapTilerTerrainProvider provider(&network);
  const QString secret = QStringLiteral("do-not-log-this-key");
  QString failure;
  QObject::connect(&provider, &cosmo::video::ITerrainProvider::failed,
                   &provider,
                   [&](const QString &message) { failure = message; });

  provider.request_terrain({85.1, -1.0, 86.0, -0.9}, QSize(1280, 720), secret);

  REQUIRE(network.request_count == 0);
  REQUIRE_FALSE(failure.contains(secret));
}

TEST_CASE(
    "MapTilerTerrainProvider rejects dateline bounds without a network request",
    "[video][terrain]") {
  CountingNetworkAccessManager network;
  cosmo::video::MapTilerTerrainProvider provider(&network);
  QString failure;
  QObject::connect(&provider, &cosmo::video::ITerrainProvider::failed,
                   &provider,
                   [&](const QString &message) { failure = message; });

  provider.request_terrain({-10.0, 179.5, 10.0, -179.5}, QSize(1280, 720),
                           QStringLiteral("test-key"));

  REQUIRE(network.request_count == 0);
  REQUIRE_FALSE(failure.isEmpty());
}

TEST_CASE("MapTilerTerrainProvider composes and decodes bounded terrain tiles",
          "[video][terrain]") {
  SuccessfulMapTilerNetwork network;
  cosmo::video::MapTilerTerrainProvider provider(&network);
  cosmo::video::TerrainPackage result;
  QString failure;
  QEventLoop loop;
  QObject::connect(&provider, &cosmo::video::ITerrainProvider::ready, &loop,
                   [&](const cosmo::video::TerrainPackage &terrain) {
                     result = terrain;
                     loop.quit();
                   });
  QObject::connect(&provider, &cosmo::video::ITerrainProvider::failed, &loop,
                   [&](const QString &message) {
                     failure = message;
                     loop.quit();
                   });
  QTimer::singleShot(3000, &loop, &QEventLoop::quit);

  provider.request_terrain({0.0, -90.0, 60.0, 0.0}, QSize(1280, 720),
                           QStringLiteral("test-key"));
  loop.exec();

  INFO(failure.toStdString());
  REQUIRE(failure.isEmpty());
  REQUIRE(network.request_count == 6);
  REQUIRE(result.real_terrain);
  REQUIRE(result.texture.size() == QSize(64, 64));
  REQUIRE(result.height_map.size() == QSize(128, 128));
  REQUIRE(result.height_span_m > 1.0);
  REQUIRE(
      result.attribution.contains(QStringLiteral("MapTiler test metadata")));
}

TEST_CASE(
    "MapTilerTerrainProvider retries invalid provider data and redacts the key",
    "[video][terrain]") {
  InvalidMapTilerNetwork network;
  cosmo::video::MapTilerTerrainProvider provider(&network);
  const QString secret = QStringLiteral("retry-secret-key");
  QString failure;
  QEventLoop loop;
  QObject::connect(&provider, &cosmo::video::ITerrainProvider::failed, &loop,
                   [&](const QString &message) {
                     failure = message;
                     loop.quit();
                   });
  QTimer::singleShot(3000, &loop, &QEventLoop::quit);

  provider.request_terrain({51.0, -1.0, 51.1, -0.9}, QSize(1280, 720), secret);
  loop.exec();

  REQUIRE(network.request_count == 4);
  REQUIRE_FALSE(failure.isEmpty());
  REQUIRE_FALSE(failure.contains(secret));
}
