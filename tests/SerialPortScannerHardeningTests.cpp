#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gateway/comms/detail/SerialPortScannerUtils.h"

TEST_CASE("Serial scanner recognizes supported POSIX device names", "[serial][scanner]") {
    using cosmo::serial::detail::is_supported_posix_port_name;

    REQUIRE(is_supported_posix_port_name("ttyUSB0"));
    REQUIRE(is_supported_posix_port_name("ttyACM12"));
    REQUIRE(is_supported_posix_port_name("ttyS1"));
    REQUIRE(is_supported_posix_port_name("cu.usbmodem101"));
    REQUIRE(is_supported_posix_port_name("tty.usbserial-A50285BI"));

    REQUIRE_FALSE(is_supported_posix_port_name("tty"));
    REQUIRE_FALSE(is_supported_posix_port_name("cu."));
    REQUIRE_FALSE(is_supported_posix_port_name("pts/1"));
    REQUIRE_FALSE(is_supported_posix_port_name("random-device"));
}

TEST_CASE("Serial scanner sorting is deterministic and removes duplicates", "[serial][scanner]") {
    using cosmo::serial::detail::sort_and_deduplicate_ports;

    const std::vector<std::string> result = sort_and_deduplicate_ports(
        {"/dev/ttyUSB2", "/dev/cu.alpha", "/dev/ttyUSB2", "/dev/ttyACM0"});

    REQUIRE(result == std::vector<std::string>{
                          "/dev/cu.alpha",
                          "/dev/ttyACM0",
                          "/dev/ttyUSB2",
                      });
    REQUIRE(std::is_sorted(result.cbegin(), result.cend()));
}

TEST_CASE("Windows COM names are canonicalized for display", "[serial][scanner]") {
    using cosmo::serial::detail::canonical_windows_port_name;
    using cosmo::serial::detail::sort_and_deduplicate_ports;

    REQUIRE(canonical_windows_port_name("COM10") == "COM10");
    REQUIRE(canonical_windows_port_name("com42") == "COM42");
    REQUIRE(canonical_windows_port_name(R"(\\.\Com17)") == "COM17");
    REQUIRE(canonical_windows_port_name("virtual-port") == "virtual-port");
    REQUIRE(canonical_windows_port_name(R"(\\.\virtual-port)") == R"(\\.\virtual-port)");

    std::vector<std::string> registered_ports{"com10", "COM2", R"(\\.\COM10)"};
    std::transform(
        registered_ports.begin(),
        registered_ports.end(),
        registered_ports.begin(),
        canonical_windows_port_name);
    REQUIRE(sort_and_deduplicate_ports(std::move(registered_ports))
            == std::vector<std::string>{"COM10", "COM2"});
}

TEST_CASE("Windows COM paths are safe for COM10 and later", "[serial][scanner]") {
    using cosmo::serial::detail::normalize_windows_port_path;

    REQUIRE(normalize_windows_port_path("COM1") == R"(\\.\COM1)");
    REQUIRE(normalize_windows_port_path("COM10") == R"(\\.\COM10)");
    REQUIRE(normalize_windows_port_path("com128") == R"(\\.\COM128)");
    REQUIRE(normalize_windows_port_path(R"(\\.\Com42)") == R"(\\.\COM42)");
    REQUIRE(normalize_windows_port_path("/dev/ttyUSB0") == "/dev/ttyUSB0");
    REQUIRE(normalize_windows_port_path("COMx") == "COMx");
}

TEST_CASE("Windows registry strings require valid bounds and termination", "[serial][scanner]") {
    using cosmo::serial::detail::checked_windows_registry_string;

    constexpr std::array<unsigned char, 6> VALID_VALUE{'C', 'O', 'M', '1', '0', '\0'};
    REQUIRE(checked_windows_registry_string(VALID_VALUE, VALID_VALUE.size()) == "COM10");
    REQUIRE_FALSE(checked_windows_registry_string(VALID_VALUE, 0).has_value());
    REQUIRE_FALSE(checked_windows_registry_string(VALID_VALUE, VALID_VALUE.size() + 1).has_value());
    REQUIRE_FALSE(checked_windows_registry_string(VALID_VALUE, VALID_VALUE.size() - 1).has_value());

    constexpr std::array<unsigned char, 6> EMBEDDED_NULL{'C', 'O', '\0', '1', '0', '\0'};
    REQUIRE_FALSE(checked_windows_registry_string(EMBEDDED_NULL, EMBEDDED_NULL.size()).has_value());

    constexpr std::array<unsigned char, 1> EMPTY_VALUE{'\0'};
    REQUIRE_FALSE(checked_windows_registry_string(EMPTY_VALUE, EMPTY_VALUE.size()).has_value());
}
