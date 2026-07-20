#include <catch2/catch_test_macros.hpp>

#include "gui/FlightReplayController.h"

#include <QCoreApplication>
#include <QMetaObject>

#include <initializer_list>
#include <memory>
#include <utility>
#include <vector>

namespace {

struct ReplayFixture {
    std::shared_ptr<const FlightSession> session;
    std::shared_ptr<const cosmo::preview::FlightPreviewCache> preview;
};

void ensureCoreApplication()
{
    if (QCoreApplication::instance() != nullptr) {
        return;
    }

    static int argumentCount = 1;
    static char applicationName[] = "cosmo-replay-tests";
    static char *arguments[] = {applicationName, nullptr};
    static QCoreApplication application(argumentCount, arguments);
    (void)application;
}

[[nodiscard]] ReplayFixture makeReplayFixture(
    const std::initializer_list<long> timestamps)
{
    auto session = std::make_shared<FlightSession>();
    session->samples.reserve(timestamps.size());
    for (const long timestamp : timestamps) {
        FlightSample sample;
        sample.timestamp = timestamp;
        session->samples.push_back(sample);
    }

    auto preview = cosmo::preview::FlightPreviewCache::build(*session);
    return {std::move(session), std::move(preview)};
}

[[nodiscard]] bool invokeTimerTick(FlightReplayController &controller)
{
    return QMetaObject::invokeMethod(
        &controller,
        "onTimerTick",
        Qt::DirectConnection);
}

} // namespace

TEST_CASE("FlightReplayController maps elapsed time with bounded UI updates",
          "[gui][replay]")
{
    ensureCoreApplication();
    qint64 clockMilliseconds = 1000;
    FlightReplayController controller(
        [&clockMilliseconds] { return clockMilliseconds; },
        nullptr);
    const ReplayFixture replay = makeReplayFixture(
        {0, 10, 20, 30, 40, 50, 60, 70, 80, 90});
    REQUIRE(replay.preview);

    std::vector<int> positions;
    int finishedCount = 0;
    QObject::connect(
        &controller,
        &FlightReplayController::positionChanged,
        [&positions](const int position) { positions.push_back(position); });
    QObject::connect(
        &controller,
        &FlightReplayController::playbackFinished,
        [&finishedCount] { ++finishedCount; });

    controller.setSession(replay.session, replay.preview);
    positions.clear();
    controller.play();
    REQUIRE((positions == std::vector<int>{1}));

    REQUIRE(invokeTimerTick(controller));
    REQUIRE((positions == std::vector<int>{1}));

    clockMilliseconds = 1033;
    REQUIRE(invokeTimerTick(controller));
    REQUIRE(controller.index() == 4);
    REQUIRE((positions == std::vector<int>{1, 4}));

    REQUIRE(invokeTimerTick(controller));
    REQUIRE((positions == std::vector<int>{1, 4}));

    clockMilliseconds = 1066;
    REQUIRE(invokeTimerTick(controller));
    REQUIRE(controller.index() == 7);

    clockMilliseconds = 1090;
    REQUIRE(invokeTimerTick(controller));
    REQUIRE(controller.index() == controller.sampleCount());
    REQUIRE_FALSE(controller.isPlaying());
    REQUIRE(finishedCount == 1);
    REQUIRE((positions == std::vector<int>{1, 4, 7, 10}));
}

TEST_CASE("FlightReplayController follows the corrected preview timeline",
          "[gui][replay]")
{
    ensureCoreApplication();
    qint64 clockMilliseconds = 0;
    FlightReplayController controller(
        [&clockMilliseconds] { return clockMilliseconds; },
        nullptr);
    const ReplayFixture replay = makeReplayFixture({1000, 900, 1100});
    REQUIRE(replay.preview);
    REQUIRE(replay.preview->correctedTimelineUsed());

    std::vector<int> positions;
    QObject::connect(
        &controller,
        &FlightReplayController::positionChanged,
        [&positions](const int position) { positions.push_back(position); });

    controller.setSession(replay.session, replay.preview);
    positions.clear();
    controller.play();
    REQUIRE((positions == std::vector<int>{1}));

    clockMilliseconds = 199;
    REQUIRE(invokeTimerTick(controller));
    REQUIRE((positions == std::vector<int>{1}));

    clockMilliseconds = 200;
    REQUIRE(invokeTimerTick(controller));
    REQUIRE((positions == std::vector<int>{1, 2}));

    clockMilliseconds = 400;
    REQUIRE(invokeTimerTick(controller));
    REQUIRE((positions == std::vector<int>{1, 2, 3}));
    REQUIRE_FALSE(controller.isPlaying());
}

