#pragma once

#include <cstdint>

namespace cosmo::mission {
/** @brief Tracks edits and exports without letting stale saves clear newer
 * data. */
class RecordingRevision {
public:
  /** @brief Identity of the recording and revision captured by an export. */
  struct Snapshot {
    std::uint64_t session = 0, revision = 0;
  };
  /** @brief Start a clean recording or an unchanged imported file. */
  void reset() {
    ++m_state.session;
    m_state.revision = m_saved = 0;
  }
  /** @brief Mark one retained sample or annotation edit as unsaved. */
  void changed() { ++m_state.revision; }
  /** @brief Capture the current export revision. */
  [[nodiscard]] Snapshot snapshot() const { return m_state; }
  /** @brief Mark only a successfully written snapshot as saved. */
  void saved(Snapshot snapshot) {
    if (snapshot.session == m_state.session && snapshot.revision > m_saved &&
        snapshot.revision <= m_state.revision)
      m_saved = snapshot.revision;
  }
  /** @brief Whether current data differs from the last successful save. */
  [[nodiscard]] bool dirty() const { return m_state.revision != m_saved; }

private:
  Snapshot m_state;
  std::uint64_t m_saved = 0;
};
} // namespace cosmo::mission
