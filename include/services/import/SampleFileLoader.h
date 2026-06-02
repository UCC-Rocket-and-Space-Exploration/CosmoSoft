#ifndef COSMO_SOFT_SAMPLEFILELOADER_H
#define COSMO_SOFT_SAMPLEFILELOADER_H

#include <optional>
#include <string>

#include "domain/FlightSession.h"

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
    /** @return Error message if load failed; session is unchanged on error. */
    static std::optional<std::string> loadTheseusCsv(const std::string &path, FlightSession &out);

    /** @return Error message if load failed; session is unchanged on error. */
    static std::optional<std::string> loadXlsx(const std::string &path, FlightSession &out);

    /**
     * Decodes Altos-style "TELEM <hex>" lines through Framer/Parser.
     * @return Error if file unreadable; if decoding yields no samples, returns an explanatory message.
     */
    static std::optional<std::string> loadTelemFile(const std::string &path, FlightSession &out, Framer &framer, Parser &parser);
};

#endif
