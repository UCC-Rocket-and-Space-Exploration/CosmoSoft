#include "gateway/comms/windows/SerialCommsWindows.h"

#include <cstring>
#include <format>
#include <iostream>
#include <string>

#include "common/SerialPortNotOpened.h"

SerialCommsWindows::SerialCommsWindows(std::string device, int baud){
    m_device = std::move(device);
    m_baud = baud;
    SerialCommsWindows::open();
    m_timeouts = COMMTIMEOUTS(50, 10, 5000);
}

SerialCommsWindows::~SerialCommsWindows() {
    SerialCommsWindows::close();
}

bool SerialCommsWindows::open() {
    if (this->m_handle != INVALID_HANDLE_VALUE && this->m_handle != nullptr) {
        return true;
    }
    this->m_handle = CreateFile(m_device.c_str(), GENERIC_READ | GENERIC_WRITE,
                                0, NULL, OPEN_EXISTING,0,NULL);
    if (this->m_handle == INVALID_HANDLE_VALUE) {
        throw SerialPortNotOpened("Serial port " + this->m_device + " can't be opened.");
    }
    bool timeout_not_experienced = SetCommTimeouts(this->m_handle, &m_timeouts);

    if (not timeout_not_experienced) {
        std::cout << "Unsuccessful timeout set" << std::endl;
        std::cout << GetLastError();
    }
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

    WriteFile(this->m_handle, data, size, &bytesWritten, NULL);
    // FlushFileBuffers(this->m_handle);
    std::cout << "Written: " << bytesWritten << std::endl;
    return bytesWritten;
}

ssize_t SerialCommsWindows::read(uint8_t* buffer, size_t maxSize) {
    unsigned long read_bytes_number;
    WINBOOL read_status = ReadFile(this->m_handle, buffer, maxSize, &read_bytes_number, NULL);
    return read_bytes_number;
}

[[nodiscard]] std::string SerialCommsWindows::getDeviceName() const {
    return this->m_device;
}
