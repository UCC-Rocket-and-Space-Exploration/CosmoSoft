#include "gateway/comms/SerialPortScannerFactory.h"

#if defined(_WIN32) || defined(_WIN64)
#include "gateway/comms/Windows/SerialPortScannerWindows.h"
#elif defined(__unix__) || defined(__APPLE__) || defined(_POSIX_VERSION)
#include "gateway/comms/Posix/SerialPortScannerPosix.h"
#endif

std::unique_ptr<ISerialPortScanner> SerialPortScannerFactory::createSerialPortScanner() {
#if defined(_WIN32) || defined(_WIN64)
    return std::make_unique<SerialPortScannerWindows>();
#elif defined(__unix__) || defined(__APPLE__) || defined(_POSIX_VERSION)
    return std::make_unique<SerialPortScannerPosix>();
#else
    return nullptr;
#endif
}
