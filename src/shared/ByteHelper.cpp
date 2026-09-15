
#include "shared/ByteHelper.h"
#include <charconv>
#include <stdexcept>

int ByteHelper::from_bytes_to_int32_be(const uint8_t *bytes, int bytes_count) {
  uint32_t result = 0;
  for (int i = 0; i < bytes_count; i++) {
    result |= (static_cast<uint32_t>(bytes[i]) << 8 * (bytes_count - 1 - i));
  }
  return static_cast<int32_t>(result);
}
int ByteHelper::from_bytes_to_int32_le(const uint8_t *bytes,
                                       size_t bytes_count) {
  uint32_t result = 0;
  for (size_t i = 0; i < bytes_count; i++) {
    result |= (static_cast<uint32_t>(bytes[i]) << 8 * i);
  }
  return static_cast<int32_t>(result);
}

uint8_t ByteHelper::get_byte_from_str(const std::string &b_str) {
  unsigned value = 0;
  const auto result =
      std::from_chars(b_str.data(), b_str.data() + b_str.size(), value, 16);
  if (b_str.size() != 2 || result.ec != std::errc{} ||
      result.ptr != b_str.data() + b_str.size() || value > 255) {
    throw std::invalid_argument("Invalid hexadecimal byte");
  }
  return static_cast<uint8_t>(value);
}
