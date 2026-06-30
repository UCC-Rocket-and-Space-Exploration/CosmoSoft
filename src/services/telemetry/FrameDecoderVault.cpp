#include "services/telemetry/FrameDecoderVault.h"
#include "services/telemetry/frame_decoders/ConfigFrameDecoder.h"
#include "services/telemetry/frame_decoders/CsvFrameDecoder.h"
#include "services/telemetry/frame_decoders/GpsFrameDecoder.h"
#include "services/telemetry/frame_decoders/TeleMegaImuFrameDecoder.h"
#include "shared/AltosPacketTypes.h"
#include "shared/exceptions/IncorrectAltosPacketType.h"

//exceptions:
//IncorrectAltosPacketType - if altos packet type is incorrect
// std::logic_error if format is incorrect
std::shared_ptr<IFrameDecoder> FrameDecoderVault::select(Frame frame) {

    switch (frame.format) {
        case Csv: {
            return m_cvs_frame_decoder;
        }
            case AltosFrame: {
            auto packet_type_value = getAltosPacketType(frame.data);
            return selectAltosPacketDecoder(packet_type_value);
        }
        default:
            throw std::logic_error("Unknown frame format");
    }
}

uint8_t FrameDecoderVault::getAltosPacketType(std::vector<uint8_t> data) {
    constexpr size_t packet_type_offset = 4;
    return data[packet_type_offset];
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

