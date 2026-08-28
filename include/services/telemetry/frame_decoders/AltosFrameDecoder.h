#ifndef COSMO_SOFT_IALTOSFRAMEDECODER_H
#define COSMO_SOFT_IALTOSFRAMEDECODER_H
#include "services/interfaces/IFrameDecoder.h"
#include "shared/ByteHelper.h"

//Notation:
//frame includes signature, length byte(22), packet bytes + status bytes including checksum
//packet starts after length byte, ends where another packet sign starts(yes, including checksum)
class AltosFrameDecoder : public IFrameDecoder {
    public:
    FlightSample decode(const Frame &frame) override = 0;
protected:
    //packet starts right after length byte, ends after checksum bytes
    static bool checksum_valid(const Frame& frame);
    static void throw_if_checksum_not_valid(const Frame& frame);
    static void get_field_bytes(const Frame& frame, uint8_t* bytes_buf, size_t packet_field_start_offset, size_t read_count);
    template <typename T>
    static int get_numerical_field_le(const Frame &frame, size_t packet_field_start_offset, size_t read_count);
};

template <typename T>
int AltosFrameDecoder::get_numerical_field_le(const Frame &frame, size_t packet_field_start_offset, size_t read_count) {
    std::vector<uint8_t> field_bytes(read_count);
    uint8_t* raw_field_bytes = field_bytes.data();
    get_field_bytes(frame, raw_field_bytes, packet_field_start_offset, read_count);
    return static_cast<T>(ByteHelper::from_bytes_to_int32_le(raw_field_bytes, read_count));
}
#endif //COSMO_SOFT_IALTOSFRAMEDECODER_H