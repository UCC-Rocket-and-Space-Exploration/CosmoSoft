
#include "services/telemetry/frame_decoders/ConfigFrameDecoder.h"

FlightSample ConfigFrameDecoder::decode(const Frame &frame) {
    FlightSample sample{};
    // sample.altitude = get_numerical_field_le<int>(frame, 6, 2);

    return sample;
}
