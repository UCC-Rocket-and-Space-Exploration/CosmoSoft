#ifndef COSMO_SOFT_FRAME_H
#define COSMO_SOFT_FRAME_H
#include <cstdint>
#include <vector>
#include <domain/FrameFormat.h>

struct Frame {
	FrameFormat format;
	std::vector<uint8_t> data;
    std::size_t size; //TODO: delete
};
#endif //COSMO_SOFT_FRAME_H