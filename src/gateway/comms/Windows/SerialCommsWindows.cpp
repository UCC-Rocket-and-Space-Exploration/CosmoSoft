#include "gateway/comms/windows/SerialCommsWindows.h"

#include <cstring>
#include <format>
#include <iostream>
#include <string>

#include "../../../../include/shared/exceptions/SerialPortNotOpened.h"
#include "../../../../include/shared/exceptions/SerialTimeout.h"
#include "../../../../include/shared/exceptions/UnhandledSerialException.h"

SerialCommsWindows::SerialCommsWindows(std::string device, int baud){
    m_device = std::move(device);
    m_baud = baud;
    SerialCommsWindows::open();
    // m_timeouts = COMMTIMEOUTS(50, 10, 5000);
    // this->m_handle = nullptr;
}

SerialCommsWindows::~SerialCommsWindows() {
    SerialCommsWindows::close();
}

bool SerialCommsWindows::open() {
    if (this->m_handle != INVALID_HANDLE_VALUE && this->m_handle != nullptr) {
        std::cout << "port " << m_device << "was already opened" << std::endl;
        return true;
    }
    this->m_handle = CreateFile(m_device.c_str(), GENERIC_READ | GENERIC_WRITE,
                                0, nullptr, OPEN_EXISTING,0,NULL);
    if (this->m_handle == INVALID_HANDLE_VALUE) {
        throw SerialPortNotOpened("Serial port " + this->m_device + " can't be opened.");
    }
    COMMTIMEOUTS timeouts = {0};

    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 5000;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    bool timeout_not_experienced = SetCommTimeouts(this->m_handle, &timeouts);

    if (not timeout_not_experienced) {
        std::cout << "Unsuccessful timeout set" << std::endl;
        std::cout << GetLastError();
        return false;
    }
    std::cout << "Opened for " << m_device << std::endl;
    return true;
}
void SerialCommsWindows::close() {
    if (m_handle != INVALID_HANDLE_VALUE) {
        std::cout << "Serial communication closed for port: " << SerialCommsWindows::getDeviceName() << std::endl;
        CloseHandle(m_handle);
        m_handle = INVALID_HANDLE_VALUE;
    }
}

bool SerialCommsWindows::isOpen() const {
    return this->m_handle != INVALID_HANDLE_VALUE && this->m_handle != nullptr;
}

ssize_t SerialCommsWindows::write(const uint8_t* data, size_t size) {
    DWORD bytesWritten = 0;
    bool val_handle = m_handle != nullptr && m_handle != INVALID_HANDLE_VALUE;
    std::cout << "val handle: " << val_handle << std::endl;
    bool written = WriteFile(this->m_handle, data, size, &bytesWritten, nullptr);
    if (!written) {
        std::cout << "Write error: " << GetLastError() << std::endl;
    }
    std::cout << "Written: " << bytesWritten << std::endl;
    return bytesWritten;
}

ssize_t SerialCommsWindows::read(uint8_t* buffer, size_t maxSize) {
    unsigned long read_bytes_number;
    WINBOOL read_status = ReadFile(this->m_handle, buffer, maxSize, &read_bytes_number, nullptr);

    if (read_bytes_number == 0) {
        throw SerialTimeout("Timeout reached when reading from the port " + this->m_device + ".");
    }

    if (read_status == 0) {
        throw UnhandledSerialException("Unknown excpetion occured while reading from the device: " +
            this->m_device + "." + "Full message: " + std::to_string(GetLastError()));
    }
    return read_bytes_number;
}

[[nodiscard]] std::string SerialCommsWindows::getDeviceName() const {
    return this->m_device;
}
