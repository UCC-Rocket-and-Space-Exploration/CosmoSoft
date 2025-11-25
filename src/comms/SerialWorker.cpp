#include "comms/SerialWorker.h"
#include "comms/SerialPortScannerFactory.h"

SerialWorker::SerialWorker(IComms* comms) {
    m_running = false;
    m_connectedPort = comms;
    // m_scanner = SerialPortScannerFactory::createSerialPortScanner();
}

SerialWorker::~SerialWorker() {
    if (m_running) {
        stop();
    }
}

bool SerialWorker::start() {
    if (m_running) {
        return true;
    }
    try {
        m_workerThread = std::thread(&SerialWorker::run, this);
    } catch (const std::exception&) {
        return false;
    }
    m_running = true;
    return true;
}

void SerialWorker::stop() {
    m_running = false;
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
}

void SerialWorker::setDataCallback(DataCallback callback) {
    m_onData = std::move(callback);
}

void SerialWorker::setErrorCallback(ErrorCallback callback) {
    m_onError = std::move(callback);
}

// TODO: consider making async instead, but since this is in its own thread it should be fine.
void SerialWorker::run() {
    uint8_t buffer[256];
    while (m_running) {
        const ssize_t n = m_connectedPort->read(buffer, sizeof(buffer));
        if (n > 0 && m_onData) {
            m_onData(std::vector<uint8_t>(buffer, buffer + n));
        } else if (n < 0 && m_onError) {
            m_onError("Read error");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}
