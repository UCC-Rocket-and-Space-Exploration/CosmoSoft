#include "services/telemetry/Parser.h"

std::optional<FlightSample> Parser::decode(const Frame &) const {
  return std::nullopt;
}
