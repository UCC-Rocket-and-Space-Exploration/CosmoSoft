#ifndef COSMO_SOFT_SERIALCOMMSWINDOWS_H
#define COSMO_SOFT_SERIALCOMMSWINDOWS_H

#include <memory>
#include <mutex>
#include <string>

#include "gateway/comms/IComms.h"

/** @brief Windows serial connection using cancellable overlapped I/O. */
class SerialCommsWindows : public IComms {
public:
    /** @brief Construct a serial connection for @p device and @p baud. */
    explicit SerialCommsWindows(const std::string& device, int baud = 115200);

    /** @brief Preserve the historical non-const device overload. */
    SerialCommsWindows(std::string& device, int baud);

    /** @brief Close the handle before releasing connection state. */
    ~SerialCommsWindows() override;

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
    struct HandleState;

    mutable std::mutex m_handleMutex;
    std::shared_ptr<HandleState> m_handle;
    std::string m_device;
    int m_baud;
};

#endif // COSMO_SOFT_SERIALCOMMSWINDOWS_H
