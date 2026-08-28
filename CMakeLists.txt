cmake_minimum_required(VERSION 3.21)
project(cosmo-soft LANGUAGES CXX)

set(CMAKE_PREFIX_PATH D:/Libraries/CppLibs/Qt/6.7.3/msvc2022_64)
set(Qt6_DIR D:/Libraries/CppLibs/Qt/6.7.3/msvc2022_64/lib/cmake/Qt6)

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

set(ZLIB_LIBRARY "D:\\Libraries\\CppLibs\\vcpkg\\packages\\zlib_x64-windows\\lib\\z.lib")
set(ZLIB_INCLUDE_DIR "D:\\Libraries\\CppLibs\\vcpkg\\packages\\zlib_x64-windows\\include")
include(D:\\Libraries\\CppLibs\\vcpkg\\scripts\\buildsystems\\vcpkg.cmake)

set(HEADERS "${CMAKE_SOURCE_DIR}/include")

include(CTest)

find_package(Qt6 6.7.3 REQUIRED COMPONENTS Core Gui Widgets Charts Concurrent WebEngineWidgets WebChannel)
message(STATUS "========== QT DEBUG ==========")
message(STATUS "Qt6_VERSION       = ${Qt6_VERSION}")
message(STATUS "Qt6_DIR            = ${Qt6_DIR}")
message(STATUS "Qt6Core_DIR        = ${Qt6Core_DIR}")
message(STATUS "CMAKE_PREFIX_PATH  = ${CMAKE_PREFIX_PATH}")
message(STATUS "CMAKE_CXX_COMPILER = ${CMAKE_CXX_COMPILER}")
message(STATUS "==============================")

find_package(ZLIB REQUIRED) # for compressing
add_subdirectory(src)

#target_compile_features(cosmo-backend PUBLIC cxx_std_20)

#add_subdirectory(tests)

#if(BUILD_TESTING)
#    add_subdirectory(tests)
#endif()

target_link_libraries(cosmo-soft-bin PRIVATE cosmo-backend)
target_compile_features(cosmo-soft-bin PRIVATE cxx_std_20)
