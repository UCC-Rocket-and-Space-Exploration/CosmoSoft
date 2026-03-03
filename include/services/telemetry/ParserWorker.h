#ifndef COSMO_SOFT_PARSERWORKER_H
#define COSMO_SOFT_PARSERWORKER_H
#include <atomic>
#include <functional>
#include <queue>
#include <string>
#include <thread>
#include <stop_token>

#include "Parser.h"
#include "Framer.h"
#include "../../domain/FlightSample.h"

class ParserWorker {
public:
    using DataCallback = std::function<void(FlightSample&&)>;
    using ErrorCallback = std::function<void(std::string_view)>;

    explicit ParserWorker(std::queue<uint8_t> inQueue, DataCallback dataCallback, ErrorCallback errorCallback) : m_queue(std::move(inQueue)), m_onData(std::move(dataCallback)), m_onError(std::move(errorCallback)), m_running(false) {};

    bool start(std::stop_token st);
    void stop();

private:
    void run();

    std::queue<uint8_t> m_queue;
    Framer m_framer;
    Parser m_parser;

    std::thread m_workerThread;
    std::atomic<bool> m_running;

    DataCallback m_onData;
    ErrorCallback m_onError;
};


#endif //COSMO_SOFT_PARSERWORKER_H



