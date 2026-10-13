#include "gateway/comms/SerialFramerWorker.h"
#include "gui/FlightDataModel.h"
#include "services/import/SampleFileLoader.h"
#include "services/persistence/FlightLogManager.h"
#include "services/telemetry/FrameDecoderVault.h"
#include "services/telemetry/LineTelemetryBatchMailbox.h"
#include "services/telemetry/RecordedComms.h"
#include "services/telemetry/TelemetrySession.h"
#include "services/telemetry/frame_decoders/TeleMegaImuFrameDecoder.h"
#include "services/telemetry/framers/AltosFramer.h"
#include "shared/ByteHelper.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <future>
#include <mutex>

using namespace std::chrono_literals;
using namespace cosmo::telemetry;
namespace {
const std::string fixture =
    "TELEM "
    "2234127856082D9600A8610000FAFFD4FE6400F401CEFF2C01FA0019007B000000AAAA42";
Frame frameOf(const std::string &text) {
  return {AltosFrame, {text.begin(), text.end()}, 8};
}
std::string packet(uint16_t tick, uint8_t type = 8) {
  auto text = fixture;
  constexpr char hex[] = "0123456789ABCDEF";
  const auto setByte = [&](size_t offset, uint8_t byte) {
    text[8 + offset * 2] = hex[byte >> 4];
    text[9 + offset * 2] = hex[byte & 15];
  };
  setByte(2, tick & 255);
  setByte(3, tick >> 8);
  setByte(4, type);
  uint8_t sum = 0x5a;
  for (size_t i = 0; i < 34; ++i)
    sum += ByteHelper::get_byte_from_str(text.substr(8 + i * 2, 2));
  setByte(34, sum);
  return text;
}
void writeFile(const QString &path, const std::string &bytes) {
  QFile file(path);
  REQUIRE(file.open(QIODevice::WriteOnly));
  REQUIRE(file.write(bytes.data(), static_cast<qint64>(bytes.size())) ==
          static_cast<qint64>(bytes.size()));
}
void ensureApplication() {
  if (QCoreApplication::instance())
    return;
  static int argc = 1;
  static char name[] = "cosmo-recorded-tests";
  static char *argv[] = {name, nullptr};
  static QCoreApplication application(argc, argv);
}
// Delivers one byte per read, inserts timeouts, and can wait on an unfinished
// packet.
class FragmentedComms final : public IComms {
public:
  explicit FragmentedComms(std::string bytes, bool hold = false)
      : input(std::move(bytes)), hold(hold) {}
  std::function<void()> atBoundary, atEnd;
  bool timeouts = true;
  bool open() override {
    std::lock_guard lock(mutex);
    active = true;
    pos = calls = 0;
    return true;
  }
  void close() override {
    std::lock_guard lock(mutex);
    active = false;
    changed.notify_all();
  }
  bool isOpen() const override {
    std::lock_guard lock(mutex);
    return active;
  }
  ssize_t write(const uint8_t *, size_t) override { return -1; }
  std::string getDeviceName() const override { return "fragment fixture"; }
  ssize_t read(uint8_t *out, size_t size) override {
    std::unique_lock lock(mutex);
    if (!active || !size)
      return 0;
    if (timeouts && ++calls % 3 == 0)
      return 0;
    if (pos == fixture.size() && atBoundary) {
      auto hook = std::move(atBoundary);
      hook();
    }
    if (pos < input.size()) {
      *out = input[pos++];
      return 1;
    }
    if (atEnd) {
      auto hook = std::move(atEnd);
      hook();
    }
    waiting = true;
    changed.notify_all();
    if (hold)
      changed.wait(lock, [&] { return !active; });
    active = false;
    return 0;
  }
  bool waitForPartialRead() {
    std::unique_lock lock(mutex);
    return changed.wait_for(lock, 2s, [&] { return waiting; });
  }

private:
  std::string input;
  bool hold;
  mutable std::mutex mutex;
  std::condition_variable changed;
  bool active = false, waiting = false;
  size_t pos = 0, calls = 0;
};
} // namespace

