#include "services/telemetry/ParserWorker.h"
#include <chrono>
#include <limits>
#include <stdexcept>

void ParserWorker::m_process() {
  bool first = true;
  long previous = 0;
  long elapsed = 0;
  std::optional<uint16_t> serial;
  while (true) {
    auto frame = m_buffer->get();
    if (!frame) {
      if (!m_running)
        break;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      continue;
    }
    try {
      auto sample = m_vault.select(*frame)->decode(*frame);
      if (frame->format == AltosFrame) {
        const auto offset = frame->packet_start_index;
        if (ByteHelper::get_byte_from_str(
                std::string(frame->data.begin() + offset + 8,
                            frame->data.begin() + offset + 10)) != 0x08)
          throw std::invalid_argument("Unsupported recorded packet type");
        const std::string text(frame->data.begin() + offset,
                               frame->data.begin() + offset + 4);
        const auto device = static_cast<uint16_t>(
            ByteHelper::get_byte_from_str(text.substr(0, 2)) |
            (ByteHelper::get_byte_from_str(text.substr(2, 2)) << 8));
        if (serial && *serial != device)
          throw std::runtime_error("Different AltOS device in active session");
        serial = device;
      }
      const long timestamp = sample.timestamp;
      long delta = first ? 0 : timestamp - previous;
      if (!first && frame->format == AltosFrame) {
        constexpr long period = 655360; // uint16 counter, 100 ticks/second.
        if (delta < 0)
          delta += period;
        if (delta >= period / 2)
          throw std::runtime_error("Out-of-order or ambiguous AltOS timestamp");
      }
      if (delta < 0 || elapsed > std::numeric_limits<long>::max() - delta)
        throw std::runtime_error("Invalid telemetry timestamp");
      elapsed += delta;
      previous = timestamp;
      first = false;
      sample.timestamp = elapsed;
      if (m_callback)
        m_callback(sample);
    } catch (const std::exception &error) {
      if (m_logger)
        m_logger->LogLine(std::string(error.what()));
    }
  }
}
