#ifndef COSMO_SOFT_IALTOSFRAMEDECODER_H
#define COSMO_SOFT_IALTOSFRAMEDECODER_H
#include "services/interfaces/IFrameDecoder.h"

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
};
#endif //COSMO_SOFT_IALTOSFRAMEDECODER_H