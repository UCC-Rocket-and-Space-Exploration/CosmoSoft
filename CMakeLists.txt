cmake_minimum_required(VERSION 4.0)
project(cosmo-soft)

set(CMAKE_CXX_STANDARD 20)

find_package(Qt6 COMPONENTS
        Core
        Gui
        Widgets
        REQUIRED)

file(GLOB_RECURSE SOURCES "src/*.cpp")
add_executable(cosmo-soft ${SOURCES})
target_include_directories(cosmo-soft PRIVATE include)

target_link_libraries(cosmo-soft
        Qt::Core
        Qt::Gui
        Qt::Widgets
)
