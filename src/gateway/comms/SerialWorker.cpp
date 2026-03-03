#include <atomic>
#include <chrono>
#include <vector>

#include "../../../include/services/comms/SerialWorker.h"
#include "../../../include/gateway/SerialPortScannerFactory.h"

<<<<<<< HEAD:src/gateway/SerialWorker.cpp
SerialWorker::SerialWorker(IComms* comms)
    : m_running(false), m_connectedPort(comms), m_scanner(nullptr) {}

=======
>>>>>>> e7d53e9 (branching off from refactor to work on parsing thread):src/gateway/comms/SerialWorker.cpp
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
void SerialWorker::run() {
    //TODO change from static size buffer
    uint8_t buffer[256];
    while (m_running) {
        ssize_t n = m_connectedPort->read(buffer, sizeof(buffer));
        if (n > 0 && m_onData) {
            // Raw bytes need parser integration before this can emit real samples.
            m_onData({});
        } else if (n < 0 && m_onError) {
            m_onError("Read error");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}
