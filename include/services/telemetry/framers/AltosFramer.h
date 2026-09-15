#ifndef COSMO_SOFT_ALTOSFRAMER_H
#define COSMO_SOFT_ALTOSFRAMER_H
#include "gateway/comms/interfaces/IComms.h"
#include "services/interfaces/IFramer.h"
#include <functional>
#include <memory>
#include <string>

/** @brief Bounded streaming AltOS framer, retaining fragments across timeouts.
 */
class AltosFramer final : public IFramer {
public:
  /** @brief Bind an already-open, interruptible input. */
  explicit AltosFramer(std::shared_ptr<IComms> comms)
      : m_comms(std::move(comms)) {}
  /** @brief Read a packet; retained for synchronous callers. */
  Frame get_frame(const bool &running) override;
  /** @brief Read while observing the worker's cancellation flag. */
  Frame get_frame(const std::atomic_bool &running) override;
  /** @brief Cancel pending reads. */
  void interrupt() override { m_comms->close(); }
  /** @brief Return whether the source has closed. */
  bool finished() const override { return !m_comms->isOpen(); }
  /** @brief Test the initial signature character. */
  static bool sign_start(uint8_t byte) { return byte == 'T'; }
  /** @brief Total bytes read, including rejected input; safe to observe while
   * receiving. */
  size_t received_bytes() const {
    return m_received.load(std::memory_order_relaxed);
  }

private:
  Frame read_frame(const std::function<bool()> &running);
  std::shared_ptr<IComms> m_comms;
  std::string m_pending;
  std::atomic_size_t m_received{0};
};
#endif
