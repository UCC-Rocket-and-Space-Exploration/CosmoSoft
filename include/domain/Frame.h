#ifndef COSMO_SOFT_FRAME_H
#define COSMO_SOFT_FRAME_H

#include <cstddef>
#include <cstdint>

#include "domain/FrameFormat.h"

struct Frame {
    FrameFormat format;
    uint8_t* data;
    std::size_t size;
};

#endif //COSMO_SOFT_FRAME_H
