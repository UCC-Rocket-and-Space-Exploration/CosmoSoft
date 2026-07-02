#ifndef COSMO_SOFT_NUMBERDECODER_H
#define COSMO_SOFT_NUMBERDECODER_H
#include <cstdint>


class NumberDecoder {
public:
    static int FromBytesToInt32(const uint8_t *bytes, int bytes_count);
};


#endif //COSMO_SOFT_NUMBERDECODER_H