#include "gateway/comms/CommsFactory.h"

#include "gateway/comms/windows/SerialCommsWindows.h"
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
