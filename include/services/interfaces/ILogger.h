
#ifndef COSMO_SOFT_ILOGGER_H
#define COSMO_SOFT_ILOGGER_H
#include <string>

class ILogger {
    public:
    virtual ~ILogger() = default;
    virtual void Log(std::string record) = 0;
    virtual void Log(unsigned char* record) = 0;
    virtual void LogLine(std::string record) = 0;
    virtual void LogLine(unsigned char* record) = 0;

};
#endif //COSMO_SOFT_ILOGGER_H