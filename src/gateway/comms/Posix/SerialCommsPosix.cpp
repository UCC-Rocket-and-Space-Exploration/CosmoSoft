#include "gateway/comms/Posix/SerialCommsPosix.h"

#include <fcntl.h>
#include <poll.h>
#include <cerrno>
#include <unistd.h>

#include <chrono>
#include <thread>

SerialCommsPosix::SerialCommsPosix(const std::string &device, const int baud) {
    m_baud = baud;
    m_device = device;
    m_port = device;
}

SerialCommsPosix::~SerialCommsPosix() {
    SerialCommsPosix::close();
}

bool SerialCommsPosix::open() {
    close();
    m_fd = ::open(m_device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (m_fd < 0) {
        return false;
    }
    return true;
}

void SerialCommsPosix::close() {
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
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
    pfd.events = POLLIN | POLLERR | POLLHUP;
#ifdef POLLRDHUP
    pfd.events |= POLLRDHUP;
#endif

    while (isOpen()) {
        const int data = poll(&pfd, 1, 200);
        if (data == 0) {
            return 0;
        }
        if (data > 0) {
            if (pfd.revents & POLLIN) {
                const ssize_t n = ::read(m_fd, buffer, maxSize);
                return n;
            }
            if (pfd.revents & POLLERR) {
                break;
            }
            if (pfd.revents & POLLHUP) {
                for (int i = 0; i < 10; ++i) {
                    if (open()) {
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                if (isOpen()) {
                    continue;
                }
                close();
                break;
            }
#ifdef POLLRDHUP
            if (pfd.revents & POLLRDHUP) {
                break;
            }
#endif
        }
        if (data < 0 && errno != EAGAIN) {
            break;
        }
    }
    return -1;
}

std::string SerialCommsPosix::getDeviceName() const {
    return m_device;
}
