#ifndef COSMO_SOFT_PARSER_H
#define COSMO_SOFT_PARSER_H

#include <cstdint>
#include <optional>

#include "Framer.h"
#include "domain/FlightSample.h"

class Parser {
public:
    Parser() = default;

    std::optional<FlightSample> decode(const Frame &frame);

private:
};

#endif // COSMO_SOFT_PARSER_H
