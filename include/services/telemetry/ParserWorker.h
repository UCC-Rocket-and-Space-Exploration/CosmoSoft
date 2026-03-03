#ifndef COSMO_SOFT_PARSERWORKER_H
#define COSMO_SOFT_PARSERWORKER_H
#include <atomic>
#include <functional>
#include <queue>
#include <string>
#include <thread>
<<<<<<< HEAD
#include <vector>
=======
#include <stop_token>
>>>>>>> e7d53e9 (branching off from refactor to work on parsing thread)

#include "Parser.h"
#include "Framer.h"
#include "../../domain/FlightSample.h"

template<typename T>
class BlockingQueue;

class ParserWorker {
public:
    using Chunk = std::vector<uint8_t>;
    using DataCallback  = std::function<void(FlightSample&&)>;
    using ErrorCallback = std::function<void(std::string_view)>;

    ParserWorker(BlockingQueue<Chunk>& inQueue,
                 DataCallback onData,
                 ErrorCallback onError)
        : m_inQueue(inQueue),
          m_onData(std::move(onData)),
          m_onError(std::move(onError)) {}

    bool start();
    void stop();

private:
    void run(const std::stop_token &st);

    BlockingQueue<Chunk>& m_inQueue;
    Framer m_framer;
    Parser m_parser;

    std::jthread m_thread;

    DataCallback m_onData;
    ErrorCallback m_onError;
};




#endif //COSMO_SOFT_PARSERWORKER_H
<<<<<<< HEAD
=======



>>>>>>> e7d53e9 (branching off from refactor to work on parsing thread)
