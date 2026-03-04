#ifndef COSMO_SOFT_COMMSFACTORY_H
#define COSMO_SOFT_COMMSFACTORY_H
#include "IComms.h"

//factory class design pattern for handling instantiating either POSIX or windows implementation of IComms
class CommsFactory {
public:

    //TODO Consider what other parameters are platform agnostic and required
    static std::unique_ptr<IComms> createSerialComms(const std::string &device, int baud);
};

#endif //COSMO_SOFT_COMMSFACTORY_H