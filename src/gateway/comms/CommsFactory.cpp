#include "gateway/comms/CommsFactory.h"

#if defined(_WIN32) || defined(_WIN64)
#include "gateway/comms/Windows/SerialCommsWindows.h"
#elif defined(__unix__) || defined(__APPLE__) || defined(_POSIX_VERSION)
#include "gateway/comms/Posix/SerialCommsPosix.h"
#endif

std::unique_ptr<IComms> CommsFactory::createSerialComms(const std::string &device, int baud) {
#if defined(_WIN32) || defined(_WIN64)
    return std::make_unique<SerialCommsWindows>(device, baud);
#elif defined(__unix__) || defined(__APPLE__) || defined(_POSIX_VERSION)
    return std::make_unique<SerialCommsPosix>(device, baud);
#else
    return nullptr;
#endif
}
