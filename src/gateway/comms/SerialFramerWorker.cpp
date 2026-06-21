
#include "gateway/comms/SerialFramerWorker.h"
#include <thread>

void SerialFramerWorker::run() {
    m_running = true;
    m_thread = std::thread([this]() { write_to_buffer_from_serial(); });
}

void SerialFramerWorker::write_to_buffer_from_serial() const {
    while (m_running) {
        Frame frame = m_framer->get_frame();
        m_buffer->put(frame);
    }
}
void SerialFramerWorker::stop() {
    m_running = false;
    if (m_thread.joinable()) {
        m_thread.join();
    }
}