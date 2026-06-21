#ifndef COSMO_SOFT_IFRAMEDECODER_H
#define COSMO_SOFT_IFRAMEDECODER_H
#include "domain/Frame.h"

class IFrameDecoder {
    public:
    virtual ~IFrameDecoder() = default;
    virtual void decode(Frame);
};
#endif //COSMO_SOFT_IFRAMEDECODER_H