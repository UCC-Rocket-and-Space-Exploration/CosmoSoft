#include "shared/abstraction/Worker.h"

void Worker::start() {
  if (m_running.exchange(true))
    return;
  if (m_worker_thread.joinable())
    m_worker_thread.join();
  m_worker_thread = std::jthread(&Worker::m_process, this);
}

void Worker::stop() {
    m_running = false;
    if (m_worker_thread.joinable()) {
        m_worker_thread.join();
    }
}