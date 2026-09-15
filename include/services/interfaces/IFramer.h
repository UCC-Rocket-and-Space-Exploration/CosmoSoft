#ifndef COSMO_SOFT_IFRAMER_H
#define COSMO_SOFT_IFRAMER_H
#include "domain/Frame.h"
#include <atomic>

class IFramer {
    public:
    virtual ~IFramer() = default;
    virtual Frame get_frame(const bool& running) = 0;
    /** @brief Read with cancellation observed throughout the operation. */
    virtual Frame get_frame(const std::atomic_bool &running) {
      const bool active = running.load();
      return get_frame(active);
    }
    /** @brief Interrupt pending input when supported by the source. */
    virtual void interrupt() {}
    /** @brief Whether the input has ended or disconnected. */
    virtual bool finished() const { return false; }
};
#endif //COSMO_SOFT_IFRAMER_H