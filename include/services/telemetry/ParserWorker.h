#ifndef COSMO_SOFT_PARSERWORKER_H
#define COSMO_SOFT_PARSERWORKER_H
#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "../../domain/FlightSample.h"

class ParserWorker {
public:
    using DataCallback = std::function<void(std::vector<FlightSample>)>;
    using ErrorCallback = std::function<void(const std::string&)>;

    explicit ParserWorker(DataCallback dataCallback, ErrorCallback errorCallback);
    ~ParserWorker();

    bool start();
    void stop();

    void setDataCallback(DataCallback callback);
    void setErrorCallback(ErrorCallback callback);

private:
    void run();

    std::thread m_workerThread;
    std::atomic<bool> m_running;
    DataCallback m_onData;
    ErrorCallback m_onError;
};


#endif //COSMO_SOFT_PARSERWORKER_H
