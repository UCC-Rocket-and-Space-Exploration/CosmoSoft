#ifndef COSMO_SOFT_SERIALCOMMSPOSIX_H
#define COSMO_SOFT_SERIALCOMMSPOSIX_H
#include <string>

#include "IComms.h"


class SerialCommsPosix : public IComms {
public:
    explicit SerialCommsPosix(const std::string& device, int baud = 115200);
    ~SerialCommsPosix() override; //destructor

    bool open() override;
    void close() override;
    bool isOpen() const override;

    ssize_t write(const uint8_t* data, size_t size) override;
    ssize_t read(uint8_t* buffer, size_t maxSize) override;

    void flush() override;

    std::string deviceName() const override;
    std::string devicePort() const override;


private:
    std::string m_device;
    int m_baud;
    int m_fd = -1;
};

#endif //COSMO_SOFT_SERIALCOMMSPOSIX_H