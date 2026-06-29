//
// Created by Lenovo on 6/16/2026.
//

#ifndef COSMO_SOFT_SERIALWRITERMOCK_H
#define COSMO_SOFT_SERIALWRITERMOCK_H
#include "gateway/comms/interfaces/IComms.h"

class SerialWriter {
public:
    explicit SerialWriter(const std::shared_ptr<IComms>& comms)  : m_comms(comms){
        if (!m_comms->isOpen()) {
            m_comms->open();
        }
    }
    SerialWriter(SerialWriter&& other) = delete;
    void write(char* message) const;
private:
    const std::shared_ptr<IComms>& m_comms;
};
#endif //COSMO_SOFT_SERIALWRITERMOCK_H