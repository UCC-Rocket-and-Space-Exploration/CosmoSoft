#ifndef COSMO_SOFT_ISERIALPORTSCANNER_H
#define COSMO_SOFT_ISERIALPORTSCANNER_H
#include <string>
#include <vector>

/**
 * @brief Discovers serial port names and optionally checks whether one can be opened.
 *
 * Enumeration reports operating-system candidates without opening them. Call
 * tryOpenPort() only when an explicit accessibility probe is required.
 */
class ISerialPortScanner {
public:
    /**
     * @brief Destroy the serial port scanner.
     */
    virtual ~ISerialPortScanner() = default;

    /**
     * @brief Enumerate serial port candidates known to the operating system.
     * @return A deterministically sorted list with duplicate names removed.
     */
    virtual std::vector<std::string> enumeratePorts() = 0;

    /**
     * @brief Check whether a serial port can currently be opened for read/write access.
     * @param port_name Platform serial port name or device path.
     * @return true when the port was opened successfully, otherwise false.
     */
    virtual bool tryOpenPort(const std::string &port_name) = 0;
};

#endif // COSMO_SOFT_ISERIALPORTSCANNER_H
