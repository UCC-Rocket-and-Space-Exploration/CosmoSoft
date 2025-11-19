#ifndef COSMO_SOFT_SERIALWORKER_H
#define COSMO_SOFT_SERIALWORKER_H
#include <functional>
#include <thread>
#include "IComms.h"
#include "ISerialPortScanner.h"

//To avoid blocking the UI when fetching serial data, we need to use Qt's signals and slots technique for async data fetching
class SerialWorker {
public:
    explicit SerialWorker(IComms* comms);
    ~SerialWorker();

    using DataCallback = std::function<void(const std::vector<uint8_t>&)>;
    using ErrorCallback = std::function<void(const std::string&)>;

    bool start();
    void stop();

    void setDataCallback(DataCallback callback);
    void setErrorCallback(ErrorCallback callback);

private:
    void run();

    std::thread m_workerThread;
    std::atomic<bool> m_running;
    IComms* m_connectedPort;
    ISerialPortScanner* m_scanner;
    DataCallback m_onData;
    ErrorCallback m_onError;
};


#endif //COSMO_SOFT_SERIALWORKER_H