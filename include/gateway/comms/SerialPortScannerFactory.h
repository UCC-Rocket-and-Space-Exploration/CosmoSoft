#ifndef COSMO_SOFT_SERIALPORTSCANNERFACTORY_H
#define COSMO_SOFT_SERIALPORTSCANNERFACTORY_H
#include <memory>

#include "interfaces/ISerialPortScanner.h"


class SerialPortScannerFactory {
public:
    /// throws std::system_error if platform is not supported(ie is neither unix nor windows)
    static std::unique_ptr<ISerialPortScanner> createSerialPortScanner();
};

#endif //COSMO_SOFT_SERIALPORTSCANNERFACTORY_H
