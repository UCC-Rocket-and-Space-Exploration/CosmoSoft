#include "comms/SerialCommsPosix.h"

#include <glob.h>
#include <vector>
#include <string>
#include <sys/stat.h>
#include <iostream>
#include <string.h>
#include <fcntl.h>
#include <iomanip>
#include <chrono>
#include <thread>
#include <unistd.h>
#include <sys/poll.h>

SerialCommsPosix::SerialCommsPosix(const std::string &device, const int baud) {
    m_baud = baud;
    m_device = device; //TODO
}

SerialCommsPosix::~SerialCommsPosix() {
    SerialCommsPosix::close();
}

bool SerialCommsPosix::open() {
    m_fd = ::open(m_device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (m_fd < 0) return false; // failed to open
    return true;
}

void SerialCommsPosix::close() {
    if (m_fd >= 0) {
        m_fd = ::close(m_fd);
    }
}

bool SerialCommsPosix::isOpen() const {
    return m_fd >= 0;
}

ssize_t SerialCommsPosix::write(const uint8_t *data, size_t size) {
    return ::write(m_fd, data, size);
}

ssize_t SerialCommsPosix::read(uint8_t *buffer, size_t maxSize) {
    pollfd pfd{};
    pfd.fd = m_fd;
    pfd.events = POLLIN | POLLERR | POLLHUP | POLLRDHUP;

    while (true) {
        int data = poll(&pfd, 1, -1); // block until data
        if (data > 0) {
            if (pfd.revents & POLLIN) { //when there is data to be read
                const ssize_t n = ::read(m_fd, buffer, sizeof(buffer)); //read from fd into the buffer all possible bytes
                return n;
            }
            if (pfd.revents & POLLERR) { //general error, typically a problem with the code itself (inc. frame errors, line errors)
                //std::cerr << "Serial port error!\n";
                //TODO add logging & error handling (consider whether errors should be bubbled up or handled internally
                break;
            }
            if (pfd.revents & POLLHUP) { //fires if a disconnect happens without explicit closing
                //TODO improve handling of reconnecting
                for (int i = 0; i < 10; ++i) {  //try to reconnect 10 times

                    if (open()) {
                        break; //the connection is reestablished
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }

                if (isOpen()) {
                    continue;
                }
                //std::cerr << "Serial port disconnected (HUP)!\n";
                close(); //if connection has not been reestablished, close the connection
                break;
            }
            if (pfd.revents & POLLRDHUP) { //fires if the peer closes the serial connection
                //std::cerr << "Peer closed connection (RDHUP)\n";
                break;
            }
        }
    }
    return -1;
}

std::string SerialCommsPosix::getDeviceName() const {
    return m_device;
}

std::string SerialCommsPosix::getDevicePort() const {
    return m_port;
}