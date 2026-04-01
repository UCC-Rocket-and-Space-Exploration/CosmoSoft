#include "services/telemetry/ParserWorker.h"
#include "services/BlockingQueue.h"

#include <chrono>

bool ParserWorker::start() {
    if (m_thread.joinable()) {
        return true;
    }
    m_thread = std::jthread([this](const std::stop_token &st) { run(st); });
    return true;
}

void ParserWorker::stop() {
    if (m_thread.joinable()) {
        m_thread.request_stop();
        m_thread = std::jthread{};
    }
}

void ParserWorker::run(const std::stop_token &st) {
    while (!st.stop_requested()) {
        Chunk chunk;
        if (!m_inQueue.pop_for(chunk, std::chrono::milliseconds(50))) {
            continue;
        }

        m_framer.ingest(chunk.data(), chunk.size());

        Frame frame{};
        while (m_framer.try_next_frame(frame)) {
            auto sampleOpt = m_parser.decode(frame);
            if (!sampleOpt) {
                if (m_onError) {
                    m_onError("Failed to decode frame");
                }
                continue;
            }
            if (m_onData) {
                m_onData(std::move(*sampleOpt));
            }
        }
    }
}
