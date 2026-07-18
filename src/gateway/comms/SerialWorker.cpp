#include "gateway/comms/SerialWorker.h"

#include <exception>
#include <system_error>
#include <vector>

SerialWorker::~SerialWorker() {
    stop();
}

bool SerialWorker::start() {
    std::scoped_lock lock(m_lifecycleMutex);

    if (m_workerThread.joinable()) {
        if (m_running.load(std::memory_order_acquire)) {
            return true;
        }
        m_workerThread.join();
    }
    if (m_connectedPort == nullptr || !m_connectedPort->isOpen()) {
        return false;
    }

    m_running.store(true, std::memory_order_release);
    try {
        m_workerThread = std::jthread([this](const std::stop_token stopToken) {
            run(stopToken);
        });
    } catch (const std::system_error &) {
        m_running.store(false, std::memory_order_release);
        return false;
    }
    return true;
}

bool SerialWorker::isRunning() const noexcept {
    return m_running.load(std::memory_order_acquire);
}

void SerialWorker::stop() {
    std::jthread worker;
    {
        std::scoped_lock lock(m_lifecycleMutex);

        m_running.store(false, std::memory_order_release);
        if (m_workerThread.joinable()) {
            m_workerThread.request_stop();
        }

        // IComms::close() is the platform-neutral cancellation operation. Both
        // serial implementations make concurrent close/read safe and bounded.
        try {
            if (m_connectedPort != nullptr && m_connectedPort->isOpen()) {
                m_connectedPort->close();
            }
        } catch (const std::exception &) {
            // Cancellation must still reach the join path. A backend that
            // throws from close() must not terminate this worker's destructor.
        }

        // A callback may request stop from the reader thread. Leave the
        // jthread owned by this object in that case; a later external stop (or
        // normal destruction) can join it without detaching live object code.
        if (m_workerThread.joinable()
            && m_workerThread.get_id() == std::this_thread::get_id()) {
            return;
        }

        worker = std::move(m_workerThread);
    }

    if (worker.joinable()) {
        worker.join();
    }
}

void SerialWorker::run(const std::stop_token stopToken) {
    uint8_t buffer[256];
    const auto reportError = [this](const std::string &message) {
        if (!m_onError) {
            return;
        }
        try {
            m_onError(message);
        } catch (const std::exception &) {
        }
    };
    const auto closeConnection = [this]() {
        try {
            if (m_connectedPort != nullptr && m_connectedPort->isOpen()) {
                m_connectedPort->close();
            }
        } catch (const std::exception &) {
        }
    };

    while (!stopToken.stop_requested()) {
        ssize_t n = -1;
        try {
            n = m_connectedPort->read(buffer, sizeof(buffer));
        } catch (const std::exception &error) {
            reportError(std::string("Serial read threw an exception: ") + error.what());
            closeConnection();
            break;
        }
        if (stopToken.stop_requested()) {
            break;
        }

        if (n > static_cast<ssize_t>(sizeof(buffer))) {
            reportError("Serial backend returned more bytes than requested");
            closeConnection();
            break;
        }

        if (n > 0 && m_onData) {
            try {
                m_onData(std::vector<uint8_t>(buffer, buffer + static_cast<size_t>(n)));
            } catch (const std::exception &error) {
                reportError(std::string("Data callback failed: ") + error.what());
                closeConnection();
                break;
            }
        } else if (n < 0) {
            reportError("Serial read failed");
            closeConnection();
            break;
        }
    }

    m_running.store(false, std::memory_order_release);
}
