#ifndef COSMO_SOFT_SERIALFRAMERWORKER_H
#define COSMO_SOFT_SERIALFRAMERWORKER_H
#include <gateway/comms/IFramerWorker.h>

class SerialFramerWorker : public IFramerWorker{
public:
    SerialFramerWorker(IFramer *framer){

    }
    void run() override;
    void stop() override;
};

#endif //COSMO_SOFT_SERIALFRAMERWORKER_H