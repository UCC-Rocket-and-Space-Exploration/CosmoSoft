#include "services/telemetry/frame_decoders/TeleMegaImuFrameDecoder.h"

FlightSample TeleMegaImuFrameDecoder::decode(const Frame& frame) {
    FlightSample sample{};
    throw_if_checksum_not_valid(frame);
    // size_t packet_index = get_packet_start_index(frame.data);

    int temperature = (frame.data[packet_index + 12] << 8) | frame.data[packet_index + 13];

    return sample;
}
