
#ifndef COSMO_SOFT_IFRAMERWORKER_H
#define COSMO_SOFT_IFRAMERWORKER_H
#include "../../gateway/comms/interfaces/IWorker.h"

class IFramerWorker : public IWorker{
public:
    virtual ~IFramerWorker() = default; //reserch why need this and what happens when implemented instance calls destructor

};
#endif //COSMO_SOFT_IFRAMERWORKER_H