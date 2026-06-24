#ifndef COSMO_SOFT_IPARSERWORKER_H
#define COSMO_SOFT_IPARSERWORKER_H
#include <memory>
#include <thread>

#include "FrameDecoderFactory.h"
#include "shared/abstraction/Worker.h"
#include "domain/FlightSample.h"
#include "services/RingBuffer.h"

#define Buffer std::shared_ptr<IBuffer<Frame>>

class ParserWorker : public Worker{
    public:
    explicit ParserWorker(
        const Buffer& buffer,
        void (*on_parsed_data_callback)(const FlightSample& flight_sample), FrameFormat frame_format);
    ~ParserWorker() override = default;
protected:
    void m_process() override;

private:
    void (*m_on_parsed_data_callback)(const FlightSample& flight_sample);
    Buffer m_buffer;
    std::unique_ptr<IFrameDecoder> m_decoder;
    std::atomic_bool m_first_frame {true};
    int m_initial_timestamp = 0;
};

#endif //COSMO_SOFT_IPARSERWORKER_H