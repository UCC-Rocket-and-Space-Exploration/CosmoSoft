#ifndef COSMO_SOFT_SERIALTIMEOUT_H
#define COSMO_SOFT_SERIALTIMEOUT_H
#include <exception>
#include <string>
#include <utility>

class SerialTimeout : public std::exception {
    std::string message;
public:
    explicit SerialTimeout(std::string  msg) :
    message(std::move(msg)) {}

    [[nodiscard]] const char* what() const noexcept {
        return message.c_str();
    }
};

#endif //COSMO_SOFT_SERIALTIMEOUT_H