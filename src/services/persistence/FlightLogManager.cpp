#include "services/persistence/FlightLogManager.h"

#include <QFileDevice>
#include <QLocale>
#include <QSaveFile>
#include <QString>
#include <QTextStream>

#include <cmath>
#include <utility>

namespace {

void writeCsvSample(QTextStream &output, const FlightSample &sample) {
  output << sample.timestamp;
  for (const double value :
       {sample.altitude, sample.temperature, sample.pressure,
        sample.acceleration.x, sample.acceleration.y, sample.acceleration.z,
        sample.coordinates.latitude, sample.coordinates.longitude,
        sample.batteryVoltage, sample.rssi, sample.angularVelocity.x,
        sample.angularVelocity.y, sample.angularVelocity.z}) {
    output << u',';
    if (std::isfinite(value))
      output << value;
  }
  output << u'\n';
}

} // namespace

void FlightLogManager::clear() {
    m_session = FlightSession{};
}

void FlightLogManager::setOutputPath(std::string path) {
    m_fileName = std::move(path);
}

bool FlightLogManager::appendSample(const FlightSample &sample) {
    if (m_session.samples.size() >= m_maxSamples) {
        return false;
    }
    m_session.samples.push_back(sample);
    return true;
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

    QSaveFile outputFile(QString::fromStdString(m_fileName));
    // Never fall back to direct writes: doing so could truncate an existing
    // destination before a later serialization or device error is detected.
    outputFile.setDirectWriteFallback(false);
    if (!outputFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    {
        QTextStream output(&outputFile);
        output.setLocale(QLocale::c());
        output.setRealNumberPrecision(17);
        output << QStringLiteral(
            "time_ms,altitude,temperature,pressure,acceleration_x,acceleration_"
            "y,"
            "acceleration_z,latitude,longitude,battery_voltage,rssi,"
            "angular_velocity_x,angular_velocity_y,angular_velocity_z\n");
        for (const auto &sample : session.samples) {
            writeCsvSample(output, sample);
        }
        output.flush();
        if (output.status() != QTextStream::Ok
            || outputFile.error() != QFileDevice::NoError) {
            outputFile.cancelWriting();
            return false;
        }
    }

    return outputFile.commit();
}
