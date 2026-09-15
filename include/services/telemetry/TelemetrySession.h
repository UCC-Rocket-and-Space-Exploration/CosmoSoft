#ifndef COSMO_SOFT_TELEMETRYSESSION_H
#define COSMO_SOFT_TELEMETRYSESSION_H
#include "gateway/comms/interfaces/IComms.h"
#include "services/telemetry/LineTelemetryDecodeWorker.h"
#include <functional>
#include <memory>

namespace cosmo::telemetry {
/**
 * @brief Owns input, bounded frame queue, framer and parser for one AltOS
 * session. Lifecycle calls must be serialized by the owner. Callbacks execute
 * on workers; they must not destroy/stop the session and GUI owners must queue
 * delivery. stop() drains accepted frames and guarantees no callbacks after
 * returning.
 */
class TelemetrySession {
public:
  using BatchCallback = std::function<void(LineTelemetryBatch)>;
  using DiagnosticCallback = std::function<void(const std::string &)>;
  /** @brief Construct a stopped session with bounded queue and delivery
   * callbacks. */
  TelemetrySession(std::shared_ptr<IComms> comms, BatchCallback batch,
                   DiagnosticCallback diagnostic,
                   std::function<void()> completed = {},
                   size_t frameCapacity = 128);
  /** @brief Cancel reads, drain parsing, and join both workers. */
  ~TelemetrySession();
  /** @brief Start or restart from a fresh input/timing state; report failures.
   */
  bool start();
  /** @brief Stop input, join framing, drain accepted frames, and join parsing.
   */
  void stop();
  /** @brief Whether the session is receiving or draining frames. */
  bool running() const;

private:
  struct State;
  std::unique_ptr<State> m_state;
};
} // namespace cosmo::telemetry
#endif