TEST_CASE("IMU fixture has independently specified physical values and no "
          "fabricated axes",
          "[imu]") {
  TeleMegaImuFrameDecoder decoder;
  const auto sample = decoder.decode(frameOf(fixture));
  REQUIRE(sample.timestamp == 221360);
  REQUIRE(std::isnan(sample.pressure));
  REQUIRE(sample.temperature == Catch::Approx(-0.06));
  REQUIRE(sample.rssi == -117.0);
  REQUIRE(std::isnan(sample.altitude));
  REQUIRE(std::isnan(sample.coordinates.latitude));
  REQUIRE(std::isnan(sample.coordinates.longitude));
  REQUIRE(std::isnan(sample.acceleration.x));
  REQUIRE(std::isnan(sample.angularVelocity.z));
  REQUIRE(std::isnan(sample.batteryVoltage));
}
TEST_CASE("TELEM file import sets the payload offset and skips invalid packets",
          "[import][imu]") {
  QTemporaryDir directory;
  REQUIRE(directory.isValid());
  const auto path = directory.filePath("input.telem");
  auto badChecksum = fixture;
  badChecksum.back() = '3';
  auto badRadioCrc = fixture;
  badRadioCrc[74] = '2';
  badRadioCrc[76] = 'C';
  writeFile(path, "TELEM 223412\n" + badChecksum + "\n" + badRadioCrc + "\n" +
                      packet(100, 0xff) + "\n" + fixture + "\n");
  FlightSession session;
  FrameDecoderVault vault;
  const auto result = SampleFileLoader::loadTelemFile(
      path.toStdString(), session, vault, cosmo::CancellationCheck{});
  INFO(result.error.value_or("no error"));
  REQUIRE(result.succeeded());
  REQUIRE(session.samples.size() == 1);
  REQUIRE(session.samples.front().timestamp == 0);
  REQUIRE(session.samples.front().temperature == Catch::Approx(-0.06));
  REQUIRE(session.samples.front().rssi == -117.0);
}

TEST_CASE("TELEM file import reports an error when no packets decode",
          "[import][imu]") {
  QTemporaryDir directory;
  REQUIRE(directory.isValid());
  const auto path = directory.filePath("invalid.telem");
  writeFile(path, "TELEM 223412\n");
  FlightSession session;
  FrameDecoderVault vault;
  const auto result = SampleFileLoader::loadTelemFile(
      path.toStdString(), session, vault, cosmo::CancellationCheck{});
  REQUIRE(result.error.has_value());
  REQUIRE_FALSE(result.canceled);
  REQUIRE(session.samples.empty());
}

TEST_CASE("TELEM import normalizes rollover and excludes placeholder and "
          "reordered packets",
          "[import][imu]") {
  QTemporaryDir directory;
  REQUIRE(directory.isValid());
  const auto path = directory.filePath("rollover.telem");
  writeFile(path, packet(65530, 4) + "\n" + packet(65530) + "\n" + packet(4) +
                      "\n" + packet(3) + "\n" + packet(5, 5) + "\n" +
                      packet(6) + "\n");
  FlightSession session;
  FrameDecoderVault vault;
  const auto result = SampleFileLoader::loadTelemFile(
      path.toStdString(), session, vault, cosmo::CancellationCheck{});
  INFO(result.error.value_or("no error"));
  REQUIRE(result.succeeded());
  REQUIRE(session.samples.size() == 3);
  REQUIRE(session.samples[0].timestamp == 0);
  REQUIRE(session.samples[1].timestamp == 100);
  REQUIRE(session.samples[2].timestamp == 120);
  const std::array<bool, FlightSession::kTraceMetricCount> metrics{
      false, true, false, false, false, true, false, false, false};
  REQUIRE(session.metricsInSource == metrics);
  REQUIRE(std::isnan(session.samples.back().altitude));
}

TEST_CASE(
    "TELEM import cancels a large capture without replacing the active session",
    "[import][cancel]") {
  QTemporaryDir directory;
  REQUIRE(directory.isValid());
  const auto path = directory.filePath("large.telem");
  std::string input;
  for (int i = 0; i < 10000; ++i)
    input += packet(static_cast<uint16_t>(i)) + "\n";
  writeFile(path, input);
  FlightSession session;
  session.samples.resize(1);
  session.samples.front().timestamp = 4242;
  FrameDecoderVault vault;
  size_t polls = 0;
  const auto result = SampleFileLoader::loadTelemFile(
      path.toStdString(), session, vault, [&] { return ++polls == 6; });
  REQUIRE(result.canceled);
  REQUIRE_FALSE(result.error);
  REQUIRE(session.samples.size() == 1);
  REQUIRE(session.samples.front().timestamp == 4242);
}

