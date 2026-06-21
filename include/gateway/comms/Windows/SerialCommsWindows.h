#ifndef COSMO_SOFT_SERIALCOMMSWINDOWS_H
#define COSMO_SOFT_SERIALCOMMSWINDOWS_H
#include <iostream>
#include <utility>
#include <windows.h>

#include "../interfaces/IComms.h"


class SerialCommsWindows : public IComms {
public:
    explicit SerialCommsWindows(std::string device, int baud = 115200);
    ~SerialCommsWindows() override;

    bool open() override;
    void close() override;
    bool isOpen() const override;
    //Blocking write function
    // uint8_t* data: pointer to the start of a block of unsigned 8-bit integers (serial port streams operate on a per-byte basis)
    // size_t size: how many bytes to write starting from the pointer as an unsigned long
    // returns a signed long of the amount of data written
    ssize_t write(const uint8_t* data, size_t size) override;
    ssize_t read(uint8_t* buffer, size_t maxSize) override;
    [[nodiscard]] std::string getDeviceName() const override;

private:
    COMMTIMEOUTS m_timeouts{};
    // const DWORD read_timeout = 5000;
    std::string m_device;
    HANDLE m_handle;
    int m_baud;
};


#endif //COSMO_SOFT_SERIALCOMMSWINDOWS_H