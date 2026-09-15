#ifndef COSMO_SOFT_PARSERWORKER_H
#define COSMO_SOFT_PARSERWORKER_H
#include "services/interfaces/IBuffer.h"
#include "services/interfaces/ILogger.h"
#include "services/telemetry/FrameDecoderVault.h"
#include "shared/abstraction/Worker.h"
#include <functional>
#include <memory>

/** @brief Background packet decoder with per-session timing and instance
 * callbacks. */
class ParserWorker : public Worker {
public:
  using SampleCallback = std::function<void(const FlightSample &)>;
  /** @brief Construct with the original function-pointer callback contract. */
  ParserWorker(const std::shared_ptr<IBuffer<Frame>> &buffer,
               void (*callback)(const FlightSample &),
               const std::shared_ptr<ILogger> &logger)
      : ParserWorker(buffer, SampleCallback(callback), logger) {}
  /** @brief Construct with an instance-bound callback; callbacks run on the
   * worker. */
  ParserWorker(std::shared_ptr<IBuffer<Frame>> buffer, SampleCallback callback,
               std::shared_ptr<ILogger> logger)
      : m_callback(std::move(callback)), m_buffer(std::move(buffer)),
        m_logger(std::move(logger)) {}
  /** @brief Drain accepted frames and join before members are destroyed. */
  ~ParserWorker() override { stop(); }

protected:
    void m_process() override;
private:
  FrameDecoderVault m_vault;
  SampleCallback m_callback;
  std::shared_ptr<IBuffer<Frame>> m_buffer;
  std::shared_ptr<ILogger> m_logger;
};
#endif
