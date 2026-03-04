#ifndef COSMO_SOFT_SERIALWORKER_H
#define COSMO_SOFT_SERIALWORKER_H
#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "../../domain/FlightSample.h"
#include "../../gateway/IComms.h"
#include "../../gateway/ISerialPortScanner.h"

//To avoid blocking the UI when fetching serial data, we need to use Qt's signals and slots technique for async data fetching
//TODO complete refactor; make async if possible, include error handling & logging, parsing, etc.
// - Also rewrite to use std::jthread and std::stop_token for better thread management and cancellation
class SerialWorker {
    using DataCallback = std::function<void(std::vector<uint8_t>)>;
    using ErrorCallback = std::function<void(const std::string&)>;

public:

    explicit SerialWorker(IComms* comms, DataCallback onData, ErrorCallback onError) : m_connectedPort(comms), m_onData(std::move(onData)), m_onError(std::move(onError)), m_running(false) {};
    ~SerialWorker();

    bool start();
    void stop();

private:
    void run() const;

    std::thread m_workerThread;
    std::atomic<bool> m_running;

    IComms* m_connectedPort;
    ISerialPortScanner* m_scanner;
    DataCallback m_onData;
    ErrorCallback m_onError;
};


#endif //COSMO_SOFT_SERIALWORKER_H
