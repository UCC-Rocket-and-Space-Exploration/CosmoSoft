#ifndef COSMO_SOFT_FRAMEDECODERFACTORY_H
#define COSMO_SOFT_FRAMEDECODERFACTORY_H
#include <memory>

#include "domain/FrameFormat.h"
#include "services/interfaces/IFrameDecoder.h"

class FrameDecoderFactory {
public:
    static std::unique_ptr<IFrameDecoder> create(FrameFormat frame_format);
};
#endif //COSMO_SOFT_FRAMEDECODERFACTORY_H