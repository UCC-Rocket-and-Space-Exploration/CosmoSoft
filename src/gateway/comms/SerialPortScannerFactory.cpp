#include "gateway/comms/SerialPortScannerFactory.h"

#if defined(__unix__) || defined(__APPLE__) || defined(_POSIX_VERSION)
#include "gateway/comms/Posix/SerialPortScannerPosix.h"
#endif

ISerialPortScanner *SerialPortScannerFactory::createSerialPortScanner() {
#if defined(__unix__) || defined(__APPLE__) || defined(_POSIX_VERSION)
    return new SerialPortScannerPosix();
#else
    return nullptr;
#endif
}
