#include "services/telemetry/frame_decoders/TeleMegaImuFrameDecoder.h"

#include <limits>
#include <stdexcept>

FlightSample TeleMegaImuFrameDecoder::decode(const Frame &frame) {
  throw_if_checksum_not_valid(frame);
  if (get_numerical_field_le<uint8_t>(frame, 4, 1) != 0x08) {
    throw std::invalid_argument("Unsupported IMU variant");
  }
  const double missing = std::numeric_limits<double>::quiet_NaN();
  FlightSample sample{};
  sample.altitude = sample.pressure = sample.batteryVoltage =
      sample.distanceFromLaunchPoint = missing;
  sample.coordinates = {missing, missing};
  sample.acceleration = {missing, missing, missing};
  sample.angularVelocity = {missing, missing, missing};
  sample.angularRotation = {missing, missing, missing};
  // Verified wire units and intentionally unavailable IMU axes:
  // docs/protocol.md.
  sample.timestamp = get_numerical_field_le<uint16_t>(frame, 2, 2) * 10L;
  sample.temperature = get_numerical_field_le<int16_t>(frame, 12, 2) / 100.0;
  sample.rssi = get_numerical_field_le<int8_t>(frame, 32, 1) / 2.0 - 74.0;
  return sample;
}
