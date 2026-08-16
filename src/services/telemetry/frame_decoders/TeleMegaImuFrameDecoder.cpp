#include "services/telemetry/frame_decoders/TeleMegaImuFrameDecoder.h"

#include <iostream>

#include "shared/ByteHelper.h"

FlightSample TeleMegaImuFrameDecoder::decode(const Frame& frame) {
    FlightSample sample{};
    throw_if_checksum_not_valid(frame);

    std::cout << "decoding" << std::endl;
    uint8_t tick_bs[2] = {};
    get_field_bytes(frame, tick_bs, 2, 2);
    sample.timestamp = ByteHelper::from_bytes_to_int32_le(tick_bs, 2);
    std::cout << "timestamp decoded" << std::endl;

    uint8_t pressure_bs[4] = {};
    get_field_bytes(frame, pressure_bs, 8, 4);
    sample.pressure = (double)(static_cast<int32_t>(ByteHelper::from_bytes_to_int32_le(pressure_bs, 4))) / 10;
    std::cout << "pressure decoded" << std::endl;

    uint8_t temperature_bs[2] = {};
    get_field_bytes(frame, temperature_bs, 12, 2);
    double casted_temp = static_cast<int16_t>(ByteHelper::from_bytes_to_int32_le(temperature_bs, 2));
    sample.temperature = casted_temp / 100;
    std::cout << "temp decoded" << std::endl;

    uint8_t accel_x_bs[2] = {};
    get_field_bytes(frame, accel_x_bs, 14, 2);
    sample.acceleration.x = static_cast<int16_t>(ByteHelper::from_bytes_to_int32_le(accel_x_bs, 2));
    std::cout << "accel_x decoded" << std::endl;

    uint8_t accel_y_bs[2] = {};
    get_field_bytes(frame, accel_y_bs, 16, 2);
    sample.acceleration.y = static_cast<int16_t>(ByteHelper::from_bytes_to_int32_le(accel_y_bs, 2));
    std::cout << "accel_y decoded" << std::endl;

    uint8_t accel_z_bs[2] = {};
    get_field_bytes(frame, accel_z_bs, 18, 2);
    sample.acceleration.z = static_cast<int16_t>(ByteHelper::from_bytes_to_int32_le(accel_z_bs, 2));
    std::cout << "accel_z decoded" << std::endl;

    uint8_t roll_bs[2] = {};
    get_field_bytes(frame, roll_bs, 20, 2);
    sample.angularVelocity.x = static_cast<int16_t>(ByteHelper::from_bytes_to_int32_le(roll_bs, 2));

    uint8_t pitch_bs[2] = {};
    get_field_bytes(frame, pitch_bs, 22, 2);
    sample.angularVelocity.y = static_cast<int16_t>(ByteHelper::from_bytes_to_int32_le(pitch_bs, 2));

    uint8_t yaw_bs[2] = {};
    get_field_bytes(frame, yaw_bs, 24, 2);
    sample.angularVelocity.z = static_cast<int16_t>(ByteHelper::from_bytes_to_int32_le(yaw_bs, 2));

    return sample;
}
