#include "services/telemetry/frame_decoders/AltosFrameDecoder.h"

#include "shared/exceptions/IncorrectChecksum.h"
#include <stdexcept>

void AltosFrameDecoder::throw_if_checksum_not_valid(const Frame &frame) {
  if (!checksum_valid(frame))
    throw IncorrectChecksum();
}

FlightSample AltosFrameDecoder::decode_base(const Frame &frame) {
  throw_if_checksum_not_valid(frame);
  FlightSample sample{};
  sample.rssi = get_numerical_field_le<int8_t>(frame, 32, 1) / 2.0 - 74.0;
  sample.timestamp = get_numerical_field_le<uint16_t>(frame, 2, 2) * 10L;
  return sample;
}

bool AltosFrameDecoder::checksum_valid(const Frame &frame) {
  const auto start = frame.packet_start_index;
  const std::string text(frame.data.begin(), frame.data.end());
  if (frame.format != AltosFrame || (start != 7 && start != 8) ||
      text.size() != start + 70 ||
      text.substr(0, start) != (start == 8 ? "TELEM 22" : "TELEM22")) {
    throw std::invalid_argument("Invalid AltOS packet length or prefix");
  }
  uint8_t sum = 0x5a;
  for (size_t offset = 0; offset < 34; ++offset) {
    sum += ByteHelper::get_byte_from_str(text.substr(start + offset * 2, 2));
  }
  const auto expected =
      ByteHelper::get_byte_from_str(text.substr(start + 68, 2));
  const auto status = ByteHelper::get_byte_from_str(text.substr(start + 66, 2));
  return sum == expected && (status & 0x80) != 0;
}

void AltosFrameDecoder::get_field_bytes(const Frame &frame, uint8_t *bytes,
                                        size_t offset, size_t count) {
  if (offset > 34 || count > 34 - offset ||
      frame.packet_start_index > frame.data.size() ||
      (offset + count) * 2 > frame.data.size() - frame.packet_start_index) {
    throw std::invalid_argument("AltOS field outside packet");
  }
  for (size_t i = 0; i < count; ++i) {
    const auto position = frame.packet_start_index + (offset + i) * 2;
    bytes[i] = ByteHelper::get_byte_from_str(std::string(
        frame.data.begin() + position, frame.data.begin() + position + 2));
  }
}
