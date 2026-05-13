cmake_minimum_required(VERSION 3.21)
project(cosmo-soft LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(HEADERS "${CMAKE_SOURCE_DIR}/include")

find_package(Qt6 6.9 REQUIRED COMPONENTS Core Gui Widgets Charts Concurrent)
add_library(cosmo-soft "")
add_subdirectory(src)

target_compile_features(cosmo-backend PUBLIC cxx_std_20)

