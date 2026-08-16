#ifndef COSMO_SOFT_GPSFRAMEDECODER_H
#define COSMO_SOFT_GPSFRAMEDECODER_H
#include "AltosFrameDecoder.h"
#include "services/interfaces/IFrameDecoder.h"

class GpsFrameDecoder : public AltosFrameDecoder{
public:
    FlightSample decode(const Frame &frame) override;
};

#endif //COSMO_SOFT_GPSFRAMEDECODER_H