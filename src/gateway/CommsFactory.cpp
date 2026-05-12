#include "../../include/gateway/CommsFactory.h"

#include "gateway/comms/posix/SerialCommsPosix.h"


#ifdef _WIN32
//TODO implement windows version of SerialComms and include here
#elif __unix__
std::unique_ptr<IComms> CommsFactory::createSerialComms(const std::string& device, int baud) {
    return std::make_unique<SerialCommsPosix>(device, baud);
}
#else
#error "Unsupported platform"
#endif