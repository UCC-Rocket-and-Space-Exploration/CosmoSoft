#include "services/telemetry/ParserWorker.h"
#include <thread>
#include "shared/exceptions/IncorrectAltosPacketType.h"




void ParserWorker::m_process() {
    while (m_running) {
        std::optional<Frame> frame = m_buffer->get();

        if (frame.has_value()) {
            std::shared_ptr<IFrameDecoder> decoder;
            try {
                decoder = m_decoder_vault.select(frame.value());
            }
            catch (std::exception& e) {
                m_debug_logger->LogLine(e.what());
                continue;
            }
            FlightSample f_sample = decoder->decode(frame.value());

            if (m_first_frame) {
                m_initial_timestamp = f_sample.timestamp;
                m_first_frame = false;
            }
            f_sample.timestamp -= m_initial_timestamp;
            m_on_parsed_data_callback(f_sample);
        }
    }
}
