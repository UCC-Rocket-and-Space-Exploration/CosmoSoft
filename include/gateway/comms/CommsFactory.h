#ifndef COSMO_SOFT_COMMSFACTORY_H
#define COSMO_SOFT_COMMSFACTORY_H

#include <memory>
#include <string>

#include "interfaces/IComms.h"

//factory class design pattern for handling instantiating either POSIX or windows implementation of IComms
class CommsFactory {
public:
    /// throws std::system_error if platform is not supported(ie is neither unix nor windows)
    static std::unique_ptr<IComms> createSerialComms(const std::string &device, int baud);
};

#endif //COSMO_SOFT_COMMSFACTORY_H