#ifndef COSMO_SOFT_TELEMINIFRAMEDECODER_H
#define COSMO_SOFT_TELEMINIFRAMEDECODER_H
#include "services/interfaces/IFrameDecoder.h"

class TeleMiniFrameDecoder : public IFrameDecoder{
public:
    FlightSample decode(Frame frame) override;
};
#endif //COSMO_SOFT_TELEMINIFRAMEDECODER_H