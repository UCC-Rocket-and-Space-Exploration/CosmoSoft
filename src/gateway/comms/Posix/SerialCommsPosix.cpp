#include "gateway/comms/Posix/SerialCommsPosix.h"

#include <cerrno>
#include <fcntl.h>
#include <sys/poll.h>
#include <termios.h>
#include <unistd.h>

namespace {
constexpr int kReadPollTimeoutMs = 50;

[[nodiscard]] bool baudToSpeed(const int baud, speed_t &speed) {
    switch (baud) {
    case 1200:
        speed = B1200;
        return true;
    case 2400:
        speed = B2400;
        return true;
    case 4800:
        speed = B4800;
        return true;
    case 9600:
        speed = B9600;
        return true;
    case 19200:
        speed = B19200;
        return true;
    case 38400:
        speed = B38400;
        return true;
    case 57600:
        speed = B57600;
        return true;
    case 115200:
        speed = B115200;
        return true;
#ifdef B230400
    case 230400:
        speed = B230400;
        return true;
#endif
    default:
        return false;
    }
}

[[nodiscard]] bool configurePort(const int fd, const int baud) {
    speed_t speed = B0;
    if (!baudToSpeed(baud, speed)) {
        errno = EINVAL;
        return false;
    }

    termios attributes{};
    if (::tcgetattr(fd, &attributes) != 0) {
        return false;
    }

    attributes.c_iflag &= static_cast<tcflag_t>(
        ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL
          | IXON | IXOFF | IXANY));
    attributes.c_oflag &= static_cast<tcflag_t>(~OPOST);
    attributes.c_lflag &= static_cast<tcflag_t>(~(ECHO | ECHONL | ICANON | ISIG | IEXTEN));
    attributes.c_cflag &= static_cast<tcflag_t>(~(CSIZE | PARENB | CSTOPB));
#ifdef CRTSCTS
    attributes.c_cflag &= static_cast<tcflag_t>(~CRTSCTS);
#endif
    attributes.c_cflag |= static_cast<tcflag_t>(CS8 | CLOCAL | CREAD);
    attributes.c_cc[VMIN] = 0;
    attributes.c_cc[VTIME] = 0;

    return ::cfsetispeed(&attributes, speed) == 0
        && ::cfsetospeed(&attributes, speed) == 0
        && ::tcsetattr(fd, TCSANOW, &attributes) == 0;
}
}

SerialCommsPosix::SerialCommsPosix(const std::string &device, const int baud)
    : m_device(device), m_baud(baud) {}

SerialCommsPosix::~SerialCommsPosix() {
    close();
}

bool SerialCommsPosix::open() {
    std::scoped_lock lock(m_handleMutex);

    if (m_fd >= 0) {
        const int oldFd = m_fd;
        m_fd = -1;
        ++m_generation;
        ::close(oldFd);
    }

    const int fd = ::open(m_device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        return false;
    }
    if (!configurePort(fd, m_baud)) {
        ::close(fd);
        return false;
    }

    m_fd = fd;
    ++m_generation;
    return true;
}

void SerialCommsPosix::close() {
    std::scoped_lock lock(m_handleMutex);
    if (m_fd >= 0) {
        const int fd = m_fd;
        m_fd = -1;
        ++m_generation;
        ::close(fd);
    }
}

bool SerialCommsPosix::isOpen() const {
    std::scoped_lock lock(m_handleMutex);
    return m_fd >= 0;
}

ssize_t SerialCommsPosix::write(const uint8_t *data, const size_t size) {
    if (data == nullptr && size > 0) {
        errno = EINVAL;
        return -1;
    }
    if (size == 0) {
        return 0;
    }

    std::scoped_lock lock(m_handleMutex);
    if (m_fd < 0) {
        errno = EBADF;
        return -1;
    }
    return ::write(m_fd, data, size);
}

ssize_t SerialCommsPosix::read(uint8_t *buffer, const size_t maxSize) {
    if (buffer == nullptr && maxSize > 0) {
        errno = EINVAL;
        return -1;
    }
    if (maxSize == 0) {
        return 0;
    }

    int fd = -1;
    std::uint64_t generation = 0;
    {
        std::scoped_lock lock(m_handleMutex);
        fd = m_fd;
        generation = m_generation;
    }
    if (fd < 0) {
        errno = EBADF;
        return -1;
    }

    pollfd descriptor{};
    descriptor.fd = fd;
    descriptor.events = POLLIN | POLLERR | POLLHUP;
#ifdef POLLRDHUP
    descriptor.events |= POLLRDHUP;
#endif

    const int ready = ::poll(&descriptor, 1, kReadPollTimeoutMs);
    if (ready == 0 || (ready < 0 && errno == EINTR)) {
        return 0;
    }
    if (ready < 0) {
        return -1;
    }

    if ((descriptor.revents & POLLIN) != 0) {
        // Check the generation while holding the lock so a descriptor closed
        // during poll() can never be mistaken for a subsequently reused fd.
        std::scoped_lock lock(m_handleMutex);
        if (m_fd != fd || m_generation != generation) {
            errno = ECANCELED;
            return -1;
        }

        const ssize_t count = ::read(fd, buffer, maxSize);
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            return 0;
        }
        return count;
    }

    constexpr short kTerminalEvents = POLLERR | POLLHUP | POLLNVAL
#ifdef POLLRDHUP
                                      | POLLRDHUP
#endif
        ;
    if ((descriptor.revents & kTerminalEvents) != 0) {
        std::scoped_lock lock(m_handleMutex);
        if (m_fd == fd && m_generation == generation) {
            m_fd = -1;
            ++m_generation;
            ::close(fd);
        }
        errno = EIO;
        return -1;
    }

    return 0;
}

std::string SerialCommsPosix::getDeviceName() const {
    return m_device;
}
