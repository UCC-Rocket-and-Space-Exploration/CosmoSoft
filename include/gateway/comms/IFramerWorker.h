
#ifndef COSMO_SOFT_IFRAMERWORKER_H
#define COSMO_SOFT_IFRAMERWORKER_H
class IFramerWorker {
public:
    virtual void run() = 0;
    virtual void stop() = 0;
}
#endif //COSMO_SOFT_IFRAMERWORKER_H