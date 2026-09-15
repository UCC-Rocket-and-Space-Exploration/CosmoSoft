#include "services/telemetry/TelemetrySession.h"
#include "gateway/comms/SerialFramerWorker.h"
#include "services/BlockingQueue.h"
#include "services/telemetry/ParserWorker.h"
#include "services/telemetry/framers/AltosFramer.h"
#include <algorithm>
#include <map>
#include <mutex>

namespace cosmo::telemetry {
namespace {
class FrameQueue final : public IBuffer<Frame> {
public:
  explicit FrameQueue(size_t capacity) : queue(std::max(size_t{1}, capacity)) {}
  void put(Frame frame) override {
    inputBytes += frame.data.size();
    queue.push(std::move(frame));
  }
  std::optional<Frame> get() override {
    Frame frame{};
    if (queue.pop_for(frame, std::chrono::milliseconds(10))) {
      outputBytes += frame.data.size();
      return frame;
    }
    return std::nullopt;
  }
  BlockingQueue<Frame> queue;
  std::atomic_uint64_t inputBytes{0};
  std::atomic_uint64_t outputBytes{0};
};
class Diagnostics final : public ILogger {
public:
  void Log(std::string message) override { LogLine(std::move(message)); }
  void Log(const unsigned char *) override {
    LogLine("Legacy binary log suppressed");
  }
  void LogLine(const unsigned char *) override {
    LogLine("Legacy binary log suppressed");
  }
  void LogLine(std::string message) override {
    std::lock_guard lock(mutex);
    if (counts.size() >= 8 && !counts.contains(message))
      message = "Other rejected packets";
    ++counts[message];
  }
  std::mutex mutex;
  std::map<std::string, size_t> counts;
};
} // namespace
struct TelemetrySession::State {
  std::shared_ptr<IComms> comms;
  BatchCallback batch;
  DiagnosticCallback diagnostic;
  std::function<void()> completed;
  size_t capacity = 128;
  uint64_t reportedBytes = 0;
  std::atomic_bool active{false};
  std::atomic_bool stopping{false};
  std::shared_ptr<FrameQueue> queue;
  std::shared_ptr<Diagnostics> diagnostics;
  std::shared_ptr<AltosFramer> framer;
  std::unique_ptr<ParserWorker> parser;
  std::unique_ptr<SerialFramerWorker> reader;

  void report(const std::string &message) {
    try {
      if (diagnostic)
        diagnostic(message);
    } catch (const std::exception
                 &) { /* An owner callback must not kill a worker. */
    }
  }
  void finish() {
    parser->stop();
    LineTelemetryBatch stats;
    stats.receivedBytes = framer->received_bytes() - reportedBytes;
    stats.droppedChunks = queue->queue.dropped();
    stats.droppedBytes = queue->inputBytes - queue->outputBytes;
    try {
      if (batch)
        batch(std::move(stats));
    } catch (const std::exception &error) {
      report(error.what());
    }
    for (const auto &[message, count] : diagnostics->counts)
      report("Skipped " + std::to_string(count) + ": " + message);
    report(stopping ? "Recorded IMU input stopped."
                    : "Recorded IMU input complete/disconnected.");
    active = false;
    try {
      if (completed)
        completed();
    } catch (const std::exception &error) {
      report(error.what());
    }
  }
};
TelemetrySession::TelemetrySession(std::shared_ptr<IComms> comms,
                                   BatchCallback batch,
                                   DiagnosticCallback diagnostic,
                                   std::function<void()> completed,
                                   size_t capacity)
    : m_state(std::make_unique<State>()) {
  m_state->comms = std::move(comms);
  m_state->batch = std::move(batch);
  m_state->diagnostic = std::move(diagnostic);
  m_state->completed = std::move(completed);
  m_state->capacity = capacity;
}
TelemetrySession::~TelemetrySession() { stop(); }
bool TelemetrySession::running() const { return m_state->active.load(); }
bool TelemetrySession::start() {
  if (running())
    return true;
  stop();
  auto &s = *m_state;
  s.stopping = false;
  s.reportedBytes = 0;
  try {
    if (!s.comms || !s.comms->open()) {
      s.report("Failed to open recorded IMU input.");
      return false;
    }
    s.queue = std::make_shared<FrameQueue>(s.capacity);
    s.diagnostics = std::make_shared<Diagnostics>();
    s.framer = std::make_shared<AltosFramer>(s.comms);
    s.parser = std::make_unique<ParserWorker>(
        s.queue, ParserWorker::SampleCallback([&s](const FlightSample &sample) {
          LineTelemetryBatch result;
          result.samples.push_back(sample);
          const auto received = s.framer->received_bytes();
          result.receivedBytes = received - s.reportedBytes;
          s.reportedBytes = received;
          if (s.batch)
            s.batch(std::move(result));
        }),
        s.diagnostics);
    s.reader = std::make_unique<SerialFramerWorker>(
        s.framer, s.queue, nullptr, s.diagnostics, [&s] { s.finish(); });
    s.active = true;
    s.parser->start();
    s.reader->start();
    return true;
  } catch (const std::exception &error) {
    s.report(std::string("Failed to start IMU input: ") + error.what());
    stop();
    return false;
  }
}
void TelemetrySession::stop() {
  auto &s = *m_state;
  s.stopping = true;
  if (s.reader) {
    s.reader->stop();
    s.reader.reset();
  }
  if (s.comms)
    s.comms->close();
  if (s.parser) {
    s.parser->stop();
    s.parser.reset();
  }
  s.active = false;
}
} // namespace cosmo::telemetry
