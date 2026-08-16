#ifndef COSMO_SOFT_SERIALFRAMERWORKER_H
#define COSMO_SOFT_SERIALFRAMERWORKER_H
#include <memory>
#include <thread>
#include "services/interfaces/IFramer.h"
#include "services/interfaces/IBuffer.h"
#include "shared/abstraction/Worker.h"

class SerialFramerWorker final : public Worker{
public:
    explicit SerialFramerWorker(const std::shared_ptr<IFramer>& framer, const std::shared_ptr<IBuffer<Frame>>& buffer){
        m_framer = framer;
        m_buffer = buffer;
    }
    ~SerialFramerWorker() override {
        SerialFramerWorker::stop();
    }

protected:
    void m_process() override;

private:
    std::shared_ptr<IFramer> m_framer;
    std::shared_ptr<IBuffer<Frame>> m_buffer;
};

#endif //COSMO_SOFT_SERIALFRAMERWORKER_H