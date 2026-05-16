#ifndef COSMO_SOFT_SERIALCOMMSWINDOWS_H
#define COSMO_SOFT_SERIALCOMMSWINDOWS_H
#include <windows.h>

#include "gateway/comms/IComms.h"


class SerialCommsWindows : public IComms {
public:
    explicit SerialCommsWindows(const std::string& device, int baud = 115200);
    ~SerialCommsWindows() override; //destructor

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
    SerialCommsWindows(std::string& device, int baud);
private:
    HANDLE m_handle;
    const std::string& m_device;
    int m_baud;
};


#endif //COSMO_SOFT_SERIALCOMMSWINDOWS_H