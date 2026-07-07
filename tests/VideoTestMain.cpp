#include <catch2/catch_session.hpp>

#include <QByteArray>
#include <QGuiApplication>

int main(int argc, char *argv[]) {
  qputenv("QT_MEDIA_BACKEND", QByteArrayLiteral("ffmpeg"));
  QGuiApplication application(argc, argv);
  return Catch::Session().run(argc, argv);
}
