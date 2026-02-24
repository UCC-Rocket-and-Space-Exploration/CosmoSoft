#ifndef COSMO_SOFT_PARSER_H
#define COSMO_SOFT_PARSER_H
#include <cstdint>
#include <vector>

//TODO turn into producer-consumer pair
class Parser {
public:
    Parser() = default;
    // serial input - > block output
    // handle errors?
    // start - end frame denoters
private:
    std::vector<std::uint8_t> m_buf;
};

#endif //COSMO_SOFT_TELEMETRYPARSER_H
