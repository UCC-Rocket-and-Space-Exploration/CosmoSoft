#include "services/telemetry/frame_decoders/TeleMegaImuFrameDecoder.h"

#include <iostream>

#include "shared/ByteHelper.h"



FlightSample TeleMegaImuFrameDecoder::decode(const Frame& frame) {
    FlightSample sample{};
    throw_if_checksum_not_valid(frame);

    sample.timestamp = get_numerical_field_le<int>(frame, 2, 2);

    sample.pressure = static_cast<double>(get_numerical_field_le<int>(frame, 8, 4)) / 10;

    sample.temperature = static_cast<double>(get_numerical_field_le<int16_t>(frame, 12, 2)) / 100;

    sample.acceleration.x = get_numerical_field_le<int16_t>(frame, 14, 2);

    sample.acceleration.y = get_numerical_field_le<int16_t>(frame, 16, 2);

    sample.acceleration.z = get_numerical_field_le<int16_t>(frame, 18, 2);

    sample.angularVelocity.x = get_numerical_field_le<int16_t>(frame, 20, 2);

    sample.angularVelocity.y = get_numerical_field_le<int16_t>(frame, 22, 2);

    sample.angularVelocity.z = get_numerical_field_le<int16_t>(frame, 24, 2);

    return sample;
}
