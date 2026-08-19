#ifndef COSMO_SOFT_SERIALCOMMSPOSIX_H
#define COSMO_SOFT_SERIALCOMMSPOSIX_H

#if defined(__unix__) || defined(__APPLE__) || defined(_POSIX_VERSION)

#include <cstdint>
#include <mutex>
#include <string>

#include "../IComms.h"

/** @brief POSIX non-blocking serial connection with bounded read cancellation. */
class SerialCommsPosix : public IComms {
public:
    /** @brief Construct a serial connection for @p device and @p baud. */
    explicit SerialCommsPosix(const std::string& device, int baud = 115200);

    /** @brief Close the descriptor before releasing connection state. */
    ~SerialCommsPosix() override;

    /** @copydoc IComms::open */
    bool open() override;

    /** @copydoc IComms::close */
    void close() override;

    /** @copydoc IComms::isOpen */
    [[nodiscard]] bool isOpen() const override;

    /** @copydoc IComms::write */
    ssize_t write(const uint8_t* data, size_t size) override;

    /** @copydoc IComms::read */
    ssize_t read(uint8_t* buffer, size_t maxSize) override;

    /** @copydoc IComms::getDeviceName */
    [[nodiscard]] std::string getDeviceName() const override;

private:
    std::string m_device;
    int m_baud;
    mutable std::mutex m_handleMutex;
    int m_fd = -1;
    std::uint64_t m_generation = 0;
};

#endif // POSIX platform
#endif // COSMO_SOFT_SERIALCOMMSPOSIX_H
