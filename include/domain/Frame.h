#include <domain/FrameFormat.h>

#ifndef COSMO_SOFT_FRAME_H
#define COSMO_SOFT_FRAME_H

struct Frame {
	FrameFormat format;
    uint8_t* data;
    std::size_t size;
};

#endif //COSMO_SOFT_FRAME_H