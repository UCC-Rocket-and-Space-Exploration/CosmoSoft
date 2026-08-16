
#include "include/SerialWriter.h"
#include <cstring>
#include <iostream>
#include <thread>

#include "gateway/comms/Windows/SerialCommsWindows.h"

void SerialWriter::write(char* message) const {
    this->m_comms->write(reinterpret_cast<uint8_t*>(message), strlen(message));
}
