#include <iostream>
#include <thread>

#include "gateway/comms/SerialFramerWorker.h"
#include "gateway/comms/windows/SerialCommsWindows.h"

// void SerialFramerWorker::start() {
//     std::cout << "Starting to run worker" << std::endl;
//     m_running = true;
//     m_thread = std::thread(&SerialFramerWorker::write_to_buffer_from_serial, this);
//     // m_worker_thread = std::thread(&ParserWorker::parse_flight_samples, this);
// }

void SerialFramerWorker::m_process() {
    try {
        std::cout << "entering worker loop" << std::endl;
        while (m_running) {
            Frame frame = m_framer->get_frame();
            std::cout << "got frame size: " << frame.data.size() << std::endl;
            for(auto i : frame.data) {
                std::cout << i;
            }
            std::cout << std::endl;

            if (!frame.data.empty()) {
                std::cout << "putting frame in buffer" << std::endl;
                m_buffer->put(frame);
            }
        }
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
    }
    std::cout << "exiting worker loop" << std::endl;
}
// void SerialFramerWorker::stop() {
//     std::cout << "calling stop" << std::endl;
//     m_running = false;
//     if (m_thread.joinable()) {
//         m_thread.join();
//     }
//     std::cout << "worker has stopped" << std::endl;
// }