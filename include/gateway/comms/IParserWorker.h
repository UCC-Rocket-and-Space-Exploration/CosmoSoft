#ifndef COSMO_SOFT_IPARSERWORKER_H
#define COSMO_SOFT_IPARSERWORKER_H
#include "IWorker.h"

class IFramerWorker : public IWorker{
    public:
    virtual ~IFramerWorker() = default;

    void run() = 0;
    void stop() = 0;
};

#endif //COSMO_SOFT_IPARSERWORKER_H