#ifndef COSMO_SOFT_SERIALPORTSCANNERWINDOWS_H
#define COSMO_SOFT_SERIALPORTSCANNERWINDOWS_H

#include <string>
#include <vector>

#include "gateway/comms/ISerialPortScanner.h"

class SerialPortScannerWindows : public ISerialPortScanner {
public:
    std::vector<std::string> enumeratePorts() override;
    bool tryOpenPort(const std::string& portName) override;
    // const std::string* &portName
    //bool tryOpenPort(const char* &)
};


#endif //COSMO_SOFT_SERIALPORTSCANNERWINDOWS_H
