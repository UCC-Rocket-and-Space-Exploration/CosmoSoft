#ifndef COSMO_SOFT_SERIALWORKER_H
#define COSMO_SOFT_SERIALWORKER_H

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "gateway/comms/IComms.h"

class SerialWorker {
    using DataCallback = std::function<void(std::vector<uint8_t>)>;
    using ErrorCallback = std::function<void(const std::string &)>;

public:
    explicit SerialWorker(IComms *comms, DataCallback onData, ErrorCallback onError)
        : m_connectedPort(comms),
          m_onData(std::move(onData)),
          m_onError(std::move(onError)),
          m_running(false) {}

    ~SerialWorker();

    bool start();
    void stop();

private:
    void run();

    std::thread m_workerThread;
    std::atomic<bool> m_running;

    IComms *m_connectedPort = nullptr;
    DataCallback m_onData;
    ErrorCallback m_onError;
};

#endif // COSMO_SOFT_SERIALWORKER_H
