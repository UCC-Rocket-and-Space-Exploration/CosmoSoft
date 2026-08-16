#ifndef COSMO_SOFT_INCORRECTCHECKSUM_H
#define COSMO_SOFT_INCORRECTCHECKSUM_H
#include <cstdint>
#include <exception>
#include <format>
#include <string>

class IncorrectChecksum : public std::exception {
    std::string m_err = "Checksums do not match. ";
public:

    [[nodiscard]] const char* what() const noexcept {
        return m_err.c_str();
    }
};

#endif //COSMO_SOFT_INCORRECTCHECKSUM_H