#ifndef COSMO_SOFT_FRAME_H
#define COSMO_SOFT_FRAME_H
#include <cstdint>
#include <vector>
#include <domain/FrameFormat.h>

class Frame {
public:
	FrameFormat format;
	std::vector<uint8_t> data;
	size_t packet_start_index = 0;
};
#endif //COSMO_SOFT_FRAME_H

// struct Frame {
//     FrameFormat format;
//     uint8_t* data;
//     std::size_t size;
// };

