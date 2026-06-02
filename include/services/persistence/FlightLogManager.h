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

    /** @brief Replaces the internal session with an externally loaded one. */
    void setSession(const FlightSession &session);

    [[nodiscard]] FlightSession &session() { return m_session; }
    [[nodiscard]] const FlightSession &session() const { return m_session; }

    /** @brief Exports session samples as CSV with a full header row. */
    [[nodiscard]] bool exportSessionToCsv() const;

    /** @brief Exports @p session as CSV with a full header row to outputPath(). */
    [[nodiscard]] bool exportSessionToCsv(const FlightSession &session) const;

private:
    FlightSession m_session;
    std::string m_fileName;
};

#endif
