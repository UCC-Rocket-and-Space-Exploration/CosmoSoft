#ifndef COSMO_SOFT_FRAMER_H
#define COSMO_SOFT_FRAMER_H
#include <cstdint>

#define Kilobyte 1024

struct Frame {
    uint8_t* data;
    std::size_t size;
};

//TODO
// consider how errors are handled (malformed frame, etc.) and how to report them back to the caller; perhaps add an error callback or return an error code along with the frames
// consider including CRC field in frame format
// consider implementing ring buffer instead of bounded queue for O(1) enqueue and dequeue operations, but need to be careful with buffer management and edge cases (full vs empty)
// Make completely thread-safe; consider some kind pipelining between serialworker and parserworker,
class Framer {
public:
    //appends incoming bytes on to the input buffer, then processes the buffer to extract complete frames and move them to the output buffer
    void ingest(const uint8_t* data, std::size_t size);

    //pops all complete frames from the output buffer and writes them to the provided data buffer, returning an array of frames and the number of frames read (TODO: refactor to use std::vector or similar for better memory management)
    Frame *readData(uint8_t *data, std::size_t size);

    bool readableData(); //Returns true if there is at least one complete frame in the output buffer

    //other methods to consider:
    // - peekData() to look at the next frame without removing it from the output buffer
    // - clear() to reset the buffers and state of the framer
    // - setFrameDelimiter() to allow for different frame formats (e.g. start and end bytes, length prefix, etc.)
    // - setMaxFrameSize() to prevent buffer overflow and allow for variable frame sizes
    // - setErrorCallback() to allow for reporting framing errors back to the caller

private:
    uint8_t m_boundedInputBuffer[4 * Kilobyte] = {}; //TODO instantiat RingBuffer with this
    Frame m_boundedOutputBuffer[256] = {};


    //TODO

    //Will handle framing to seperate concerns from parser
};

#endif //COSMO_SOFT_FRAMER_H