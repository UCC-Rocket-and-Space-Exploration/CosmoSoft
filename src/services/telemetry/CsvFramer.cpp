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

    while (next_byte != '\n') {
        this->m_comms->read(byte_buffer, 1);
        next_byte = byte_buffer[0];
        if (frame_size != 0) {
            per_frame_buffer.push_back(previous_byte);
        }
        frame_size++;
        previous_byte = next_byte;
    }
    frame.size = frame_size - 1; //discluding \n char

    if (frame.size == 0) {
        frame.data = nullptr;
    }
    else {
        std::cout << "frame.data not emp" << std::endl;
        frame.data = per_frame_buffer.data();
    }
    frame.format = Csv;
    per_frame_buffer.clear();
    return frame;
}

CsvFramer::CsvFramer(std::unique_ptr<IComms> comms) {
    m_comms = std::move(comms);
    per_frame_buffer = std::vector<uint8_t>();
}
CsvFramer::~CsvFramer() {
}