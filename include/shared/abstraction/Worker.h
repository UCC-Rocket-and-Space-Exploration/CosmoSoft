#ifndef COSMO_SOFT_IWORKER_H
#define COSMO_SOFT_IWORKER_H
#include <thread>

class Worker {
    public:
    virtual ~Worker() = default;

    virtual void start();
    virtual void stop();
    virtual void m_process() = 0;

protected:
    std::thread m_worker_thread;
    std::atomic<bool> m_running{false};
};

#endif //COSMO_SOFT_IWORKER_H