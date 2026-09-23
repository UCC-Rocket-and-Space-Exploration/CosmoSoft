
#include <thread>
#include "gateway/comms/SerialFramerWorker.h"
#include "gateway/comms/Windows/SerialCommsWindows.h"
#include "shared/exceptions/SerialTimeout.h"

void SerialFramerWorker::m_process() {
    std::string error_start = "Error: ";
    try {
        while (m_running) {
            Frame frame;
            try {
                frame = m_framer->get_frame(m_running);
            }
            catch (const std::exception& ex) {
                m_debug_logger->LogLine(error_start + ex.what());
                continue;
            }
            if (!frame.data.empty()) {
                unsigned char *str(frame.data.data());
                m_results_logger->LogLine(str);
                try {
                    m_buffer->put(frame);
                }
                catch (const std::exception& ex) {
                    // std::string message = "Got exception when putting into the buffer: ";
                    // message += ex.what();
                    // m_debug_logger->LogLine(message);
                }
            }
        }
    }
    catch(const std::exception& e)
    {
        m_debug_logger->LogLine(error_start + e.what());
    }
}
