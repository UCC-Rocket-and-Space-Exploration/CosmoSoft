#include "services/comms/SerialWorker.h"

#include <chrono>
#include <vector>

SerialWorker::~SerialWorker() {
    if (m_running) {
        stop();
    }
}

bool SerialWorker::start() {
    if (m_workerThread.joinable()) {
        return true;
    }
    m_running = true;
    m_workerThread = std::thread([this]() { run(); });
    if (!m_workerThread.joinable()) {
        m_running = false;
        return false;
    }
    return true;
}

void SerialWorker::stop() {
    m_running = false;
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
}

void SerialWorker::run() {
    uint8_t buffer[256];
    while (m_running) {
        const ssize_t n = m_connectedPort->read(buffer, sizeof(buffer));
        if (n > 0 && m_onData) {
            m_onData(std::vector<uint8_t>(buffer, buffer + static_cast<size_t>(n)));
        } else if (n < 0 && m_onError) {
            m_onError("Read error");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}
