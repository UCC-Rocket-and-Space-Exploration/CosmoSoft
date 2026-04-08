#include "gateway/comms/windows/SerialCommsWindows.h"

SerialCommsWindows::SerialCommsWindows(const std::string& device, const int baud) : m_device(device){
    m_baud = baud;
}

bool SerialCommsWindows::open() {

}

