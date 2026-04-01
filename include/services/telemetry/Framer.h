#ifndef COSMO_SOFT_FRAMER_H
#define COSMO_SOFT_FRAMER_H

#include <cstddef>
#include <cstdint>

#define Kilobyte 1024

struct Frame {
    uint8_t *data = nullptr;
    std::size_t size = 0;
};

class Framer {
public:
    void ingest(const uint8_t *data, std::size_t size);

    Frame *readData(uint8_t *data, std::size_t size);

    bool readableData();

    bool try_next_frame(Frame &out);

private:
    uint8_t m_boundedInputBuffer[4 * Kilobyte] = {};
    Frame m_boundedOutputBuffer[256] = {};
};

#endif // COSMO_SOFT_FRAMER_H
