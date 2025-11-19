#ifndef COSMO_SOFT_SERIALPORTSCANNERPOSIX_H
#define COSMO_SOFT_SERIALPORTSCANNERPOSIX_H
#include <vector>
#include "ISerialPortScanner.h"

//TODO
class SerialPortScannerPosix : ISerialPortScanner {
public:
    std::vector<std::string> enumeratePorts() override;

    bool tryOpenPort(const std::string &portName) override;

};


#endif //COSMO_SOFT_SERIALPORTSCANNERPOSIX_H