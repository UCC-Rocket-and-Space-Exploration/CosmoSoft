#ifndef COSMO_SOFT_FRAME_H
#define COSMO_SOFT_FRAME_H
#include <cstdint>
#include <vector>
#include <domain/FrameFormat.h>

class Frame {
public:
	FrameFormat format;
	std::vector<uint8_t> data;
};
#endif //COSMO_SOFT_FRAME_H