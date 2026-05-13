#ifndef COSMO_SOFT_PARSER_H
#define COSMO_SOFT_PARSER_H
#include <cstdint>
#include <vector>

#include "Framer.h"
#include "domain/FlightSample.h"

class Parser {
public:
    Parser() = default;
    FlightSample decode(Frame);

private:
};

#endif //COSMO_SOFT_TELEMETRYPARSER_H