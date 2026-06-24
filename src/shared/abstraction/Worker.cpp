#include "shared/abstraction/Worker.h"

void Worker::start() {
    m_running = true;
    m_worker_thread = std::thread(&Worker::m_process, this);
}

void Worker::stop() {
    m_running = false;
    if (m_worker_thread.joinable()) {
        m_worker_thread.join();
    }
}