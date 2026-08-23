
#include <thread>
#include "gateway/comms/SerialFramerWorker.h"
#include "gateway/comms/windows/SerialCommsWindows.h"
#include "shared/exceptions/SerialTimeout.h"

std::string error_start = "Error: ";
void SerialFramerWorker::m_process() {
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
                m_buffer->put(frame);
            }
        }
    }
    catch(const std::exception& e)
    {
        m_debug_logger->LogLine(error_start + e.what());
    }
}