TEST_CASE(
    "TELEM import rejects invalid input even when a previous session exists",
    "[import][imu]") {
  QTemporaryDir directory;
  REQUIRE(directory.isValid());
  const auto path = directory.filePath("invalid.telem");
  writeFile(path,
            "TELEM 223412\n" + packet(100, 4) + "\n" + packet(100, 5) + "\n");
  FlightSession session;
  session.samples.resize(1);
  session.samples.front().timestamp = 4242;
  FrameDecoderVault vault;
  const auto result = SampleFileLoader::loadTelemFile(
      path.toStdString(), session, vault, cosmo::CancellationCheck{});
  REQUIRE(result.error);
  REQUIRE(session.samples.size() == 1);
  REQUIRE(session.samples.front().timestamp == 4242);
}

TEST_CASE("TELEM import loads the recorded and TeleMini capture fixtures",
          "[import]") {
  for (const auto *name : {"recorded_imu.telem", "altos_sample_data.telem",
                           "altos_sample_data2.telem"}) {
    FlightSession session;
    FrameDecoderVault vault;
    const auto result = SampleFileLoader::loadTelemFile(
        std::string(COSMO_SOURCE_DIR) + "/sample_data/" + name, session, vault,
        cosmo::CancellationCheck{});
    INFO(name);
    INFO(result.error.value_or("no error"));
    REQUIRE(result.succeeded());
    REQUIRE_FALSE(session.samples.empty());
    REQUIRE(session.samples.front().timestamp == 0);
    for (size_t i = 1; i < session.samples.size(); ++i)
      REQUIRE(session.samples[i].timestamp >= session.samples[i - 1].timestamp);
  }
}

