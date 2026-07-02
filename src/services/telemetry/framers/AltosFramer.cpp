
#include "services/telemetry/framers/AltosFramer.h"

#include <cstring>
#include <iostream>
#include <vector>

#include "domain/Frame.h"

AltosFramer::~AltosFramer() = default;

Frame AltosFramer::get_frame(const bool& running) {
    constexpr size_t packet_bytes_data_length = 0x22;
    constexpr size_t full_packet_length = (packet_bytes_data_length + 1) * 2; //adding checksum byte
    verify_sign(running);
    Frame frame = {};

    std::vector<uint8_t> full_frame_data = {'T', 'E', 'L', 'E', 'M'};
    uint8_t len_byte_buf[1] = {};
    std::string packet_len = "00";
    size_t current_pos = 5;
    do {
        m_comms->read(len_byte_buf, 1);
        std::cout << len_byte_buf[0] << std::endl;
        full_frame_data.push_back(len_byte_buf[0]);
        if (len_byte_buf[0] == '2') {
            if (packet_len[0] != '2') {
                packet_len[0] = '2';
            }
            else {
                packet_len[1] = '2';
            }
        }

        if (packet_len == "22"){
            break;
        }
        current_pos++;

    }while (running);
    frame.packet_start_index = current_pos + 1;

    uint8_t frame_data[full_packet_length] = {};
    m_comms->read(frame_data, full_packet_length);
    full_frame_data.insert(full_frame_data.end(), frame_data, frame_data + full_packet_length);

    frame.data = full_frame_data;
    frame.format = AltosFrame;
    return frame;
}

bool AltosFramer::sign_start(uint8_t byte) {
    return static_cast<int>(byte) == static_cast<int>('T');
}

void AltosFramer::verify_sign(bool running) const {
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
