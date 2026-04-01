#include "gateway/comms/posix/SerialPortScannerPosix.h"

#include <fcntl.h>
#include <glob.h>
#include <unistd.h>

#include <string>
#include <vector>

std::vector<std::string> SerialPortScannerPosix::enumeratePorts() {
    const char *patterns[] = {
        "/dev/tty.usbserial*",
        "/dev/tty.usbmodem*",
        "/dev/ttyUSB*",
        "/dev/ttyACM*",
        "/dev/ttyS*",
    };

    std::vector<std::string> possiblePorts;

    for (const char *p : patterns) {
        glob_t g{};
        if (glob(p, 0, nullptr, &g) == 0) {
            for (size_t i = 0; i < g.gl_pathc; ++i) {
                possiblePorts.emplace_back(g.gl_pathv[i]);
            }
        }
        globfree(&g);
    }

    std::vector<std::string> accessiblePorts;
    for (const std::string &port : possiblePorts) {
        if (tryOpenPort(port)) {
            accessiblePorts.emplace_back(port);
        }
    }

    return accessiblePorts;
}

bool SerialPortScannerPosix::tryOpenPort(const std::string &portName) {
    const int fd = ::open(portName.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        return false;
    }
    ::close(fd);
    return true;
}
