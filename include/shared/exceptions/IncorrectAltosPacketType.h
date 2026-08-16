//
// Created by Lenovo on 6/30/2026.
//

#ifndef COSMO_SOFT_INCORRECTALTOSPACKETTYPE_H
#define COSMO_SOFT_INCORRECTALTOSPACKETTYPE_H
#include <cstdint>
#include <exception>
#include <string>

class IncorrectAltosPacketType : public std::exception {
    std::string err = "Incorrect Altos Packet type received: ";
public:
    explicit IncorrectAltosPacketType(uint8_t packet_type_value){
        err.append(std::to_string(packet_type_value));
    }

    [[nodiscard]] const char* what() const noexcept {
        return err.c_str();
    }
};

#endif //COSMO_SOFT_INCORRECTALTOSPACKETTYPE_H