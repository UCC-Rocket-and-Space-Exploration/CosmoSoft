#ifndef COSMO_SOFT_TELEMEGAFRAMEDECODER_H
#define COSMO_SOFT_TELEMEGAFRAMEDECODER_H
#include "services/interfaces/IFrameDecoder.h"

class TeleMegaFrameDecoder : public IFrameDecoder{
public:
    FlightSample decode(Frame frame) override;
};
#endif //COSMO_SOFT_TELEMEGAFRAMEDECODER_H