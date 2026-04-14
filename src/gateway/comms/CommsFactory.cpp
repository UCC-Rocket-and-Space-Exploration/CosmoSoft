#include "gateway/comms/CommsFactory.h"

#if defined(__unix__) || defined(__APPLE__) || defined(_POSIX_VERSION)
#include "gateway/comms/posix/SerialCommsPosix.h"
#endif

std::unique_ptr<IComms> CommsFactory::createSerialComms(const std::string &device, int baud) {
#if defined(__unix__) || defined(__APPLE__) || defined(_POSIX_VERSION)
    return std::make_unique<SerialCommsPosix>(device, baud);
#else
#error "Unsupported platform: implement Windows serial comms"
#endif
}
