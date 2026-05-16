#ifndef COSMO_SOFT_SERIALCOMMSPOSIX_H
#if defined(__unix__) || defined(__APPLE__) || defined(_POSIX_VERSION)
#define COSMO_SOFT_SERIALCOMMSPOSIX_H
#include <string>

#include "../IComms.h"


class SerialCommsPosix : public IComms {
public:
    explicit SerialCommsPosix(const std::string& device, int baud = 115200);
    ~SerialCommsPosix() override; //destructor

    bool open() override;
    // bool open(std::string* flags);
    void close() override;
    [[nodiscard]] bool isOpen() const override;

    ssize_t write(const uint8_t* data, size_t size) override;
    ssize_t read(uint8_t* buffer, size_t maxSize) override;

    [[nodiscard]] std::string getDeviceName() const override;

private:
    std::string m_device;
    std::string m_port;
    int m_baud;
    int m_fd = -1;
};

#endif //POSIX CHECK
#endif //COSMO_SOFT_SERIALCOMMSPOSIX_H

//storage on flight comp filled up
//Telemetry loss on launch
//Avionix bay missized

