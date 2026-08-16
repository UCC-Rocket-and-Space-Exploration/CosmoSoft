#ifndef COSMO_SOFT_CSVFRAMEDECODER_H
#define COSMO_SOFT_CSVFRAMEDECODER_H

#include <string>

#include "services/interfaces/IFrameDecoder.h"

class CsvFrameDecoder : public IFrameDecoder {
public:
    FlightSample decode(const Frame &frame) override;
private:
    static bool isNumber(const std::string & value);

    static void setValueToFlightSampleField(const std::string& value, FlightSample& sample, int field_position);
    const uint8_t m_field_separator = ',';
};
#endif //COSMO_SOFT_CSVFRAMEDECODER_H