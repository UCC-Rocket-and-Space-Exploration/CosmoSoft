#ifndef COSMO_SOFT_PARSER_H
#define COSMO_SOFT_PARSER_H
#include <cstdint>
#include <vector>

#include "Framer.h"
#include "domain/FlightSample.h"

class Parser {
public:
<<<<<<< HEAD
    Parser() = default;
    // serial input - > block output
    // handle errors?
    // start - end frame denoters
=======
    Parser();

    FlightSample decode(Frame frame);

>>>>>>> e7d53e9 (branching off from refactor to work on parsing thread)
private:
};

#endif //COSMO_SOFT_TELEMETRYPARSER_H
