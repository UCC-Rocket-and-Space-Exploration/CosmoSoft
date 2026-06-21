#ifndef COSMO_SOFT_SERIALFRAMERWORKER_H
#define COSMO_SOFT_SERIALFRAMERWORKER_H
#include <memory>
#include <thread>

#include "../../services/interfaces/IFramer.h"
#include "../../services/telemetry/IFramerWorker.h"
#include "services/IBuffer.h"

class SerialFramerWorker final : public IFramerWorker{
public:
    explicit SerialFramerWorker(std::unique_ptr<IFramer> framer, IBuffer<Frame>* buffer){
        m_framer = std::move(framer);
        m_buffer = buffer;
    }
    void run() override;
    void stop() override;

private:
    void write_to_buffer_from_serial() const;
    std::unique_ptr<IFramer> m_framer;
    IBuffer<Frame>* m_buffer;
    bool m_running = false;
    std::thread m_thread;
};

#endif //COSMO_SOFT_SERIALFRAMERWORKER_H