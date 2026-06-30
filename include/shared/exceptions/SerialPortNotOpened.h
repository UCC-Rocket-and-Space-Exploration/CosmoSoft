#ifndef COSMO_SOFT_SERIALPORTNOTOPENED_H
#define COSMO_SOFT_SERIALPORTNOTOPENED_H
#include <exception>
#include <string>

class SerialPortNotOpened : public std::exception {
    std::string message;
public:
    explicit SerialPortNotOpened(const std::string& msg) :
    message(msg) {}

    const char* what() const noexcept {
        return message.c_str();
    }
};
#endif //COSMO_SOFT_SERIALPORTNOTOPENED_H