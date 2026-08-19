#ifndef COSMO_SOFT_SERIALPORTSCANNERPOSIX_H
#define COSMO_SOFT_SERIALPORTSCANNERPOSIX_H
#if defined(__unix__) || defined(__APPLE__) || defined(_POSIX_VERSION)
#include "gateway/comms/ISerialPortScanner.h"

/**
 * @brief Discovers POSIX serial device nodes without opening candidate devices.
 */
class SerialPortScannerPosix : public ISerialPortScanner {
public:
    /**
     * @brief Enumerate supported serial device nodes below /dev.
     * @return A sorted list with duplicate paths removed.
     */
    std::vector<std::string> enumeratePorts() override;

    /**
     * @brief Probe a device node for read/write accessibility.
     * @param port_name Absolute POSIX device path.
     * @return true when the device could be opened, otherwise false.
     */
    bool tryOpenPort(const std::string &port_name) override;
};

#endif // POSIX check
#endif // COSMO_SOFT_SERIALPORTSCANNERPOSIX_H