TEST_CASE("FlightReplayController preserves time across speed pause resume and seek",
          "[gui][replay]")
{
    ensureCoreApplication();
    qint64 clockMilliseconds = 0;
    FlightReplayController controller(
        [&clockMilliseconds] { return clockMilliseconds; },
        nullptr);
    const ReplayFixture replay = makeReplayFixture({0, 100, 200, 300, 400});
    REQUIRE(replay.preview);

    std::vector<int> positions;
    int startedCount = 0;
    int pausedCount = 0;
    int finishedCount = 0;
    QObject::connect(
        &controller,
        &FlightReplayController::positionChanged,
        [&positions](const int position) { positions.push_back(position); });
    QObject::connect(
        &controller,
        &FlightReplayController::playbackStarted,
        [&startedCount] { ++startedCount; });
    QObject::connect(
        &controller,
        &FlightReplayController::playbackPaused,
        [&pausedCount] { ++pausedCount; });
    QObject::connect(
        &controller,
        &FlightReplayController::playbackFinished,
        [&finishedCount] { ++finishedCount; });

    controller.setSession(replay.session, replay.preview);
    positions.clear();
    controller.play();
    REQUIRE((positions == std::vector<int>{1}));

    clockMilliseconds = 50;
    controller.setSpeed(2.0);
    REQUIRE(controller.speed() == 2.0);

    clockMilliseconds = 75;
    REQUIRE(invokeTimerTick(controller));
    REQUIRE(controller.index() == 2);

    clockMilliseconds = 100;
    controller.pause();
    REQUIRE_FALSE(controller.isPlaying());
    REQUIRE(pausedCount == 1);

    clockMilliseconds = 1100;
    controller.play();
    REQUIRE(controller.index() == 2);

    clockMilliseconds = 1125;
    REQUIRE(invokeTimerTick(controller));
    REQUIRE(controller.index() == 3);

    controller.setPosition(4);
    REQUIRE_FALSE(controller.isPlaying());
    REQUIRE(controller.index() == 4);
    REQUIRE(pausedCount == 2);
    controller.setPosition(4);
    REQUIRE((positions == std::vector<int>{1, 2, 3, 4}));

    controller.play();
    clockMilliseconds = 1175;
    REQUIRE(invokeTimerTick(controller));
    REQUIRE(controller.index() == 5);
    REQUIRE_FALSE(controller.isPlaying());
    REQUIRE(finishedCount == 1);
    REQUIRE(startedCount == 3);
    REQUIRE((positions == std::vector<int>{1, 2, 3, 4, 5}));

    controller.setSpeed(-1.0);
    REQUIRE(controller.speed() == 1.0);
    controller.play();
    REQUIRE(controller.isPlaying());
    REQUIRE((positions == std::vector<int>{1, 2, 3, 4, 5, 0, 1}));
}

TEST_CASE("FlightReplayController rejects a preview from another session",
          "[gui][replay]")
{
    ensureCoreApplication();
    qint64 clockMilliseconds = 0;
    FlightReplayController controller(
        [&clockMilliseconds] { return clockMilliseconds; },
        nullptr);
    const ReplayFixture first = makeReplayFixture({0, 10, 20});
    const ReplayFixture second = makeReplayFixture({0, 10, 20});

    QString error;
    QObject::connect(
        &controller,
        &FlightReplayController::errorOccurred,
        [&error](const QString &message) { error = message; });

    controller.setSession(first.session, second.preview);

    REQUIRE_FALSE(controller.hasSession());
    REQUIRE(controller.index() == 0);
    REQUIRE_FALSE(error.isEmpty());
}

TEST_CASE("FlightReplayController does not rewind when an injected clock regresses",
          "[gui][replay]")
{
    ensureCoreApplication();
    qint64 clockMilliseconds = 0;
    FlightReplayController controller(
        [&clockMilliseconds] { return clockMilliseconds; },
        nullptr);
    const ReplayFixture replay = makeReplayFixture({0, 100, 200});
    controller.setSession(replay.session, replay.preview);
    controller.play();

    clockMilliseconds = 100;
    REQUIRE(invokeTimerTick(controller));
    REQUIRE(controller.index() == 2);

    clockMilliseconds = 50;
    REQUIRE(invokeTimerTick(controller));
    REQUIRE(controller.index() == 2);
    controller.setSpeed(2.0);

    clockMilliseconds = 100;
    REQUIRE(invokeTimerTick(controller));
    REQUIRE(controller.index() == 3);
    REQUIRE_FALSE(controller.isPlaying());
}
