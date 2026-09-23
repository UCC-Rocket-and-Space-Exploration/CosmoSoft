cmake_minimum_required(VERSION 3.21)
project(cosmo-soft LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR
        "In-source builds are not supported. Create a separate build directory, e.g.:\n"
        "  cmake -S . -B build && cmake --build build")
endif()

# Optional Qt dependency; avoids noisy failed lookup when Vulkan SDK is not installed.
set(CMAKE_DISABLE_FIND_PACKAGE_WrapVulkanHeaders ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(HEADERS "${CMAKE_SOURCE_DIR}/include")


include(CTest)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets Charts Concurrent WebEngineWidgets WebChannel)

find_package(ZLIB REQUIRED) # for compressing
add_subdirectory(src)

#target_compile_features(cosmo-backend PUBLIC cxx_std_20)
add_subdirectory(tests)
#if(BUILD_TESTING)
#    add_subdirectory(tests)
#endif()

target_link_libraries(cosmo-soft-bin PRIVATE cosmo-backend)
target_compile_features(cosmo-soft-bin PRIVATE cxx_std_20)
