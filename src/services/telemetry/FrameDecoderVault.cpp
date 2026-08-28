#include "services/telemetry/FrameDecoderVault.h"

#include <stdexcept>

#include "services/telemetry/frame_decoders/ConfigFrameDecoder.h"
#include "services/telemetry/frame_decoders/CsvFrameDecoder.h"
#include "services/telemetry/frame_decoders/GpsFrameDecoder.h"
#include "services/telemetry/frame_decoders/TeleMegaImuFrameDecoder.h"
#include "shared/AltosPacketTypes.h"
#include "shared/ByteHelper.h"
#include "shared/exceptions/IncorrectAltosPacketType.h"

//exceptions:
//IncorrectAltosPacketType - if altos packet type is incorrect
//std::logic_error if format is incorrect
std::shared_ptr<IFrameDecoder> FrameDecoderVault::select(const Frame& frame) {
    switch (frame.format) {
        case Csv: {
            return m_cvs_frame_decoder;
        }
        case AltosFrame: {
            const auto packet_type_value = getAltosPacketType(frame);
            return selectAltosPacketDecoder(packet_type_value);
        }
        default:
            throw std::logic_error("Unknown frame format");
    }
}

uint8_t FrameDecoderVault::getAltosPacketType(const Frame& frame) {
    size_t packet_type_offset = frame.packet_start_index + 4 * 2;
    std::string byte_pts = std::to_string(frame.data[packet_type_offset]) + std::to_string(frame.data[packet_type_offset+1]);
    return ByteHelper::get_byte_from_str(byte_pts);
}

std::shared_ptr<IFrameDecoder> FrameDecoderVault::selectAltosPacketDecoder(uint8_t packet_type_value) {
    switch (packet_type_value) {
        case AltosPacketTypes::TeleMega15VKalman:
        case AltosPacketTypes::TeleMega30VKalman:
            return m_tele_mega_kalman_frame_decoder;
        case AltosPacketTypes::TeleMegaBMI088IMU:
        case AltosPacketTypes::TeleMegaInvensenseIMU:
        case AltosPacketTypes::TeleMegaMPU6000IMU:
        case AltosPacketTypes::TeleMegaBMX160IMU:
            return m_tele_mega_imu_frame_decoder;
        case AltosPacketTypes::Gps:
            return m_gps_frame_decoder;
        case AltosPacketTypes::Config:
            return m_config_frame_decoder;
        default:
            throw IncorrectAltosPacketType(packet_type_value);
    }
}

