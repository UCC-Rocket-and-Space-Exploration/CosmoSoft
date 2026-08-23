#include "gateway/comms/SerialPortScannerFactory.h"

#if defined(__unix__) || defined(__APPLE__) || defined(_POSIX_VERSION)
#include "gateway/comms/Posix/SerialPortScannerPosix.h"
#endif

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__)
#include "gateway/comms/Windows/SerialPortScannerWindows.h"
#endif

std::unique_ptr<ISerialPortScanner> SerialPortScannerFactory::createSerialPortScanner() {
#if defined(__unix__) || defined(__APPLE__) || defined(_POSIX_VERSION)
    return std::make_unique<SerialPortScannerPosix>();
#elif defined(WIN32) || defined(_WIN32) || defined(__WIN32__)
    return std::make_unique<SerialPortScannerWindows>();
#else
    throw std::system_error(
        std::make_error_code(std::errc::not_supported),
        "Platform is not supported. "
    );
#endif
}
