#ifndef COSMO_SOFT_TELEMEGAIMUFRAMEDECODER_H
#define COSMO_SOFT_TELEMEGAIMUFRAMEDECODER_H
#include "AltosFrameDecoder.h"
#include "services/interfaces/IFrameDecoder.h"

class TeleMegaImuFrameDecoder : public AltosFrameDecoder{
public:
    FlightSample decode(const Frame &frame) override;
};

#endif //COSMO_SOFT_TELEMEGAIMUFRAMEDECODER_H