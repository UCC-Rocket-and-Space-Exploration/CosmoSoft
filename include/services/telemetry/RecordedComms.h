#ifndef COSMO_SOFT_RECORDEDCOMMS_H
#define COSMO_SOFT_RECORDEDCOMMS_H
#include "gateway/comms/interfaces/IComms.h"
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <mutex>

/** @brief Read-only recorded bytes, paced once per line with interruptible
 * waits. */
class RecordedComms final : public IComms {
public:
  /** @brief Open path on start; cadence defaults to 250 ms, independent of
   * packet time. */
  explicit RecordedComms(std::string path, std::chrono::milliseconds cadence =
                                               std::chrono::milliseconds(250));
  /** @brief Close and release the input. */
  ~RecordedComms() override;
  /** @brief Rewind the recording and begin a fresh session. */
  bool open() override;
  /** @brief Cancel pacing and close the recording. */
  void close() override;
  /** @brief Whether more input can be read. */
  bool isOpen() const override;
  /** @brief Reject every write: recordings are read-only. */
  ssize_t write(const uint8_t *, size_t) override { return -1; }
  /** @brief Read bounded fragments, ending each read at a newline or EOF. */
  ssize_t read(uint8_t *buffer, size_t size) override;
  /** @brief Return the recording path. */
  std::string getDeviceName() const override { return m_path; }

private:
  std::string m_path;
  std::chrono::milliseconds m_cadence;
  mutable std::mutex m_mutex;
  std::condition_variable m_changed;
  std::ifstream m_input;
  bool m_open = false;
  std::chrono::steady_clock::time_point m_nextLine;
};
#endif
