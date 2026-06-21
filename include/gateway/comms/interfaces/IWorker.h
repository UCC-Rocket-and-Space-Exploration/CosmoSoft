#ifndef COSMO_SOFT_IWORKER_H
#define COSMO_SOFT_IWORKER_H

class IWorker {
    public:
    virtual ~IWorker() = default;

    virtual void run() = 0;
    virtual void stop() = 0;
};

#endif //COSMO_SOFT_IWORKER_H