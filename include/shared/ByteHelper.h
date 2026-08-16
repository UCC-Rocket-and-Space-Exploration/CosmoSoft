#ifndef COSMO_SOFT_BYTEHELPER_H
#define COSMO_SOFT_BYTEHELPER_H
#include <cstdint>
#include <string>

class ByteHelper {
public:
    static uint8_t get_byte_from_str(const std::string &b_str);
    static int from_bytes_to_int32_be(const uint8_t *bytes, int bytes_count);
    static int from_bytes_to_int32_le(const uint8_t *bytes, int bytes_count);
};

#endif //COSMO_SOFT_BYTEHELPER_H