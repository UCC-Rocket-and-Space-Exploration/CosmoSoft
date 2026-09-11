#ifndef COSMO_SOFT_FRAMER_H
#define COSMO_SOFT_FRAMER_H

#include <cstddef>
#include <cstdint>

#include "domain/Frame.h"

/**
 * @brief Compatibility frame extractor retained for legacy .telem imports.
 *
 * The streaming telemetry pipeline uses IFramer implementations. This adapter
 * preserves the established import API while legacy frame decoding is absent.
 */
class Framer {
public:
  /** @brief Accept input bytes for future frame extraction. */
  void ingest(const uint8_t *data, std::size_t size);

  /** @brief Return the next extracted frame, or nullptr when none is available.
   */
  Frame *readData(uint8_t *data, std::size_t size);

  /** @brief Report whether an extracted frame is available. */
  [[nodiscard]] bool readableData() const;

  /** @brief Write the next extracted frame to @p out when one is available. */
  bool try_next_frame(Frame &out);
};

#endif // COSMO_SOFT_FRAMER_H
