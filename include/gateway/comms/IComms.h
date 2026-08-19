#ifndef COSMO_SOFT_ICOMMS_H
#define COSMO_SOFT_ICOMMS_H

#include <cstddef>
#include <cstdint>
#include <string>
#if defined(_MSC_VER)
#include <BaseTsd.h>
using ssize_t = SSIZE_T;
#else
#include <sys/types.h>
#endif

/**
 * @brief Platform-agnostic byte-stream communications interface.
 *
 * Implementations passed to SerialWorker must allow close() to run concurrently
 * with a pending read(). Closing that connection must make the read return
 * promptly because close() is the worker's platform-neutral cancellation path.
 */
class IComms {
public:
    /** @brief Destroy the communications implementation. */
    virtual ~IComms() = default;

    /** @brief Open the connection. */
    virtual bool open() = 0;

    /** @brief Close the connection and promptly cancel any pending read. */
    virtual void close() = 0;

    /** @brief Return whether the connection is currently open. */
    [[nodiscard]] virtual bool isOpen() const = 0;

    /**
     * @brief Write bytes to the connection.
     * @return The number of bytes written, or -1 on error.
     */
    virtual ssize_t write(const uint8_t* data, size_t size) = 0;

    /**
     * @brief Read up to @p maxSize bytes without waiting indefinitely.
     * @return The number of bytes read, 0 when no data is available, or -1 on error.
     */
    virtual ssize_t read(uint8_t *buffer, size_t maxSize) = 0;

    /** @brief Return a human-readable device name. */
    [[nodiscard]] virtual std::string getDeviceName() const = 0;
};

#endif // COSMO_SOFT_ICOMMS_H
