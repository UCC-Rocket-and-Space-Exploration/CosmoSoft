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

void FlightLogManager::setSession(const FlightSession &session) {
    m_session = session;
}

bool FlightLogManager::exportSessionToCsv() const {
    return exportSessionToCsv(m_session);
}

bool FlightLogManager::exportSessionToCsv(const FlightSession &session) const {
    if (m_fileName.empty()) {
        return false;
    }
    std::ofstream out(m_fileName, std::ios::out | std::ios::trunc);
    if (!out) {
        return false;
    }
    out << "time,altitude,temperature,pressure,acceleration_x,acceleration_y,"
           "acceleration_z,latitude,longitude,battery_voltage,rssi,"
           "angular_velocity_x,angular_velocity_y,angular_velocity_z\n";
    for (const auto &s : session.samples) {
        out << s.timestamp
            << ',' << s.altitude
            << ',' << s.temperature
            << ',' << s.pressure
            << ',' << s.acceleration.x
            << ',' << s.acceleration.y
            << ',' << s.acceleration.z
            << ',' << s.coordinates.latitude
            << ',' << s.coordinates.longitude
            << ',' << s.batteryVoltage
            << ',' << s.rssi
            << ',' << s.angularVelocity.x
            << ',' << s.angularVelocity.y
            << ',' << s.angularVelocity.z
            << '\n';
    }
    return static_cast<bool>(out);
}
