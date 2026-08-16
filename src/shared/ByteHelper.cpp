
#include "shared/ByteHelper.h"

int ByteHelper::from_bytes_to_int32_be(const uint8_t *bytes, int bytes_count) {
    int result = 0;
    for (int i = 0; i < bytes_count; i++) {
        result |= (bytes[i] << 8*(bytes_count - 1 - i));
    }
    return result;
}
int ByteHelper::from_bytes_to_int32_le(const uint8_t *bytes, int bytes_count) {
    int result = 0;
    for (int i = 0; i < bytes_count; i++) {
        result |= (bytes[i] << 8*i);
    }
    return result;
}

uint8_t ByteHelper::get_byte_from_str(const std::string &b_str) {
    return std::stoi(b_str, nullptr, 16);
}

