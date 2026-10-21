#include <catch2/catch_test_macros.hpp>

#include "services/persistence/FlightLogManager.h"
#include "services/persistence/FlightMetadata.h"
#include "services/persistence/MissionState.h"
#include "services/persistence/PreflightChecklist.h"
#include "services/persistence/RecoveryState.h"

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

TEST_CASE("Recording saves cannot erase newer data or another session",
          "[mission][persistence]") {
  cosmo::mission::RecordingRevision revision;
  REQUIRE_FALSE(revision.dirty());
  revision.changed();
  REQUIRE(revision.dirty());
  const auto first = revision.snapshot();
  revision.changed();
  revision.saved(first);
  REQUIRE(revision.dirty());
  revision.saved(revision.snapshot());
  REQUIRE_FALSE(revision.dirty());
  revision.reset();
  revision.changed();
  revision.saved(first);
  REQUIRE(revision.dirty());
  revision.reset();
  REQUIRE_FALSE(revision.dirty());
}

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
    REQUIRE(exported.startsWith("timestamp_ms,altitude,temperature,pressure,"
                                "acceleration_x,acceleration_y,"));
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

TEST_CASE(
    "Flight metadata belongs to the exact recording and survives reopening",
    "[mission][metadata]") {
  QTemporaryDir dir;
  const auto path = dir.filePath("flight.csv");
  FlightLogManager manager;
  manager.setOutputPath(path.toStdString());
  REQUIRE(manager.exportSessionToCsv(sessionWithOneSample()));
  const cosmo::mission::FlightMetadata metadata{
      QJsonArray{QJsonObject{{"time", 1.25}, {"name", "Motor cutoff"}}},
      QJsonObject{{"lat", 51.9}, {"lon", -8.5}}};
  REQUIRE(cosmo::mission::saveMetadata(path, metadata));
  const auto loaded = cosmo::mission::loadMetadata(path);
  REQUIRE(loaded.has_value());
  REQUIRE(loaded->events == metadata.events);
  REQUIRE(loaded->reference == metadata.reference);
  QFile changed(path);
  REQUIRE(changed.open(QIODevice::Append));
  REQUIRE(changed.write("different recording\n") > 0);
  changed.close();
  REQUIRE_FALSE(cosmo::mission::loadMetadata(path).has_value());
  REQUIRE_FALSE(cosmo::mission::saveMetadata(dir.filePath("missing/flight.csv"),
                                             metadata));
  const auto noSidecar = dir.filePath("plain.csv");
  manager.setOutputPath(noSidecar.toStdString());
  REQUIRE(manager.exportSessionToCsv(sessionWithOneSample()));
  REQUIRE(cosmo::mission::loadMetadata(noSidecar)->events.isEmpty());
}

TEST_CASE(
    "Recovery preserves real fixes across invalid samples demo and restarts",
    "[mission][recovery]") {
  cosmo::mission::RecoveryState state;
  state.beginSession("first", "Serial receiver");
  FlightSample fix;
  fix.coordinates = {51.9, -8.5};
  REQUIRE(state.observe(fix, 1000, true));
  state.setReference(
      QJsonObject{{"lat", 51.8}, {"lon", -8.4}, {"name", "ground station"}});
  const auto saved = state.json();
  fix.coordinates = {0, 0};
  REQUIRE_FALSE(state.observe(fix, 2000, true));
  fix.coordinates = {52, -8};
  REQUIRE_FALSE(state.observe(fix, 3000, false));
  REQUIRE(state.json() == saved);
  cosmo::mission::RecoveryState reopened;
  REQUIRE(reopened.restore(saved));
  REQUIRE(reopened.json().value("receivedUtcMs").toDouble() == 1000);
  REQUIRE(reopened.json().value("earlier").toBool());
  REQUIRE(reopened.json().value("reference") == saved.value("reference"));
  REQUIRE(reopened.json().value("source").toString() == "Serial receiver");
  reopened.beginSession("second", "Other receiver");
  REQUIRE(reopened.observe(fix, 4000, true));
  REQUIRE(reopened.json().value("session").toString() == "second");
  REQUIRE(
      reopened.json().value("reference").toObject().value("name").toString() ==
      "launch point");
  REQUIRE_FALSE(reopened.restore(QJsonObject{{"lat", 91}, {"lon", 0}}));
  const auto [distance, bearing] = cosmo::mission::rangeBearing(0, 0, 0, 1);
  REQUIRE(distance > 111000);
  REQUIRE(distance < 112000);
  REQUIRE(std::abs(bearing - 90) < 0.001);
}

TEST_CASE("Checklist profiles validate thresholds and keep unknown "
          "measurements unknown",
          "[mission][checklist]") {
  using namespace cosmo::mission;
  REQUIRE(validChecklists(defaultChecklists()));
  REQUIRE_FALSE(validChecklists({}));
  REQUIRE_FALSE(validChecklists(QJsonArray{
      QJsonObject{{"name", "Invalid"},
                  {"items", QJsonArray{QJsonObject{{"name", "Voltage"},
                                                   {"kind", "battery"},
                                                   {"required", true},
                                                   {"limit", -1}}}}}}));
  REQUIRE_FALSE(measuredCheck("battery", 7.0, {}, {}, false).has_value());
  REQUIRE(measuredCheck("battery", 7.0, {}, 7.4, false) == true);
  REQUIRE(measuredCheck("packetAge", 2, 3.0, {}, false) == false);
  REQUIRE_FALSE(measuredCheck("gps", 0, {}, {}, false).has_value());
  REQUIRE(measuredCheck("gps", 0, {}, {}, true) == true);
}
