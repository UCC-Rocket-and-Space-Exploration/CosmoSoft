#ifndef COSMO_SOFT_PARSER_H
#define COSMO_SOFT_PARSER_H
#include <cstdint>
#include <vector>

class Parser {
public:
    Parser();
    //needed methods:
    // - convert frame to sample
    // -

private:
    //std::vector<std::uint8_t> m_buf; //considering whether parser needs its own buffer; could instead have it just input a single frame and output a single sample
};

#endif //COSMO_SOFT_TELEMETRYPARSER_H