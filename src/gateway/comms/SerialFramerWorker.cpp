#include <iostream>
#include <thread>

#include "gateway/comms/SerialFramerWorker.h"
#include "gateway/comms/windows/SerialCommsWindows.h"

void SerialFramerWorker::run() {
    m_running = true;
    std::cout << "Starting to run worker" << std::endl;
    m_thread = std::thread(&SerialFramerWorker::write_to_buffer_from_serial, this);
}

void SerialFramerWorker::write_to_buffer_from_serial() const {
    try {
        std::cout << "entering worker loop" << std::endl;
        while (m_running) {
            Frame frame = m_framer->get_frame();
            std::cout << "got frame size: " << frame.size << std::endl;
            for(auto i : frame.data) {
                std::cout << i;
            }
            std::cout << std::endl;

            if (frame.size != 0) {
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
void SerialFramerWorker::stop() {
    std::cout << "calling stop" << std::endl;
    m_running = false;
    if (m_thread.joinable()) {
        m_thread.join();
    }
    std::cout << "worker has stopped" << std::endl;
}