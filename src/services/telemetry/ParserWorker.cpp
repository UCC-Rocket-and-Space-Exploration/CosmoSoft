// #include "../../../include/services/telemetry/ParserWorker.h"
// #include "../../../include/services/BlockingQueue.h"
//
// bool ParserWorker::start() {
//     if (m_thread.joinable()) return true;
//     m_thread = std::jthread([this](const std::stop_token &st){ run(st); });
//     return true;
// }
//
// void ParserWorker::stop() {
//     if (m_thread.joinable()) {
//         m_thread.request_stop();
//         m_thread = std::jthread{};
//     }
// }
//
// void ParserWorker::run(const std::stop_token &st) {
//     while (!st.stop_requested()) {
//         Chunk chunk;
//         if (!m_inQueue.pop_for(chunk, std::chrono::milliseconds(50))) {
//             continue;
//         }
//
//         m_framer.ingest(chunk.data(), chunk.size());
//
//         Frame frame{};
//         while (m_framer.try_next_frame(frame)) {
//             auto sampleOpt = m_parser.decode(frame);
//             if (!sampleOpt) {
//                 if (m_onError) m_onError("Failed to decode frame");
//                 continue;
//             }
//             if (m_onData) m_onData(std::move(*sampleOpt));
//         }
//     }
// }

#include "services/telemetry/ParserWorker.h"
#include <thread>

ParserWorker::ParserWorker(
        const Buffer& buffer,
        void (*on_parsed_data_callback)(const FlightSample& flight_sample), FrameFormat frame_format) {
    m_on_parsed_data_callback = on_parsed_data_callback;
    m_buffer = buffer;
    m_decoder = FrameDecoderFactory::create(frame_format);
}

// void ParserWorker::start() {
//
//     // 13603,19.24,100896.15,165.59,0.704,-0.927,9.850,-0.047,0.091,0.091,-4.822,-2.985,16.506,0.000000,0.000000,0.00
//     m_worker_thread = std::thread(&ParserWorker::parse_flight_samples, this);
// }

// void ParserWorker::stop() {
//
// }

void ParserWorker::m_process() {
    while (m_running) {
        std::optional<Frame> frame = m_buffer->get();
        if (frame.has_value()) {
            FlightSample f_sample = m_decoder->decode(frame.value());
            std::cout << "decoded frame: " << f_sample.timestamp << std::endl;
            if (m_first_frame) {
                m_initial_timestamp = f_sample.timestamp;
                m_first_frame = false;
            }
            f_sample.timestamp -= m_initial_timestamp;
            m_on_parsed_data_callback(f_sample);
        }
    }
}
