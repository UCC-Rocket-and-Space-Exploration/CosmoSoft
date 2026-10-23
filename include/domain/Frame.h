#ifndef COSMO_SOFT_FRAME_H
#define COSMO_SOFT_FRAME_H
#include <cstdint>
#include <vector>
#include <domain/FrameFormat.h>

/** @brief Encoded telemetry frame and its payload offset. */
class Frame {
public:
  FrameFormat format = Csv;
  std::vector<uint8_t> data;
  mutable size_t packet_start_index = 0;
};
#endif //COSMO_SOFT_FRAME_H

// struct Frame {
//     FrameFormat format;
//     uint8_t* data;
//     std::size_t size;
// };

