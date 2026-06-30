#ifndef COSMO_SOFT_TELEMINIV3FRAMEDECODER_H
#define COSMO_SOFT_TELEMINIV3FRAMEDECODER_H
#include "services/interfaces/IFrameDecoder.h"

class TeleMiniV3FrameDecoder : public IFrameDecoder{
public:
    FlightSample decode(Frame frame) override;
};

#endif //COSMO_SOFT_TELEMINIV3FRAMEDECODER_H