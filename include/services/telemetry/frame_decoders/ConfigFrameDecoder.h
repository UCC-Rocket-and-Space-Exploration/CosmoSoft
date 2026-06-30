
#ifndef COSMO_SOFT_CONFIGFRAMEDECODER_H
#define COSMO_SOFT_CONFIGFRAMEDECODER_H
#include "services/interfaces/IFrameDecoder.h"

class ConfigFrameDecoder : public IFrameDecoder{
public:
    FlightSample decode(Frame frame) override;
};

#endif //COSMO_SOFT_CONFIGFRAMEDECODER_H