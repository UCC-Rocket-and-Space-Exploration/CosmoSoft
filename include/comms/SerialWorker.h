#ifndef COSMO_SOFT_SERIALWORKER_H
#define COSMO_SOFT_SERIALWORKER_H
#include <thread>

#include "IComms.h"
#include "ISerialPortScanner.h"

//To avoid blocking the UI when fetching serial data, we need to use Qt's signals and slots technique for async data fetching
class SerialWorker {
public:
    SerialWorker();
    ~SerialWorker();

private:
    //consider adding additional thread just for the SerialComms (avoid missing any sent data
    std::thread m_thread;
    IComms* m_connectedPort;
    ISerialPortScanner* m_scanner;
    
};


#endif //COSMO_SOFT_SERIALWORKER_H