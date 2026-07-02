#ifndef COSMO_SOFT_TELEMEGAFRAMEDECODER_H
#define COSMO_SOFT_TELEMEGAFRAMEDECODER_H
#include "AltosFrameDecoder.h"
#include "services/interfaces/IFrameDecoder.h"

class TeleMegaKalmanFrameDecoder : public AltosFrameDecoder{
public:
    FlightSample decode(const Frame &frame) override;
};
#endif //COSMO_SOFT_TELEMEGAFRAMEDECODER_H