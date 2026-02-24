#ifndef COSMO_SOFT_FLIGHTLOGMANAGER_H
#define COSMO_SOFT_FLIGHTLOGMANAGER_H
#include <memory_resource>
#include <string>

#include "../../domain/FlightSession.h"

class Logger {
public:

private:
    FlightSession m_session;
    std::pmr::string m_fileName;
};

#endif //COSMO_SOFT_FLIGHTLOGMANAGER_H
