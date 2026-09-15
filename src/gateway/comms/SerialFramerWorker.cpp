#include "gateway/comms/SerialFramerWorker.h"
#include <chrono>

void SerialFramerWorker::m_process() {
  while (m_running) {
    try {
      Frame frame = m_framer->get_frame(m_running);
      if (!frame.data.empty()) {
        if (m_results_logger)
          m_results_logger->LogLine(
              std::string(frame.data.begin(), frame.data.end()));
        m_buffer->put(std::move(frame));
      } else if (m_framer->finished()) {
        break;
      } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
    } catch (const std::exception &error) {
      if (m_debug_logger)
        m_debug_logger->LogLine(std::string(error.what()));
      if (m_framer->finished())
        break;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }
  m_running = false;
  if (m_completed)
    m_completed();
}
