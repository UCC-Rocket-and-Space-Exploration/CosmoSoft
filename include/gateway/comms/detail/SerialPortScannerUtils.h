#ifndef COSMO_SOFT_SERIALPORTSCANNERUTILS_H
#define COSMO_SOFT_SERIALPORTSCANNERUTILS_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cosmo::serial::detail {

/**
 * @brief Test whether a /dev entry name matches a supported serial device family.
 * @param entry_name Filename relative to /dev, without a directory prefix.
 * @return true for supported Linux and macOS serial device naming conventions.
 */
[[nodiscard]] inline bool is_supported_posix_port_name(std::string_view entry_name) noexcept {
    constexpr std::array<std::string_view, 5> PREFIXES{
        "ttyUSB",
        "ttyACM",
        "ttyS",
        "cu.",
        "tty.",
    };

    return std::any_of(PREFIXES.cbegin(), PREFIXES.cend(), [entry_name](std::string_view prefix) {
        return entry_name.starts_with(prefix) && entry_name.size() > prefix.size();
    });
}

/**
 * @brief Sort serial port paths and remove exact duplicates.
 * @param ports Port paths to normalize.
 * @return The sorted unique paths.
 */
[[nodiscard]] inline std::vector<std::string> sort_and_deduplicate_ports(
    std::vector<std::string> ports) {
    std::sort(ports.begin(), ports.end());
    ports.erase(std::unique(ports.begin(), ports.end()), ports.end());
    return ports;
}

/**
 * @brief Compare two ASCII characters without regard to letter case.
 * @param lhs First character.
 * @param rhs Second character.
 * @return true when both characters represent the same ASCII letter or byte value.
 */
[[nodiscard]] inline bool equals_ascii_case_insensitive(char lhs, char rhs) noexcept {
    const auto fold_case = [](char character) {
        if (character >= 'a' && character <= 'z') {
            return static_cast<char>(character - ('a' - 'A'));
        }
        return character;
    };
    return fold_case(lhs) == fold_case(rhs);
}

/**
 * @brief Test whether a character is an ASCII decimal digit.
 * @param character Character to test.
 * @return true for characters in the range 0 through 9.
 */
[[nodiscard]] inline bool is_ascii_digit(char character) noexcept {
    return character >= '0' && character <= '9';
}

/**
 * @brief Convert a Windows COM port name to a canonical display name.
 * @param port_name A bare COM name or a Win32 device path.
 * @return COM followed by its decimal suffix, or the original name when it is not a COM port.
 */
[[nodiscard]] inline std::string canonical_windows_port_name(std::string_view port_name) {
    constexpr std::string_view DEVICE_PREFIX = R"(\\.\)";
    const std::string_view original_port_name = port_name;
    if (port_name.starts_with(DEVICE_PREFIX)) {
        port_name.remove_prefix(DEVICE_PREFIX.size());
    }

    const bool has_com_prefix = port_name.size() > 3
                                && equals_ascii_case_insensitive(port_name[0], 'C')
                                && equals_ascii_case_insensitive(port_name[1], 'O')
                                && equals_ascii_case_insensitive(port_name[2], 'M');
    const bool has_numeric_suffix =
        has_com_prefix
        && std::all_of(port_name.cbegin() + 3, port_name.cend(), is_ascii_digit);
    if (!has_numeric_suffix) {
        return std::string(original_port_name);
    }

    return std::string("COM") + std::string(port_name.substr(3));
}

/**
 * @brief Normalize a Windows COM port for CreateFileA.
 * @param port_name A bare COM name or an existing Win32 device path.
 * @return A \\.\COMx path for valid COM names; otherwise the original path.
 */
[[nodiscard]] inline std::string normalize_windows_port_path(std::string_view port_name) {
    constexpr std::string_view DEVICE_PREFIX = R"(\\.\)";
    const std::string canonical_name = canonical_windows_port_name(port_name);
    if (canonical_name.size() > 3 && canonical_name.starts_with("COM")
        && std::all_of(canonical_name.cbegin() + 3, canonical_name.cend(), is_ascii_digit)) {
        return std::string(DEVICE_PREFIX) + canonical_name;
    }

    return std::string(port_name);
}

/**
 * @brief Parse a bounded, null-terminated Windows registry string value.
 * @param buffer Bytes available from the registry query.
 * @param reported_size Number of bytes reported by the registry API.
 * @return The string when it is non-empty, bounded, and has exactly one terminal null byte.
 */
[[nodiscard]] inline std::optional<std::string> checked_windows_registry_string(
    std::span<const unsigned char> buffer,
    std::size_t reported_size) {
    if (reported_size == 0 || reported_size > buffer.size() || buffer[reported_size - 1] != 0) {
        return std::nullopt;
    }

    const auto *characters = reinterpret_cast<const char *>(buffer.data());
    std::string result(characters, reported_size - 1);
    if (result.empty() || result.find('\0') != std::string::npos) {
        return std::nullopt;
    }
    return result;
}

} // namespace cosmo::serial::detail

#endif // COSMO_SOFT_SERIALPORTSCANNERUTILS_H
