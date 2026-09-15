#ifndef COSMO_SOFT_SERIALFRAMERWORKER_H
#define COSMO_SOFT_SERIALFRAMERWORKER_H
#include "services/interfaces/IBuffer.h"
#include "services/interfaces/IFramer.h"
#include "services/interfaces/ILogger.h"
#include "shared/abstraction/Worker.h"
#include <functional>
#include <memory>
#include <thread>

/** @brief Frames input into a queue; completion runs after input has stopped.
 */
class SerialFramerWorker final : public Worker{
public:
  /** @brief Bind workers, optional loggers, and an optional completion
   * callback. */
  explicit SerialFramerWorker(const std::shared_ptr<IFramer> &framer,
                              const std::shared_ptr<IBuffer<Frame>> &buffer,
                              const std::shared_ptr<ILogger> &results_logger,
                              const std::shared_ptr<ILogger> &debug_logger,
                              std::function<void()> completed = {})
      : m_completed(std::move(completed)), m_framer(framer), m_buffer(buffer),
        m_results_logger(results_logger), m_debug_logger(debug_logger) {}
  /** @brief Interrupt and join before releasing dependencies. */
  ~SerialFramerWorker() override { SerialFramerWorker::stop(); }

  /** @brief Interrupt pending reads, then join the framing worker. */
  void stop() override {
    m_running = false;
    m_framer->interrupt();
    Worker::stop();
  }

protected:
    void m_process() override;

private:
  std::function<void()> m_completed;
  std::shared_ptr<IFramer> m_framer;
  std::shared_ptr<IBuffer<Frame>> m_buffer;
  std::shared_ptr<ILogger> m_results_logger;
  std::shared_ptr<ILogger> m_debug_logger;
};

#endif //COSMO_SOFT_SERIALFRAMERWORKER_H