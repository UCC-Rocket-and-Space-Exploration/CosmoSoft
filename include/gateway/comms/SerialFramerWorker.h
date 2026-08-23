#ifndef COSMO_SOFT_SERIALFRAMERWORKER_H
#define COSMO_SOFT_SERIALFRAMERWORKER_H
#include <memory>
#include <thread>
#include "services/interfaces/IFramer.h"
#include "services/interfaces/IBuffer.h"
#include "services/interfaces/ILogger.h"
#include "shared/abstraction/Worker.h"

class SerialFramerWorker final : public Worker{
public:
    explicit SerialFramerWorker(
        const std::shared_ptr<IFramer>& framer,
        const std::shared_ptr<IBuffer<Frame>>& buffer,
        const std::shared_ptr<ILogger>& results_logger,
        const std::shared_ptr<ILogger>& debug_logger){
        m_framer = framer;
        m_buffer = buffer;
        m_results_logger = results_logger;
        m_debug_logger = debug_logger;
    }
    ~SerialFramerWorker() override {
        SerialFramerWorker::stop();
    }

protected:
    void m_process() override;

private:
    std::shared_ptr<IFramer> m_framer;
    std::shared_ptr<IBuffer<Frame>> m_buffer;
    std::shared_ptr<ILogger> m_results_logger;
    std::shared_ptr<ILogger> m_debug_logger;
};

#endif //COSMO_SOFT_SERIALFRAMERWORKER_H