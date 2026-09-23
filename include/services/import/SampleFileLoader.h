#ifndef COSMO_SOFT_SAMPLEFILELOADER_H
#define COSMO_SOFT_SAMPLEFILELOADER_H

#include <optional>
#include <string>

#include "domain/FlightSession.h"
#include "services/Cancellation.h"
#include "services/telemetry/FrameDecoderVault.h"

class Framer;
class Parser;

/**
 * Loads flight logs into FlightSession.
 * - CSV: first non-comment row is the header with supported telemetry column names.
 * - XLSX: first worksheet with supported telemetry column names in the first non-empty row.
 * - TELEM: hex lines fed through Framer + Parser (samples only if decode is implemented).
 */
class SampleFileLoader {
public:
    /**
     * @brief Outcome returned by cancellable file-loading overloads.
     */
    struct LoadResult {
        /** Error detail when loading failed for a reason other than cancellation. */
        std::optional<std::string> error;
        /** True when cooperative cancellation stopped the load. */
        bool canceled = false;

        /**
         * @brief Return whether loading completed successfully.
         * @return true when there is no error and cancellation was not requested.
         */
        [[nodiscard]] bool succeeded() const noexcept
        {
            return !canceled && !error.has_value();
        }
    };

    /**
     * @brief Load a Theseus CSV flight log.
     * @param path Source file path.
     * @param out Destination session, unchanged on failure.
     * @return Error message if loading failed.
     */
    static std::optional<std::string> loadTheseusCsv(const std::string &path, FlightSession &out);

    /**
     * @brief Load a Theseus CSV flight log with cooperative cancellation.
     * @param path Source file path.
     * @param out Destination session, unchanged on failure or cancellation.
     * @param cancellation_check Callback polled during file reading and row parsing.
     * @return Structured success, error, or cancellation outcome.
     */
    static LoadResult loadTheseusCsv(
        const std::string &path,
        FlightSession &out,
        const cosmo::CancellationCheck &cancellation_check);

    /**
     * @brief Load the first telemetry worksheet in an XLSX workbook.
     * @param path Source file path.
     * @param out Destination session, unchanged on failure.
     * @return Error message if loading failed.
     */
    static std::optional<std::string> loadXlsx(const std::string &path, FlightSession &out);

    /**
     * @brief Load an XLSX workbook with cooperative cancellation.
     * @param path Source file path.
     * @param out Destination session, unchanged on failure or cancellation.
     * @param cancellation_check Callback polled during input, ZIP, XML, and row processing.
     * @return Structured success, error, or cancellation outcome.
     */
    static LoadResult loadXlsx(
        const std::string &path,
        FlightSession &out,
        const cosmo::CancellationCheck &cancellation_check);

    /**
     * @brief Decode Altos-style "TELEM <hex>" lines through Framer and Parser.
     * @param path Source file path.
     * @param out Destination session.
     * @param framer Binary frame extractor.
     * @param parser Telemetry frame decoder.
     * @return Error if file unreadable; if decoding yields no samples, returns an explanatory message.
     */
    static std::optional<std::string> loadTelemFile(
        const std::string &path,
        FlightSession &out,
        Framer &framer,
        Parser &parser);

    /**
     * @brief Decode a TELEM file with cooperative cancellation.
     * @param path Source file path.
     * @param out Destination session, unchanged on cancellation.
     * @param decoder_vault
     * @param framer Binary frame extractor.
     * @param parser Telemetry frame decoder.
     * @param cancellation_check Callback polled during line, hex, and frame processing.
     * @return Structured success, error, or cancellation outcome.
     */
    static LoadResult loadTelemFile(
        const std::string &path,
        FlightSession &out,
        FrameDecoderVault& decoder_vault,
        const cosmo::CancellationCheck &cancellation_check);
};

#endif
