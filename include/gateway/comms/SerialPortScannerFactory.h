#ifndef COSMO_SOFT_SERIALPORTSCANNERFACTORY_H
#define COSMO_SOFT_SERIALPORTSCANNERFACTORY_H
#include "ISerialPortScanner.h"

#include <memory>

class SerialPortScannerFactory {
public:
    /** @brief Create the scanner implementation for the current platform. */
    [[nodiscard]] static std::unique_ptr<ISerialPortScanner> createSerialPortScanner();
};

#endif //COSMO_SOFT_SERIALPORTSCANNERFACTORY_H
