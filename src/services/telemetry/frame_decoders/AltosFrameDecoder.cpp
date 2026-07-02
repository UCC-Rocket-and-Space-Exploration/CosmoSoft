#include "services/telemetry/frame_decoders/AltosFrameDecoder.h"
#include <cstdint>
#include <vector>
#include "shared/ByteHelper.h"
#include "shared/exceptions/IncorrectChecksum.h"


void AltosFrameDecoder::throw_if_checksum_not_valid(const Frame& frame) {
    if (!checksum_valid(frame)) {
        throw IncorrectChecksum();
    }
}
bool AltosFrameDecoder::checksum_valid(const Frame& frame) {
    constexpr size_t packet_byte_count = 35;
    char pts[] = {
        (char)frame.data[frame.packet_start_index + packet_byte_count*2 - 2],
        (char)frame.data[frame.packet_start_index + packet_byte_count*2 - 1]
    };

    std::string checksum_byte_str(pts, 2);
    int expected_checksum = ByteHelper::get_byte_from_str(checksum_byte_str);
    uint8_t actual_checksum = 0x5a;

    for (size_t i = frame.packet_start_index; i < packet_byte_count - 1; i++) {
        char byte_pts[] = {
            static_cast<char>(frame.data[i*2]),
            static_cast<char>(frame.data[i*2 + 1])
        };
        std::string byte_str(byte_pts, 2);

        actual_checksum += ByteHelper::get_byte_from_str(byte_str);
    }
    return expected_checksum == actual_checksum;
}

void AltosFrameDecoder::get_field_bytes(const Frame& frame, uint8_t* bytes_buf, size_t packet_field_start_offset, size_t read_count) {
    for (int i = 0; i < read_count; i++) {
        size_t byte_pos = frame.packet_start_index + packet_field_start_offset*2 + i*2;
        char byte_part[2] = {
            static_cast<char>(frame.data[byte_pos]),
            static_cast<char>(frame.data[byte_pos + 1])};
        std::string byte_str(byte_part, 2);
        bytes_buf[i] = ByteHelper::get_byte_from_str(byte_str);
    }
}