#include "services/telemetry/frame_decoders/TeleMiniV1FrameDecoder.h"

FlightSample TeleMiniV1FrameDecoder::decode(const Frame &frame) {
    FlightSample sample = decode_base(frame);

    // FlightSample sample{};

    //todo: rename to earlierversionsdecoder
    sample.altitude = get_numerical_field_le<int>(frame, 22, 2);
    sample.pressure = static_cast<double>(get_numerical_field_le<int>(frame, 24, 1)) / 10;
    sample.temperature = static_cast<double>(get_numerical_field_le<int>(frame, 10, 2)) / 100;
    sample.batteryVoltage = get_numerical_field_le<int>(frame, 12, 2);

    return sample;
}
