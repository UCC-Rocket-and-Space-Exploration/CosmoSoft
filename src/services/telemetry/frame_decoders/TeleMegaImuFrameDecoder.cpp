#include "services/telemetry/frame_decoders/TeleMegaImuFrameDecoder.h"

#include "shared/ByteHelper.h"

FlightSample TeleMegaImuFrameDecoder::decode(const Frame& frame) {
    FlightSample sample{};
    throw_if_checksum_not_valid(frame);

    uint8_t pressure_bs[4] = {};
    get_field_bytes(frame, pressure_bs, 8, 4);
    sample.pressure = (double)ByteHelper::from_bytes_to_int32_le(pressure_bs, 4) / 10;

    uint8_t temperature_bs[2] = {};
    get_field_bytes(frame, temperature_bs, 12, 2);
    sample.temperature = (double)ByteHelper::from_bytes_to_int32_le(temperature_bs, 2) / 100.0;

    uint8_t accel_x_bs[2] = {};
    get_field_bytes(frame, accel_x_bs, 14, 2);
    sample.acceleration.x = ByteHelper::from_bytes_to_int32_le(accel_x_bs, 2);

    uint8_t accel_y_bs[2] = {};
    get_field_bytes(frame, accel_x_bs, 16, 2);
    sample.acceleration.y = ByteHelper::from_bytes_to_int32_le(accel_y_bs, 2);

    uint8_t accel_z_bs[2] = {};
    get_field_bytes(frame, accel_z_bs, 18, 2);
    sample.acceleration.z = ByteHelper::from_bytes_to_int32_le(accel_z_bs, 2);

    uint8_t roll_bs[2] = {};
    get_field_bytes(frame, roll_bs, 18, 2);
    sample.angularVelocity.x = ByteHelper::from_bytes_to_int32_le(roll_bs, 2);

    uint8_t pitch_bs[2] = {};
    get_field_bytes(frame, pitch_bs, 18, 2);
    sample.angularVelocity.y = ByteHelper::from_bytes_to_int32_le(pitch_bs, 2);

    uint8_t yaw_bs[2] = {};
    get_field_bytes(frame, yaw_bs, 18, 2);
    sample.angularVelocity.z = ByteHelper::from_bytes_to_int32_le(yaw_bs, 2);

    return sample;
}
