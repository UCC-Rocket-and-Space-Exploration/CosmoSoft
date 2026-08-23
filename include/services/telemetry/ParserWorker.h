#ifndef COSMO_SOFT_IPARSERWORKER_H
#define COSMO_SOFT_IPARSERWORKER_H
#include <memory>
#include <thread>

#include "FrameDecoderVault.h"
#include "shared/abstraction/Worker.h"
#include "domain/FlightSample.h"
#include "services/RingBuffer.h"
#include "services/interfaces/ILogger.h"

#define Buffer std::shared_ptr<IBuffer<Frame>>

class ParserWorker : public Worker{
    public:
    explicit ParserWorker(
            const Buffer& buffer,
            void (*on_parsed_data_callback)(const FlightSample& flight_sample),
            const std::shared_ptr<ILogger>& debug_logger) {
        m_on_parsed_data_callback = on_parsed_data_callback;
        m_buffer = buffer;
        m_decoder_vault = FrameDecoderVault();
        m_debug_logger = debug_logger;
    }
    ~ParserWorker() override = default;
protected:
    void m_process() override;

private:
    FrameDecoderVault m_decoder_vault;
    void (*m_on_parsed_data_callback)(const FlightSample& flight_sample);
    Buffer m_buffer;
    std::shared_ptr<ILogger> m_debug_logger;
    std::atomic_bool m_first_frame {true};
    int m_initial_timestamp = 0;
};

#endif //COSMO_SOFT_IPARSERWORKER_H