#include "services/telemetry/framers/AltosFramer.h"
#include "shared/exceptions/SerialTimeout.h"
#include <stdexcept>

Frame AltosFramer::get_frame(const bool &running) {
  return read_frame([&running] { return running; });
}
Frame AltosFramer::get_frame(const std::atomic_bool &running) {
  return read_frame([&running] { return running.load(); });
}
Frame AltosFramer::read_frame(const std::function<bool()> &running) {
  while (running()) {
    uint8_t byte = 0;
    ssize_t count = 0;
    try {
      count = m_comms->read(&byte, 1);
    } catch (const SerialTimeout &) {
      return {};
    } catch (const std::exception &) {
      m_comms->close();
      throw;
    }
    if (count < 0) {
      m_comms->close();
      throw std::runtime_error("Recorded/serial input read failed");
    }
    if (count == 0) {
      if (finished() && !m_pending.empty()) {
        m_pending.clear();
        throw std::runtime_error("Truncated AltOS packet at end of input");
      }
      return {};
    }
    ++m_received;
    // 'T' cannot appear in hex, so it is also a resynchronization boundary.
    if (byte == 'T') {
      const bool truncated = m_pending.size() > 1;
      m_pending = "T";
      if (truncated)
        throw std::runtime_error("Resynchronized after truncated AltOS packet");
      continue;
    }
    if (m_pending.empty())
      continue;
    m_pending += static_cast<char>(byte);
    if (m_pending.size() <= 5) {
      if (std::string("TELEM").compare(0, m_pending.size(), m_pending) != 0)
        m_pending.clear();
      continue;
    }
    const size_t start = m_pending[5] == ' ' ? 8 : 7;
    if (m_pending.size() == start && m_pending.substr(start - 2) != "22") {
      m_pending.clear();
      throw std::runtime_error("Unsupported AltOS packet length");
    }
    if (byte == '\n' || byte == '\r') {
      m_pending.clear();
      throw std::runtime_error("Truncated AltOS packet");
    }
    if (m_pending.size() == start + 70) {
      Frame frame{AltosFrame, {m_pending.begin(), m_pending.end()}, start};
      m_pending.clear();
      return frame;
    }
  }
  return {};
}
