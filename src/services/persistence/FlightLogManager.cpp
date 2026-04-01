#include "services/persistence/FlightLogManager.h"

#include <fstream>

void FlightLogManager::clear() {
    m_session.samples.clear();
}

void FlightLogManager::setOutputPath(std::string path) {
    m_fileName = std::move(path);
}

void FlightLogManager::appendSample(const FlightSample &sample) {
    m_session.samples.push_back(sample);
}

bool FlightLogManager::exportSessionToTextFile() const {
    if (m_fileName.empty()) {
        return false;
    }
    std::ofstream out(m_fileName, std::ios::out | std::ios::trunc);
    if (!out) {
        return false;
    }
    for (const auto &s : m_session.samples) {
        out << s.timestamp << ' ' << s.altitude << ' ' << s.temperature << ' ' << s.pressure << '\n';
    }
    return static_cast<bool>(out);
}
