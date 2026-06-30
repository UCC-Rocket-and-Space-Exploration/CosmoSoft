#ifndef COSMO_SOFT_TELEMEGAIMUFRAMEDECODER_H
#define COSMO_SOFT_TELEMEGAIMUFRAMEDECODER_H
#include "services/interfaces/IFrameDecoder.h"

class TeleMegaImuFrameDecoder : public IFrameDecoder{
public:
    FlightSample decode(Frame frame) override;
};

#endif //COSMO_SOFT_TELEMEGAIMUFRAMEDECODER_H