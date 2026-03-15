#ifndef COSMO_SOFT_SERIALPORTSCANNERWINDOWS_H
#define COSMO_SOFT_SERIALPORTSCANNERWINDOWS_H

#include <string>
#include <vector>

#include "ISerialPortScanner.h"
#include "gui/MainWindow.h"

class SerialPortScannerWindows : public ISerialPortScanner {
public:
    std::vector<std::string> enumeratePorts() override { return {}; }
    bool tryOpenPort(const std::string &) override { return false; }
};


#endif //COSMO_SOFT_SERIALPORTSCANNERWINDOWS_H
