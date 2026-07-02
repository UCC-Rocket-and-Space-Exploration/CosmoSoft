#ifndef COSMO_SOFT_IFRAMEDECODER_H
#define COSMO_SOFT_IFRAMEDECODER_H
#include "domain/FlightSample.h"
#include "domain/Frame.h"

class IFrameDecoder {
    //decoders must be stateless(view vault)
    public:
    virtual ~IFrameDecoder() = default;
    virtual FlightSample decode(const Frame &frame) = 0;
};
#endif //COSMO_SOFT_IFRAMEDECODER_H