TEST_CASE("IMU rejects every truncation, bad hex, checksum, radio CRC, and "
          "unsupported type",
          "[imu]") {
  TeleMegaImuFrameDecoder decoder;
  for (size_t size = 0; size < fixture.size(); ++size)
    REQUIRE_THROWS(decoder.decode(frameOf(fixture.substr(0, size))));
  for (size_t i = 8; i < fixture.size(); ++i) {
    auto corrupt = fixture;
    corrupt[i] = 'G';
    REQUIRE_THROWS(decoder.decode(frameOf(corrupt)));
  }
  auto corrupt = fixture;
  corrupt.back() = '3';
  REQUIRE_THROWS(decoder.decode(frameOf(corrupt)));
  corrupt = fixture;
  corrupt[74] = '2';
  corrupt[76] = 'C'; // valid sum, clear radio CRC bit
  REQUIRE_THROWS(decoder.decode(frameOf(corrupt)));
  auto wrongIndex = frameOf(fixture);
  wrongIndex.packet_start_index = SIZE_MAX;
  REQUIRE_THROWS(decoder.decode(wrongIndex));
  FrameDecoderVault vault;
  for (auto type : {4, 5, 9, 0x12, 0x13, 0x14})
    REQUIRE_NOTHROW(vault.select(frameOf(packet(100, type))));
  for (auto type : {0xff})
    REQUIRE_THROWS(vault.select(frameOf(packet(100, type))));
  REQUIRE_THROWS(ByteHelper::get_byte_from_str("1g"));
  REQUIRE_THROWS(ByteHelper::get_byte_from_str("+1"));
}
TEST_CASE("AltOS framing retains fragments over timeouts and resynchronizes "
          "bounded noise",
          "[imu]") {
  auto input = std::make_shared<FragmentedComms>(
      std::string(1000, 'z') + "TELEM 2234" + fixture + "\n" + fixture);
  REQUIRE(input->open());
  AltosFramer framer(input);
  std::atomic_bool active{true};
  size_t frames = 0, errors = 0;
  while (!framer.finished()) {
    try {
      if (!framer.get_frame(active).data.empty())
        ++frames;
    } catch (const std::exception &) {
      ++errors;
    }
  }
  REQUIRE(frames == 2);
  REQUIRE(errors == 1);
  active = false;
  REQUIRE(framer.get_frame(active).data.empty());
}
TEST_CASE("IMU sessions reset rollover timing, summarize rejected packets, and "
          "finish without late callbacks",
          "[imu]") {
  QTemporaryDir directory;
  REQUIRE(directory.isValid());
  const auto path = directory.filePath("input.telem");
  writeFile(path, packet(65530) + "\n" + packet(4) + "\n" + packet(3) + "\n" +
                      packet(5, 4) + "\nTELEM 2234");
  std::vector<FlightSample> samples;
  std::vector<std::string> diagnostics;
  std::mutex mutex;
  std::condition_variable changed;
  bool done = false;
  TelemetrySession session(
      std::make_shared<RecordedComms>(path.toStdString(), 0ms),
      [&](LineTelemetryBatch batch) {
        samples.insert(samples.end(), batch.samples.begin(),
                       batch.samples.end());
      },
      [&](const std::string &message) { diagnostics.push_back(message); },
      [&] {
        std::lock_guard lock(mutex);
        done = true;
        changed.notify_all();
      });
  for (int run = 0; run < 3; ++run) {
    samples.clear();
    diagnostics.clear();
    done = false;
    REQUIRE(session.start());
    {
      std::unique_lock lock(mutex);
      REQUIRE(changed.wait_for(lock, 3s, [&] { return done; }));
    }
    session.stop();
    REQUIRE_FALSE(session.running());
    REQUIRE(samples.size() == 2);
    REQUIRE(samples[0].timestamp == 0);
    REQUIRE(samples[1].timestamp == 100);
    REQUIRE(diagnostics.size() >= 4);
  }
}
TEST_CASE("Stopping and destroying a session interrupts a pending partial read",
          "[imu]") {
  for (bool explicitStop : {false, true}) {
    auto input = std::make_shared<FragmentedComms>("TELEM 2234", true);
    auto session = std::make_unique<TelemetrySession>(
        input, [](LineTelemetryBatch) {}, [](const std::string &) {});
    REQUIRE(session->start());
    REQUIRE(input->waitForPartialRead());
    const auto begin = std::chrono::steady_clock::now();
    if (explicitStop)
      session->stop();
    session.reset();
    REQUIRE(std::chrono::steady_clock::now() - begin < 1s);
  }
}
TEST_CASE("Recorded input is read-only and its line pacing can be canceled",
          "[imu]") {
  QTemporaryDir directory;
  const auto path = directory.filePath("input.telem");
  writeFile(path, "a\nb\n");
  RecordedComms input(path.toStdString(), 10s);
  REQUIRE(input.open());
  uint8_t bytes[8]{};
  REQUIRE(input.read(bytes, sizeof(bytes)) == 2);
  REQUIRE(input.write(bytes, 1) == -1);
  auto read = std::async(std::launch::async,
                         [&] { return input.read(bytes, sizeof(bytes)); });
  input.close();
  REQUIRE(read.wait_for(1s) == std::future_status::ready);
  REQUIRE(read.get() == 0);
  REQUIRE(input.open());
  REQUIRE(input.read(bytes, sizeof(bytes)) == 2);
  input.close();
}
TEST_CASE("Recorded bytes reach the model in GUI-thread order and survive CSV "
          "export and replay loading",
          "[imu][gui]") {
  ensureApplication();
  QTemporaryDir directory;
  const auto inputPath = directory.filePath("input.telem");
  writeFile(inputPath, packet(100) + "\n" + packet(125) + "\n");
  FlightDataModel model;
  FlightLogManager log;
  LineTelemetryBatchMailbox mailbox;
  QEventLoop loop;
  std::vector<long> times;
  bool onGuiThread = true, done = false;
  QObject::connect(&model, &FlightDataModel::liveSamplesReceived, &model,
                   [&](const FlightSampleBatch &samples) {
                     onGuiThread &= QThread::currentThread() == model.thread();
                     for (const auto &sample : samples) {
                       times.push_back(sample.timestamp);
                       log.appendSample(sample);
                     }
                   });
  const auto drain = [&] {
    const auto result = mailbox.take(1);
    for (const auto &batch : result.batches) {
      FlightSampleBatch samples;
      for (const auto &sample : batch.samples)
        samples.append(sample);
      model.appendLiveBatch(samples);
    }
  };
  TelemetrySession session(
      std::make_shared<RecordedComms>(inputPath.toStdString(), 0ms),
      [&](LineTelemetryBatch batch) {
        if (mailbox.push(1, std::move(batch)))
          QMetaObject::invokeMethod(&model, drain, Qt::QueuedConnection);
      },
      [](const std::string &) {},
      [&] {
        QMetaObject::invokeMethod(
            &model,
            [&] {
              drain();
              done = true;
              loop.quit();
            },
            Qt::QueuedConnection);
      });
  QTimer::singleShot(3000, &loop, &QEventLoop::quit);
  REQUIRE(session.start());
  loop.exec();
  session.stop();
  REQUIRE(done);
  REQUIRE(onGuiThread);
  REQUIRE(times == std::vector<long>{0, 250});
  REQUIRE(std::isnan(model.latestSample().altitude));
  log.setOutputPath(directory.filePath("export.csv").toStdString());
  REQUIRE(log.exportSessionToCsv());
  FlightSession reloaded;
  REQUIRE_FALSE(
      SampleFileLoader::loadTheseusCsv(log.outputPath(), reloaded).has_value());
  REQUIRE(reloaded.samples.size() == 2);
  REQUIRE(reloaded.samples[1].timestamp == 250);
  REQUIRE(reloaded.samples[1].temperature == Catch::Approx(-0.06));
  REQUIRE(std::isnan(reloaded.samples[1].altitude));
  REQUIRE(std::isnan(reloaded.samples[1].coordinates.latitude));
  REQUIRE(std::isnan(reloaded.samples[1].angularVelocity.x));
}

