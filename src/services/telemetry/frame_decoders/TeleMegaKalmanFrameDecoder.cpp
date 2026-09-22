#include "services/telemetry/frame_decoders/TeleMegaKalmanFrameDecoder.h"

FlightSample TeleMegaKalmanFrameDecoder::decode(const Frame &frame) {
    FlightSample sample = decode_base(frame);
    // FlightSample sample{};
    sample.batteryVoltage = get_numerical_field_le<int16_t>(frame, 6, 2);
    sample.altitude = get_numerical_field_le<int16_t>(frame, 30, 2);
    return sample;
}
