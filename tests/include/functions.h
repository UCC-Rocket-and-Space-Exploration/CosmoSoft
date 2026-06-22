#pragma once
#include <iostream>
#include "catch2/catch_test_macros.hpp"

inline bool excepted_actual_content_equal_except_last_char(char* expected, std::vector<uint8_t> actual) {
    for (int i = 0; i < std::strlen(expected) - 1; i++) {
        auto actual_char = (char)actual[i];

        REQUIRE(expected[i] == actual_char);
    }
    return true;
}
