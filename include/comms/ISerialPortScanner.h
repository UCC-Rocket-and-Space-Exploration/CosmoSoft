//
// Created by mark on 28.10.25.
//

#ifndef COSMO_SOFT_ISERIALPORTSCANNER_H
#define COSMO_SOFT_ISERIALPORTSCANNER_H
#include <string>
#include <vector>

class ISerialPortScanner {
protected:
    ~ISerialPortScanner() = default;

public:
    //virtual ~ISerialPortScanner() = default;

    virtual std::vector<std::string> enumeratePorts();

    virtual bool tryOpenPort(const std::string& portName);
private:

};


#endif //COSMO_SOFT_ISERIALPORTSCANNER_H