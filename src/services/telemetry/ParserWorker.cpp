#include "services/telemetry/ParserWorker.h"
#include <thread>
#include "shared/exceptions/IncorrectAltosPacketType.h"

void ParserWorker::m_process() {
    while (m_running) {
        std::optional<Frame> frame{};
        try {
            frame = m_buffer->get();
        }
        catch (std::exception& e) {
            std::string message = "Got an exception while getting the frame from buffer: ";
            message += e.what();
            m_debug_logger->LogLine(message);
        }

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
            try {
                m_on_parsed_data_callback(f_sample);
            }
            catch (std::exception& e){
                this->m_debug_logger->LogLine("Error on calling m_on_parsed_data_callback function: ");
                this->m_debug_logger->LogLine(e.what());
            }
        }
    }
}
