/**
 * @file FlightLogManager.h
 * @brief In-memory flight-session recording and transactional CSV export.
 */

#ifndef COSMO_SOFT_FLIGHTLOGMANAGER_H
#define COSMO_SOFT_FLIGHTLOGMANAGER_H

#include "domain/FlightSample.h"
#include "domain/FlightSession.h"

#include <string>

/**
 * @class FlightLogManager
 * @brief Stores flight samples and exports stable snapshots as CSV files.
 *
 * Export writes through a temporary file in the destination directory and
 * replaces the destination only after every row has been written successfully.
 */
class FlightLogManager {
public:
    /** @brief Construct an empty manager with no output path. */
    FlightLogManager() = default;

    /** @brief Remove all samples from the current in-memory session. */
    void clear();

    /**
     * @brief Set the destination used by subsequent CSV exports.
     * @param path Native path encoded as UTF-8.
     */
    void setOutputPath(std::string path);

    /** @brief Return the configured CSV export destination. @return UTF-8 path. */
    [[nodiscard]] const std::string &outputPath() const { return m_fileName; }

    /**
     * @brief Append one sample to the current in-memory session.
     * @param sample Sample copied into the session.
     */
    void appendSample(const FlightSample &sample);

    /**
     * @brief Replace the internal session with an externally loaded one.
     * @param session Session copied into this manager.
     */
    void setSession(const FlightSession &session);

    /** @brief Return mutable access to the current session. @return Stored session. */
    [[nodiscard]] FlightSession &session() { return m_session; }

    /** @brief Return read-only access to the current session. @return Stored session. */
    [[nodiscard]] const FlightSession &session() const { return m_session; }

    /**
     * @brief Transactionally export the current session with a full CSV header.
     * @return true after the temporary file is committed; false on any failure.
     *
     * A false result leaves an existing destination file unchanged.
     */
    [[nodiscard]] bool exportSessionToCsv() const;

    /**
     * @brief Transactionally export @p session to outputPath() as CSV.
     * @param session Stable session snapshot to serialize.
     * @return true after the temporary file is committed; false on any failure.
     *
     * A false result leaves an existing destination file unchanged.
     */
    [[nodiscard]] bool exportSessionToCsv(const FlightSession &session) const;

private:
    FlightSession m_session;
    std::string m_fileName;
};

#endif // COSMO_SOFT_FLIGHTLOGMANAGER_H
