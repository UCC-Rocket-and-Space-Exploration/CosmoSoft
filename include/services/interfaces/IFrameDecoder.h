#ifndef COSMO_SOFT_IFRAMEDECODER_H
#define COSMO_SOFT_IFRAMEDECODER_H
#include "domain/FlightSample.h"
#include "domain/Frame.h"

class IFrameDecoder {
    public:
    virtual ~IFrameDecoder() = default;
    virtual FlightSample decode(Frame frame) = 0;
};
#endif //COSMO_SOFT_IFRAMEDECODER_H