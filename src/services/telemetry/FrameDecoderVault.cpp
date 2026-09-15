#include "services/telemetry/FrameDecoderVault.h"

#include "shared/AltosPacketTypes.h"
#include "shared/ByteHelper.h"
#include "shared/exceptions/IncorrectAltosPacketType.h"

#include <algorithm>
#include <stdexcept>

std::shared_ptr<IFrameDecoder> FrameDecoderVault::select(const Frame &frame) {
  switch (frame.format) {
  case Csv:
    return m_cvs_frame_decoder;
  case AltosFrame:
    return selectAltosPacketDecoder(getAltosPacketType(frame));
  default:
    throw std::logic_error("Unknown frame format");
  }
}

bool FrameDecoderVault::exists(const std::string &element) {
  return std::find(m_types.begin(), m_types.end(), element) != m_types.end();
}

uint8_t FrameDecoderVault::getAltosPacketType(const Frame &frame) {
  const auto start = frame.packet_start_index;
  if (start > frame.data.size() || frame.data.size() - start != 70)
    throw std::invalid_argument("Invalid AltOS packet length");
  return ByteHelper::get_byte_from_str(std::string(
      frame.data.begin() + start + 8, frame.data.begin() + start + 10));
}

std::shared_ptr<IFrameDecoder>
FrameDecoderVault::selectAltosPacketDecoder(uint8_t type) {
  switch (type) {
  case AltosPacketTypes::TeleMega15VKalman:
  case AltosPacketTypes::TeleMega30VKalman:
    return m_tele_mega_kalman_frame_decoder;
  case AltosPacketTypes::TeleMegaInvensenseIMU:
  case AltosPacketTypes::TeleMegaBMI088IMU:
  case AltosPacketTypes::TeleMegaMPU6000IMU:
  case AltosPacketTypes::TeleMegaBMX160IMU:
    return m_tele_mega_imu_frame_decoder;
  case AltosPacketTypes::Gps:
    return m_gps_frame_decoder;
  case AltosPacketTypes::Config:
    return m_config_frame_decoder;
  case AltosPacketTypes::TeleMiniV3:
    return m_tele_mini_v3_frame_decoder;
  case AltosPacketTypes::TeleMiniV1:
    return m_tele_mini_v1_frame_decoder;
  default:
    throw IncorrectAltosPacketType(type);
  }
}
