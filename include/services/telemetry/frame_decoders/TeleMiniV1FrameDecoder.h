#ifndef COSMO_SOFT_TELEMINIFRAMEDECODER_H
#define COSMO_SOFT_TELEMINIFRAMEDECODER_H
#include "AltosFrameDecoder.h"
#include "services/interfaces/IFrameDecoder.h"

class TeleMiniV1FrameDecoder : public AltosFrameDecoder{
public:
    FlightSample decode(const Frame &frame) override;
};
#endif //COSMO_SOFT_TELEMINIFRAMEDECODER_H