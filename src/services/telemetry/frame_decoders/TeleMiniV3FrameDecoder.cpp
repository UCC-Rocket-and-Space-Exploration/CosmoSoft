#include "services/telemetry/frame_decoders/TeleMiniV3FrameDecoder.h"

FlightSample TeleMiniV3FrameDecoder::decode(const Frame &frame) {
    FlightSample sample{};
    sample.timestamp = get_numerical_field_le<int>(frame, 2, 2);

    sample.batteryVoltage = get_numerical_field_le<int>(frame, 6, 2);
    sample.pressure = static_cast<double>(get_numerical_field_le<int>(frame, 12, 4)) / 10;
    sample.temperature = static_cast<double>(get_numerical_field_le<int16_t>(frame, 16, 2)) / 100;
    sample.altitude = get_numerical_field_le<int16_t>(frame, 22, 2);

    return sample;
}
