/**
 * @file FlightLogManager.h
 * @brief In-memory flight-session recording and transactional CSV export.
 */

#ifndef COSMO_SOFT_FLIGHTLOGMANAGER_H
#define COSMO_SOFT_FLIGHTLOGMANAGER_H

#include "domain/FlightSample.h"
#include "domain/FlightSession.h"

#include <cstddef>
#include <string>

/**
 * @class FlightLogManager
 * @brief Stores flight samples and exports stable snapshots as CSV files.
 *
 * Samples appended during live recording are capped by the configured
 * in-memory limit. An externally supplied session may be larger.
 *
 * Export writes through a temporary file in the destination directory and
 * replaces the destination only after every row has been written successfully.
 */
class FlightLogManager {
public:
    /** Maximum number of live samples retained for an export snapshot by default. */
    static constexpr std::size_t kDefaultMaxInMemorySamples = 1'000'000U;

    /**
     * @brief Construct an empty manager with a bounded in-memory recording.
     * @param maxSamples Maximum samples accepted through appendSample().
     */
    explicit FlightLogManager(
        std::size_t maxSamples = kDefaultMaxInMemorySamples)
        : m_maxSamples(maxSamples) {}

    /** @brief Remove all samples and release their retained allocation. */
    void clear();

    /**
     * @brief Set the destination used by subsequent CSV exports.
     * @param path Native path encoded as UTF-8.
     */
    void setOutputPath(std::string path);

    /** @brief Return the configured CSV export destination. @return UTF-8 path. */
    [[nodiscard]] const std::string &outputPath() const { return m_fileName; }

    /**
     * @brief Append one sample when the configured recording bound permits it.
     * @param sample Sample copied into the session.
     * @return true when retained; false when the in-memory recording is full.
     */
    bool appendSample(const FlightSample &sample);

    /** @brief Return the configured in-memory sample limit. */
    [[nodiscard]] std::size_t maxSamples() const noexcept { return m_maxSamples; }

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
    std::size_t m_maxSamples = kDefaultMaxInMemorySamples;
};

#endif // COSMO_SOFT_FLIGHTLOGMANAGER_H
