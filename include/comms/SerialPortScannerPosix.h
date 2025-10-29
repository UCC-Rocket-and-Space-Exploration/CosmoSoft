#ifndef COSMO_SOFT_SERIALPORTSCANNERPOSIX_H
#define COSMO_SOFT_SERIALPORTSCANNERPOSIX_H
#if defined(__unix__) || defined(__APPLE__) || defined(_POSIX_VERSION)
#include "ISerialPortScanner.h"

//TODO
class SerialPortScannerPosix : ISerialPortScanner {
    //Ideas for necessary member variables & methods:
    //methods
    //  constructor
    //  accessors
    //  potentially mutators?
    //  EnumeratePorts(): enumerate all possible serial ports, regardless of accessibility
    //  TryOpen(): attempt to open a serial port connection; use to see which ports are accessible#
    //  EnumerateAccessible():
    //variables
    //  m_patterns: patterns for POSIX serial ports
    //  m_ports:    all found ports
    //  potentially some form of mapping of port to VendorID and ModelID?

};


#endif //LINUX
#endif //COSMO_SOFT_SERIALPORTSCANNERPOSIX_H