TEST_CASE("A stalled parser bounds the frame queue and reports exactly the "
          "dropped frames",
          "[imu]") {
  std::string bytes;
  for (int i = 0; i < 101; ++i)
    bytes += packet(i) + "\n";
  auto input = std::make_shared<FragmentedComms>(bytes);
  input->timeouts = false;
  std::mutex mutex;
  std::condition_variable changed;
  bool entered = false, eof = false, done = false, timedOut = false;
  size_t samples = 0, dropped = 0;
  uint64_t receivedBytes = 0, droppedBytes = 0;
  input->atBoundary = [&] {
    std::unique_lock lock(mutex);
    if (!changed.wait_for(lock, 3s, [&] { return entered; }))
      timedOut = true;
  };
  input->atEnd = [&] {
    std::lock_guard lock(mutex);
    eof = true;
    changed.notify_all();
  };
  TelemetrySession session(
      input,
      [&](LineTelemetryBatch batch) {
        if (!batch.samples.empty()) {
          if (samples == 0) {
            std::unique_lock lock(mutex);
            entered = true;
            changed.notify_all();
            if (!changed.wait_for(lock, 3s, [&] { return eof; }))
              timedOut = true;
          }
          samples += batch.samples.size();
        }
        dropped += batch.droppedChunks;
        receivedBytes += batch.receivedBytes;
        droppedBytes += batch.droppedBytes;
      },
      [](const std::string &) {},
      [&] {
        std::lock_guard lock(mutex);
        done = true;
        changed.notify_all();
      },
      8);
  REQUIRE(session.start());
  {
    std::unique_lock lock(mutex);
    REQUIRE(changed.wait_for(lock, 5s, [&] { return done; }));
  }
  session.stop();
  REQUIRE_FALSE(timedOut);
  REQUIRE(samples == 9);
  REQUIRE(dropped == 92);
  REQUIRE(receivedBytes == bytes.size());
  REQUIRE(droppedBytes == 92 * fixture.size());
}

TEST_CASE("Input and delivery failures become diagnostics without escaping "
          "worker threads",
          "[imu]") {
  std::vector<std::string> messages;
  TelemetrySession missing(
      std::make_shared<RecordedComms>("/nonexistent-cosmo-recording.telem"), {},
      [&](const std::string &message) { messages.push_back(message); });
  REQUIRE_FALSE(missing.start());
  REQUIRE_FALSE(messages.empty());
  auto input = std::make_shared<FragmentedComms>(fixture);
  TelemetrySession throwing(
      input,
      [](LineTelemetryBatch) { throw std::runtime_error("delivery failure"); },
      [&](const std::string &message) { messages.push_back(message); });
  REQUIRE(throwing.start());
  REQUIRE(input->waitForPartialRead());
  throwing.stop();
  REQUIRE(messages.size() >= 3);
}
