#ifndef COSMO_SOFT_SERIALPORTSCANNERPOSIX_H
#define COSMO_SOFT_SERIALPORTSCANNERPOSIX_H
#if defined(__unix__) || defined(__APPLE__) || defined(_POSIX_VERSION)
#include "gateway/comms/ISerialPortScanner.h"

//TODO
class SerialPortScannerPosix : public ISerialPortScanner {
public:
    std::vector<std::string> enumeratePorts() override;

    bool tryOpenPort(const std::string &portName) override;

};


#endif //Posix Check
#endif //COSMO_SOFT_SERIALPORTSCANNERPOSIX_H
