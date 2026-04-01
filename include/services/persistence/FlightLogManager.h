#ifndef COSMO_SOFT_FLIGHTLOGMANAGER_H
#define COSMO_SOFT_FLIGHTLOGMANAGER_H

#include <string>

#include "domain/FlightSample.h"
#include "domain/FlightSession.h"

class FlightLogManager {
public:
    FlightLogManager() = default;

    void clear();
    void setOutputPath(std::string path);
    [[nodiscard]] const std::string &outputPath() const { return m_fileName; }

    void appendSample(const FlightSample &sample);

    [[nodiscard]] FlightSession &session() { return m_session; }
    [[nodiscard]] const FlightSession &session() const { return m_session; }

    /** Writes samples as simple text lines when outputPath() is non-empty. */
    [[nodiscard]] bool exportSessionToTextFile() const;

private:
    FlightSession m_session;
    std::string m_fileName;
};

#endif
