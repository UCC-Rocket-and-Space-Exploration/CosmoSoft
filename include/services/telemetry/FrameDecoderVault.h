#ifndef COSMO_SOFT_FRAMEDECODERFACTORY_H
#define COSMO_SOFT_FRAMEDECODERFACTORY_H
#include <memory>

#include "frame_decoders/ConfigFrameDecoder.h"
#include "frame_decoders/CsvFrameDecoder.h"
#include "frame_decoders/GpsFrameDecoder.h"
#include "frame_decoders/TeleMegaImuFrameDecoder.h"
#include "frame_decoders/TeleMegaKalmanFrameDecoder.h"
#include "services/interfaces/IFrameDecoder.h"


class FrameDecoderVault {
public:
    std::shared_ptr<IFrameDecoder> select(Frame frame);
private:
    std::shared_ptr<CsvFrameDecoder> m_cvs_frame_decoder = std::make_shared<CsvFrameDecoder>();
    std::shared_ptr<IFrameDecoder> m_tele_mega_kalman_frame_decoder = std::make_shared<TeleMegaKalmanFrameDecoder>();
    std::shared_ptr<TeleMegaImuFrameDecoder> m_tele_mega_imu_frame_decoder = std::make_shared<TeleMegaImuFrameDecoder>();
    std::shared_ptr<GpsFrameDecoder> m_gps_frame_decoder = std::make_shared<GpsFrameDecoder>();
    std::shared_ptr<ConfigFrameDecoder> m_config_frame_decoder = std::make_shared<ConfigFrameDecoder>();

    static uint8_t getAltosPacketType(std::vector<uint8_t> data);
    std::shared_ptr<IFrameDecoder> selectAltosPacketDecoder(uint8_t packet_type_value);

};
#endif //COSMO_SOFT_FRAMEDECODERFACTORY_H