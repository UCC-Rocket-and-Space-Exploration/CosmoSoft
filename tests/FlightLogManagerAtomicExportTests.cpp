#include <catch2/catch_test_macros.hpp>

#include "services/persistence/FlightLogManager.h"

#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTemporaryFile>

#include <string>
#include <utility>

namespace {

class PermissionRestorer {
public:
    PermissionRestorer(QString path, QFileDevice::Permissions permissions)
        : m_path(std::move(path)),
          m_permissions(permissions) {}

    ~PermissionRestorer() {
        if (m_active) {
            static_cast<void>(QFile::setPermissions(m_path, m_permissions));
        }
    }

    PermissionRestorer(const PermissionRestorer &) = delete;
    PermissionRestorer &operator=(const PermissionRestorer &) = delete;

    [[nodiscard]] bool restore() {
        if (!m_active) {
            return true;
        }
        if (!QFile::setPermissions(m_path, m_permissions)) {
            return false;
        }
        m_active = false;
        return true;
    }

private:
    QString m_path;
    QFileDevice::Permissions m_permissions;
    bool m_active = true;
};

[[nodiscard]] QByteArray readAll(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

[[nodiscard]] FlightSession sessionWithOneSample() {
    FlightSample sample;
    sample.timestamp = 123;
    sample.altitude = 456.5;
    sample.temperature = 21.25;
    sample.pressure = 101325.0;
    sample.acceleration = {1.0, 2.0, 3.0};
    sample.coordinates = {51.9, -8.5};
    sample.batteryVoltage = 7.4;
    sample.rssi = -42.0;
    sample.angularVelocity = {4.0, 5.0, 6.0};

    FlightSession session;
    session.samples.push_back(sample);
    return session;
}

} // namespace

TEST_CASE("FlightLogManager transactionally replaces a CSV destination",
          "[persistence][export]") {
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());

    const QString path = temporary.filePath(QStringLiteral("flight.csv"));
    QFile previous(path);
    REQUIRE(previous.open(QIODevice::WriteOnly));
    REQUIRE(previous.write("previous-content\n") > 0);
    previous.close();

    FlightLogManager manager;
    manager.setOutputPath(path.toStdString());
    REQUIRE(manager.exportSessionToCsv(sessionWithOneSample()));

    const QByteArray exported = readAll(path);
    REQUIRE(exported.startsWith(
        "time,altitude,temperature,pressure,acceleration_x,acceleration_y,"));
    REQUIRE(exported.contains("123,456.5,21.25,101325"));
    REQUIRE_FALSE(exported.contains("previous-content"));

    const QStringList entries = QDir(temporary.path()).entryList(
        QDir::Files | QDir::NoDotAndDotDot);
    REQUIRE(entries == QStringList{QStringLiteral("flight.csv")});
}

TEST_CASE("FlightLogManager preserves an existing CSV when transaction creation fails",
          "[persistence][export]") {
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());

    const QString path = temporary.filePath(QStringLiteral("flight.csv"));
    const QByteArray original("irreplaceable-existing-content\n");
    QFile destination(path);
    REQUIRE(destination.open(QIODevice::WriteOnly));
    REQUIRE(destination.write(original) == original.size());
    destination.close();

    const QFileDevice::Permissions originalPermissions =
        QFile::permissions(temporary.path());
    PermissionRestorer restorePermissions(temporary.path(), originalPermissions);
    const QFileDevice::Permissions readOnlyDirectory =
        QFileDevice::ReadOwner | QFileDevice::ExeOwner;
    REQUIRE(QFile::setPermissions(temporary.path(), readOnlyDirectory));

    // Some privileged or non-POSIX environments do not enforce directory
    // write permission bits. Capability-check that condition so this test
    // never risks replacing the fixture it is intended to protect.
    QTemporaryFile permissionProbe(
        temporary.filePath(QStringLiteral("permission-probe.XXXXXX")));
    if (permissionProbe.open()) {
        permissionProbe.close();
        REQUIRE(restorePermissions.restore());
        SKIP("Directory write permissions are not enforced in this environment");
    }

    FlightLogManager manager;
    manager.setOutputPath(path.toStdString());
    const bool exported = manager.exportSessionToCsv(sessionWithOneSample());

    REQUIRE(restorePermissions.restore());
    REQUIRE_FALSE(exported);
    REQUIRE(readAll(path) == original);
}

TEST_CASE("FlightLogManager discards serialized output when the final commit fails",
          "[persistence][export]") {
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());

    // A non-empty directory at the destination allows QSaveFile to create and
    // populate its sibling temporary file, but cannot be replaced at commit.
    const QString destination = temporary.filePath(QStringLiteral("flight.csv"));
    REQUIRE(QDir().mkpath(destination));
    const QString sentinelPath =
        QDir(destination).filePath(QStringLiteral("existing-content.txt"));
    const QByteArray original("existing-destination-content\n");
    QFile sentinel(sentinelPath);
    REQUIRE(sentinel.open(QIODevice::WriteOnly));
    REQUIRE(sentinel.write(original) == original.size());
    sentinel.close();

    FlightLogManager manager;
    manager.setOutputPath(destination.toStdString());
    REQUIRE_FALSE(manager.exportSessionToCsv(sessionWithOneSample()));

    REQUIRE(QFileInfo(destination).isDir());
    REQUIRE(readAll(sentinelPath) == original);
    const QStringList parentEntries = QDir(temporary.path()).entryList(
        QDir::AllEntries | QDir::NoDotAndDotDot);
    REQUIRE(parentEntries == QStringList{QStringLiteral("flight.csv")});
}

TEST_CASE("FlightLogManager bounds live recording and accepts samples after clear",
          "[persistence][recording]") {
    FlightLogManager manager(2U);
    FlightSample first;
    first.timestamp = 1;
    FlightSample second;
    second.timestamp = 2;
    FlightSample overflow;
    overflow.timestamp = 3;

    REQUIRE(manager.appendSample(first));
    REQUIRE(manager.appendSample(second));
    REQUIRE_FALSE(manager.appendSample(overflow));
    REQUIRE(manager.session().samples.size() == 2U);
    REQUIRE(manager.session().samples.back().timestamp == 2);

    manager.clear();
    REQUIRE(manager.session().samples.empty());
    REQUIRE(manager.appendSample(overflow));
    REQUIRE(manager.session().samples.size() == 1U);
    REQUIRE(manager.session().samples.front().timestamp == 3);
}
