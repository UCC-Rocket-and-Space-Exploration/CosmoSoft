#include "gateway/comms/windows/SerialCommsWindows.h"

#include <cstring>


SerialCommsWindows::SerialCommsWindows(const std::string& device, int baud) :
    m_device(std::move(device)){
    m_baud = baud;
    m_handle = INVALID_HANDLE_VALUE;
}

bool SerialCommsWindows::open() {
    this->m_handle = CreateFile(m_device.c_str(), GENERIC_READ | GENERIC_WRITE,
                               0, NULL, OPEN_EXISTING,0,NULL);

    // TODO: is it better to throw exception when device is tried to be opened?
    if (this->m_handle == INVALID_HANDLE_VALUE) {
        // throw "Invalid handle value";
        // cout << "INVALID_HANDLE_VALUE for" << m_device << endl;
        return false;
    }
    return true;
}

void SerialCommsWindows::close() {
    if (m_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_handle);
        m_handle = INVALID_HANDLE_VALUE;
    }
}
bool SerialCommsWindows::isOpen() const {
    return this->m_handle != INVALID_HANDLE_VALUE && this->m_handle != nullptr;
}

ssize_t SerialCommsWindows::write(const uint8_t* data, size_t size) {
    // LPCVOID buffer = (LPCVOID)data;
    WriteFile(this->m_handle, data, size, 0, NULL);
    return size;
}

ssize_t SerialCommsWindows::read(uint8_t* buffer, size_t maxSize) {
    //TODO: disscuss mb there is no need to return number of data read. Instead read_status can be returned
    WINBOOL read_status = ReadFile(this->m_handle, buffer, maxSize, 0, NULL);
    return std::strlen((char*)buffer);
}

SerialCommsWindows::~SerialCommsWindows() {
    close();
}

[[nodiscard]] std::string SerialCommsWindows::getDeviceName() const {
    return this->m_device;
}
