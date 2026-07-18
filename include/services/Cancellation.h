#ifndef COSMO_SOFT_CANCELLATION_H
#define COSMO_SOFT_CANCELLATION_H

#include <cstddef>
#include <functional>
#include <utility>

namespace cosmo {

/**
 * @brief Callback queried by long-running service operations for cooperative cancellation.
 */
using CancellationCheck = std::function<bool()>;

namespace detail {

/**
 * @brief Latches cooperative cancellation and amortizes checks in tight loops.
 */
class CancellationState final {
public:
    /**
     * @brief Create cancellation state backed by @p cancellation_check.
     * @param cancellation_check Callback that returns true after cancellation is requested.
     */
    explicit CancellationState(const CancellationCheck &cancellation_check) noexcept
        : m_cancellation_check(cancellation_check) {}

    /**
     * @brief Query the callback immediately and latch a positive result.
     * @return true once cancellation has been requested.
     */
    [[nodiscard]] bool poll()
    {
        m_operations_until_poll = POLL_INTERVAL;
        if (!m_canceled && m_cancellation_check && m_cancellation_check()) {
            m_canceled = true;
        }
        return m_canceled;
    }

    /**
     * @brief Query cancellation periodically while traversing a tight loop.
     * @return true once cancellation has been requested.
     */
    [[nodiscard]] bool poll_periodically()
    {
        if (m_canceled) {
            return true;
        }
        if (m_operations_until_poll > 1U) {
            --m_operations_until_poll;
            return false;
        }
        return poll();
    }

    /**
     * @brief Return the latched cancellation state without invoking the callback.
     * @return true after a cancellation check has succeeded.
     */
    [[nodiscard]] bool is_canceled() const noexcept { return m_canceled; }

private:
    static constexpr std::size_t POLL_INTERVAL = 256U;

    const CancellationCheck &m_cancellation_check;
    std::size_t m_operations_until_poll = 1U;
    bool m_canceled = false;
};

} // namespace detail
} // namespace cosmo

#endif // COSMO_SOFT_CANCELLATION_H
