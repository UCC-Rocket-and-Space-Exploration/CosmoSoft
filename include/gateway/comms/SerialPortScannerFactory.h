#ifndef COSMO_SOFT_SERIALPORTSCANNERFACTORY_H
#define COSMO_SOFT_SERIALPORTSCANNERFACTORY_H
#include "interfaces/ISerialPortScanner.h"


class SerialPortScannerFactory {
public:
    static ISerialPortScanner* createSerialPortScanner();
};

#endif //COSMO_SOFT_SERIALPORTSCANNERFACTORY_H
