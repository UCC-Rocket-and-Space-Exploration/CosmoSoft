#include "comms/SerialPortScannerFactory.h"

#if defined(_WIN32) || defined(_WIN64)
#include "comms/SerialPortScannerWindows.h"
#else
#include "comms/SerialPortScannerPosix.h"
#endif

ISerialPortScanner* SerialPortScannerFactory::createSerialPortScanner() {
#if defined(_WIN32) || defined(_WIN64)
    static SerialPortScannerWindows scanner;
#else
    static SerialPortScannerPosix scanner;
#endif
    return &scanner; // Static lifetime; do not delete.
}
