#ifndef COSMO_SOFT_PARSER_H
#define COSMO_SOFT_PARSER_H

#include <optional>

#include "domain/FlightSample.h"
#include "domain/Frame.h"

/**
 * @brief Compatibility decoder retained for legacy .telem imports.
 *
 * It returns no sample until a legacy telemetry decoder is implemented.
 */
class Parser {
public:
  /** @brief Decode @p frame into a flight sample when supported. */
  [[nodiscard]] std::optional<FlightSample> decode(const Frame &frame) const;
};

#endif // COSMO_SOFT_PARSER_H
