#ifndef COSMO_SOFT_SERIALPORTSCANNERWINDOWS_H
#define COSMO_SOFT_SERIALPORTSCANNERWINDOWS_H

#include <string>
#include <vector>

#include "gateway/comms/ISerialPortScanner.h"

/**
 * @brief Discovers serial ports registered by Windows.
 */
class SerialPortScannerWindows : public ISerialPortScanner {
public:
    /**
     * @brief Enumerate registered Windows serial port names.
     * @return A deterministically sorted list with duplicate names removed.
     * @throws std::runtime_error when the registry cannot be queried safely.
     */
    std::vector<std::string> enumeratePorts() override;

    /**
     * @brief Probe a Windows serial port for read/write accessibility.
     * @param port_name A COM name such as COM10 or a Win32 device path.
     * @return true when the port could be opened, otherwise false.
     */
    bool tryOpenPort(const std::string &port_name) override;
};

#endif // COSMO_SOFT_SERIALPORTSCANNERWINDOWS_H
