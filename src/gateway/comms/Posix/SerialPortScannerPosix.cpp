#include "gateway/comms/Posix/SerialPortScannerPosix.h"

#include <fcntl.h>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <unistd.h>
#include <vector>

#include "gateway/comms/detail/SerialPortScannerUtils.h"

std::vector<std::string> SerialPortScannerPosix::enumeratePorts() {
    constexpr std::string_view DEVICE_DIRECTORY = "/dev";
    std::vector<std::string> ports;
    std::error_code error;
    std::filesystem::directory_iterator iterator(
        DEVICE_DIRECTORY,
        std::filesystem::directory_options::skip_permission_denied,
        error);
    const std::filesystem::directory_iterator end;

    while (!error && iterator != end) {
        const std::filesystem::path &path = iterator->path();
        if (cosmo::serial::detail::is_supported_posix_port_name(path.filename().string())) {
            ports.emplace_back(path.string());
        }
        iterator.increment(error);
    }

    return cosmo::serial::detail::sort_and_deduplicate_ports(std::move(ports));
}

bool SerialPortScannerPosix::tryOpenPort(const std::string &port_name) {
    const int fd = ::open(port_name.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    ::close(fd);
    return true;
}
