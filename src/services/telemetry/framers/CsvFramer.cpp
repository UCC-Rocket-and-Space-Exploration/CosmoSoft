#include "services/telemetry/framers/CsvFramer.h"

#include <cstring>
#include <iostream>
#include <vector>

Frame CsvFramer::get_frame(const bool& running) {
    Frame frame = Frame();
    // std::cout << this->m_comms;
    std::vector<uint8_t> per_frame_buffer = {};
    uint8_t byte_buffer[1] = {};
    size_t frame_size = 0;
    uint8_t previous_byte = 0;
    uint8_t next_byte = 0;

    std::cout << "entering" << std::endl;
    while (next_byte != frame_separator && running) {
        const ssize_t read_count = this->m_comms->read(byte_buffer, 1);
        next_byte = byte_buffer[0];
        if (frame_size != 0) {
            per_frame_buffer.push_back(previous_byte);
        }
        if (read_count == 0) {
            break;
        }
        previous_byte = next_byte;
        frame_size++;
    }
    frame.format = Csv;

    if (!per_frame_buffer.empty()) {
        frame.data = per_frame_buffer;
    }

    return frame;
}

CsvFramer::CsvFramer(const std::shared_ptr<IComms>& comms) : m_comms(comms) {
    // m_comms = comms;
    // std::cout << "from constructor: " << comms;
}

CsvFramer::~CsvFramer() = default;
