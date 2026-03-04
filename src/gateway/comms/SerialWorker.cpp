#include <atomic>

#include "../../../include/services/comms/SerialWorker.h"
#include "../../../include/gateway/SerialPortScannerFactory.h"

SerialWorker::~SerialWorker() {
    if (m_running) {
        stop();
    }
}

//TODO: implement handshake
bool SerialWorker::start() {
    if (m_workerThread.joinable()) return true;
        m_running = true;
        m_workerThread = std::thread(&SerialWorker::run, this);
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
    };
}

//TODO rewrite to instead pass to new parsing thread
void SerialWorker::run() const {
    //TODO change from static size buffer
    uint8_t buffer[256];
    while (m_running) {
        ssize_t n = m_connectedPort->read(buffer, sizeof(buffer));
        if (n > 0 && m_onData) {
            m_onData(std::vector<uint8_t>(buffer, buffer + n));
        } else if (n < 0 && m_onError) {
            m_onError("Read error");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}
