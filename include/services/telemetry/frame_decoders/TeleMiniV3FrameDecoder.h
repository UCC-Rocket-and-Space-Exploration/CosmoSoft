#ifndef COSMO_SOFT_TELEMINIV3FRAMEDECODER_H
#define COSMO_SOFT_TELEMINIV3FRAMEDECODER_H
#include "AltosFrameDecoder.h"
#include "services/interfaces/IFrameDecoder.h"

class TeleMiniV3FrameDecoder : public AltosFrameDecoder{
public:
    FlightSample decode(const Frame &frame) override;
};

#endif //COSMO_SOFT_TELEMINIV3FRAMEDECODER_H