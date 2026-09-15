#include "services/telemetry/RecordedComms.h"
#include <algorithm>
#include <filesystem>

RecordedComms::RecordedComms(std::string path,
                             std::chrono::milliseconds cadence)
    : m_path(std::move(path)),
      m_cadence(std::max(cadence, std::chrono::milliseconds(0))) {}
RecordedComms::~RecordedComms() { close(); }
bool RecordedComms::open() {
  std::lock_guard lock(m_mutex);
  if (m_input.is_open())
    m_input.close();
  m_input.clear();
  m_open = false;
  const std::filesystem::path path(std::u8string(m_path.begin(), m_path.end()));
  std::error_code error;
  if (!std::filesystem::is_regular_file(path, error))
    return false;
  m_input.open(path, std::ios::binary);
  m_open = m_input.is_open();
  m_nextLine = std::chrono::steady_clock::now();
  return m_open;
}
void RecordedComms::close() {
  std::lock_guard lock(m_mutex);
  m_open = false;
  m_input.close();
  m_changed.notify_all();
}
bool RecordedComms::isOpen() const {
  std::lock_guard lock(m_mutex);
  return m_open;
}
ssize_t RecordedComms::read(uint8_t *buffer, size_t size) {
  std::unique_lock lock(m_mutex);
  if (!buffer || size == 0)
    return 0;
  m_changed.wait_until(lock, m_nextLine, [this] { return !m_open; });
  if (!m_open)
    return 0;
  size_t count = 0;
  // Bound work under the lock, including adversarial files without newlines.
  while (count < std::min(size, size_t{512})) {
    char byte = 0;
    if (!m_input.get(byte)) {
      m_open = false;
      if (m_input.bad())
        return -1;
      break;
    }
    buffer[count++] = static_cast<uint8_t>(byte);
    if (byte == '\n') {
      m_nextLine = std::chrono::steady_clock::now() + m_cadence;
      break;
    }
  }
  return static_cast<ssize_t>(count);
}
