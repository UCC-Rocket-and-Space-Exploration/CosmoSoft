#include "gateway/comms/CommsFactory.h"

#if defined(_WIN32) || defined(_WIN64)
#include "gateway/comms/windows/SerialCommsWindows.h"
#endif
#if defined(__unix__) || defined(__APPLE__) || defined(_POSIX_VERSION)
#include "gateway/comms/Posix/SerialCommsPosix.h"
#endif

std::unique_ptr<IComms> CommsFactory::createSerialComms(const std::string &device, int baud) {
#if defined(__unix__) || defined(__APPLE__) || defined(_POSIX_VERSION)
    return std::make_unique<SerialCommsPosix>(device, baud);
#else
    return std::make_unique<SerialCommsWindows>(device, baud);
// #error "Unsupported platform: implement Windows serial comms"
#endif
}
