#ifndef COSMO_SOFT_IPARSERWORKER_H
#define COSMO_SOFT_IPARSERWORKER_H
#include "gateway/comms/interfaces/IWorker.h"

class IFramerWorker : public IWorker{
    public:
    ~IFramerWorker() override = default;

    void run() override = 0;
    void stop() override = 0;
};

#endif //COSMO_SOFT_IPARSERWORKER_H