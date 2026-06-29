
#include "services/telemetry/framers/TeleFramer.h"

#include <cstring>
#include <iostream>
#include <vector>

#include "domain/Frame.h"

TeleFramer::~TeleFramer() = default;

Frame TeleFramer::get_frame(const bool& running) {
    constexpr size_t packet_data_length = 0x22;
    constexpr size_t full_packet_length = packet_data_length + 1; //adding checksum byte

    Frame frame = {};
    verify_sign(running);
    uint8_t len_byte_buf[1] = {};
    std::cout << "skipping bytes: " << std::endl;

    do {
        m_comms->read(len_byte_buf, 1);
        std::cout << len_byte_buf[0] << std::endl;
        if (len_byte_buf[0] == packet_length_value){
            break;
        }
    }while (running);

    uint8_t frame_data[full_packet_length] = {};
    m_comms->read(frame_data, full_packet_length);

    std::cout << "actual bytes: " << std::endl;
    for (auto a: frame_data) {
        std::cout << a;
    }

    std::vector<uint8_t> v = {};
    copy(frame_data, frame_data + full_packet_length, back_inserter(v));
    frame.data = v;

    return frame;
}

bool TeleFramer::sign_start(uint8_t byte) {
    return static_cast<int>(byte) == static_cast<int>('T');
}

void TeleFramer::verify_sign(bool running) const {
    uint8_t sign[4] = {};
    while (running) {
        size_t read_bytes = this->m_comms->read(sign, 1);
        if (sign[0] == 'T') {
            this->m_comms->read(sign, 4);
            const bool sign_verified = sign[0] == 'E' && sign[1] == 'L' && sign[2] == 'E' && sign[3] == 'M';
            if (sign_verified) {
                break;
            }
        }
    }
}
