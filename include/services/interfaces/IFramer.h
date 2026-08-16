#ifndef COSMO_SOFT_IFRAMER_H
#define COSMO_SOFT_IFRAMER_H
#include "domain/Frame.h"

class IFramer {
    public:
    virtual ~IFramer() = default;
    virtual Frame get_frame(const bool& running) = 0;
};
#endif //COSMO_SOFT_IFRAMER_H