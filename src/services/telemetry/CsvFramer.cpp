#include "services/telemetry/CsvFramer.h"

#include <cstring>
#include <iostream>
#include <vector>

Frame CsvFramer::get_frame() {
    Frame frame = Frame();
    uint8_t byte_buffer[1] = {};
    size_t frame_size = 0;
    uint8_t previous_byte = 0;
    uint8_t next_byte = 0;

    while (next_byte != frame_separator) {
        this->m_comms->read(byte_buffer, 1);
        next_byte = byte_buffer[0];
        if (frame_size != 0) {
            per_frame_buffer.push_back(previous_byte);
        }
        if (next_byte == 0) {
            break;
        }
        previous_byte = next_byte;
        frame_size++;
    }
    // frame.data.size = per_frame_buffer.size(); //discluding \n char
    frame.format = Csv;

    if (!per_frame_buffer.empty()) {
        frame.data = per_frame_buffer;
    }
    per_frame_buffer.clear();
    return frame;
}

CsvFramer::CsvFramer(const std::shared_ptr<IComms>& comms) : m_comms(comms) {
    per_frame_buffer = std::vector<uint8_t>();
}

CsvFramer::~CsvFramer() = default;
