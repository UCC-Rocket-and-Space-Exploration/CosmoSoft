#include "gateway/comms/Windows/SerialPortScannerWindows.h"

#include <cstddef>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <windows.h>

#include "gateway/comms/detail/SerialPortScannerUtils.h"

namespace {

constexpr std::size_t INITIAL_REGISTRY_BUFFER_SIZE = 256;
constexpr std::size_t MAXIMUM_REGISTRY_BUFFER_SIZE = 1024 * 1024;

class RegistryKey final {
public:
    explicit RegistryKey(HKEY key) noexcept
        : m_key(key) {}

    ~RegistryKey() {
        if (m_key != nullptr) {
            RegCloseKey(m_key);
        }
    }

    RegistryKey(const RegistryKey &) = delete;
    RegistryKey &operator=(const RegistryKey &) = delete;

    [[nodiscard]] HKEY get() const noexcept { return m_key; }

private:
    HKEY m_key = nullptr;
};

class FileHandle final {
public:
    explicit FileHandle(HANDLE handle) noexcept
        : m_handle(handle) {}

    ~FileHandle() {
        if (m_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(m_handle);
        }
    }

    FileHandle(const FileHandle &) = delete;
    FileHandle &operator=(const FileHandle &) = delete;

    [[nodiscard]] bool is_valid() const noexcept { return m_handle != INVALID_HANDLE_VALUE; }

private:
    HANDLE m_handle = INVALID_HANDLE_VALUE;
};

[[nodiscard]] std::runtime_error registry_error(const std::string &operation, LSTATUS status) {
    return std::runtime_error(
        operation + " failed with Windows error " + std::to_string(status));
}

template <typename Value>
void grow_registry_buffer(std::vector<Value> &buffer, DWORD reported_size) {
    const std::size_t reported = static_cast<std::size_t>(reported_size);
    if (reported >= MAXIMUM_REGISTRY_BUFFER_SIZE) {
        throw std::runtime_error("Windows serial registry value exceeds the supported size");
    }
    const std::size_t doubled = buffer.size() <= MAXIMUM_REGISTRY_BUFFER_SIZE / 2
                                    ? buffer.size() * 2
                                    : MAXIMUM_REGISTRY_BUFFER_SIZE;
    const std::size_t reported_with_terminator = reported + 1;
    const std::size_t requested =
        doubled > reported_with_terminator ? doubled : reported_with_terminator;
    if (requested > MAXIMUM_REGISTRY_BUFFER_SIZE || requested <= buffer.size()) {
        throw std::runtime_error("Windows serial registry value exceeds the supported size");
    }
    buffer.resize(requested);
}

[[nodiscard]] DWORD buffer_size_as_dword(std::size_t size) {
    if (size > static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())) {
        throw std::runtime_error("Windows serial registry buffer is too large");
    }
    return static_cast<DWORD>(size);
}

[[nodiscard]] std::string checked_registry_port_name(
    const std::vector<BYTE> &data,
    DWORD data_size,
    DWORD type) {
    const std::size_t byte_count = static_cast<std::size_t>(data_size);
    if (type != REG_SZ) {
        return {};
    }

    const auto port_name = cosmo::serial::detail::checked_windows_registry_string(
        std::span<const unsigned char>(data.data(), data.size()),
        byte_count);
    if (!port_name.has_value()) {
        return {};
    }

    return cosmo::serial::detail::canonical_windows_port_name(*port_name);
}

} // namespace

std::vector<std::string> SerialPortScannerWindows::enumeratePorts() {
    constexpr const char *SUB_KEY = "HARDWARE\\DEVICEMAP\\SERIALCOMM";

    HKEY raw_key = nullptr;
    const LSTATUS open_status =
        RegOpenKeyExA(HKEY_LOCAL_MACHINE, SUB_KEY, 0, KEY_READ, &raw_key);
    if (open_status == ERROR_FILE_NOT_FOUND) {
        return {};
    }
    if (open_status != ERROR_SUCCESS) {
        throw registry_error("Opening the Windows serial registry key", open_status);
    }
    const RegistryKey key(raw_key);

    std::vector<std::string> ports;
    DWORD index = 0;
    while (true) {
        std::vector<char> value_name(INITIAL_REGISTRY_BUFFER_SIZE);
        std::vector<BYTE> data(INITIAL_REGISTRY_BUFFER_SIZE);

        while (true) {
            DWORD value_name_size = buffer_size_as_dword(value_name.size());
            DWORD data_size = buffer_size_as_dword(data.size());
            DWORD type = 0;
            const LSTATUS status = RegEnumValueA(
                key.get(),
                index,
                value_name.data(),
                &value_name_size,
                nullptr,
                &type,
                data.data(),
                &data_size);

            if (status == ERROR_MORE_DATA) {
                grow_registry_buffer(value_name, value_name_size);
                grow_registry_buffer(data, data_size);
                continue;
            }
            if (status == ERROR_NO_MORE_ITEMS) {
                return cosmo::serial::detail::sort_and_deduplicate_ports(std::move(ports));
            }
            if (status != ERROR_SUCCESS) {
                throw registry_error("Enumerating Windows serial registry values", status);
            }

            std::string port_name = checked_registry_port_name(data, data_size, type);
            if (!port_name.empty()) {
                ports.emplace_back(std::move(port_name));
            }
            break;
        }

        if (index == (std::numeric_limits<DWORD>::max)()) {
            throw std::runtime_error("Windows serial registry contains too many values");
        }
        ++index;
    }
}

bool SerialPortScannerWindows::tryOpenPort(const std::string &port_name) {
    const std::string device_path = cosmo::serial::detail::normalize_windows_port_path(port_name);
    const FileHandle handle(CreateFileA(
        device_path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr));
    return handle.is_valid();
}
