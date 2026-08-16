
#ifndef COSMO_SOFT_CONFIGFRAMEDECODER_H
#define COSMO_SOFT_CONFIGFRAMEDECODER_H
#include "AltosFrameDecoder.h"
#include "services/interfaces/IFrameDecoder.h"

class ConfigFrameDecoder : public AltosFrameDecoder{
public:
    FlightSample decode(const Frame &frame) override;
};

#endif //COSMO_SOFT_CONFIGFRAMEDECODER_H