#include "../../../include/gateway/posix/SerialPortScannerPosix.h"

#include <fcntl.h>
#include <glob.h>
#include <string>
#include <unistd.h>
#include <vector>

std::vector<std::string> SerialPortScannerPosix::enumeratePorts() {
    const char* patterns[] = {
        "/dev/ttyUSB*",   // Linux USB-to-serial
        "/dev/ttyACM*",   // Linux CDC-ACM devices (e.g. Arduino)
        "/dev/ttyS*",     // Linux legacy serial
        //"/dev/pts/*",   //internal loopback virtual ports, for testing only
    };

    std::vector<std::string> possiblePorts;

    for (const char* p : patterns) {
        glob_t g{};
        if (glob(p, 0, nullptr, &g) == 0) {
            for (size_t i = 0; i < g.gl_pathc; ++i) { //gl_pathc = number of paths matching the pattern
                possiblePorts.emplace_back(g.gl_pathv[i]); //gl.pathv = ponter to the list of matched paths
                //using emplace_back instead of push_back is a best practice
            }
        }
        globfree(&g); //frees memory used by glob
    }

    std::vector<std::string> accessiblePorts;
    for (const std::string& port : possiblePorts) {
        if (tryOpenPort(port)) {
            accessiblePorts.emplace_back(port);
        }
    }

    return accessiblePorts;
}

bool SerialPortScannerPosix::tryOpenPort(const std::string& portName) {
    const int fd = ::open(portName.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return false;
    ::close(fd);
    return true;
}
