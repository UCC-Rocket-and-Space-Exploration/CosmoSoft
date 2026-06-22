#ifndef COSMO_SOFT_SERIALFRAMERWORKER_H
#define COSMO_SOFT_SERIALFRAMERWORKER_H
#include <memory>
#include <thread>

#include "../../services/interfaces/IFramer.h"
#include "interfaces/IFramerWorker.h"
#include "../../services/interfaces/IBuffer.h"

class SerialFramerWorker final : public IFramerWorker{
public:
    explicit SerialFramerWorker(const std::shared_ptr<IFramer>& framer, const std::shared_ptr<IBuffer<Frame>>& buffer){
        m_framer = framer;
        m_buffer = buffer;
    }
    ~SerialFramerWorker() override {
        // std::cout << "from worker dest" << std::endl;
        SerialFramerWorker::stop();
    }
    void run() override;
    void stop() override;

private:
    void write_to_buffer_from_serial() const;
    std::shared_ptr<IFramer> m_framer;
    std::shared_ptr<IBuffer<Frame>> m_buffer;
    std::atomic<bool> m_running{false};
    std::thread m_thread;
};

#endif //COSMO_SOFT_SERIALFRAMERWORKER_H