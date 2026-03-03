#ifndef COSMO_SOFT_PARSER_H
#define COSMO_SOFT_PARSER_H
#include <cstdint>
#include <vector>

class Parser {
public:
<<<<<<< HEAD
    Parser() = default;
    // serial input - > block output
    // handle errors?
    // start - end frame denoters
=======
    Parser();
    //needed methods:
    // - convert frame to sample
    // -

>>>>>>> e7d53e9 (branching off from refactor to work on parsing thread)
private:
    //std::vector<std::uint8_t> m_buf; //considering whether parser needs its own buffer; could instead have it just input a single frame and output a single sample
};

#endif //COSMO_SOFT_TELEMETRYPARSER_H
