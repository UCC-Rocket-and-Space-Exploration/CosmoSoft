#ifndef COSMO_SOFT_GPSFRAMEDECODER_H
#define COSMO_SOFT_GPSFRAMEDECODER_H
#include "services/interfaces/IFrameDecoder.h"

class GpsFrameDecoder : public IFrameDecoder{
public:
    FlightSample decode(Frame frame) override;
};

#endif //COSMO_SOFT_GPSFRAMEDECODER_H