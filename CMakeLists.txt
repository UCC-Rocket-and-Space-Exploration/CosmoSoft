cmake_minimum_required(VERSION 3.21)
project(cosmo-soft LANGUAGES CXX)
cmake_minimum_required(VERSION 3.21)
project(cosmo-soft LANGUAGES CXX)

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
        Qt::Charts
)
target_compile_features(cosmo-gui PUBLIC cxx_std_20)

add_executable(cosmo-soft
        src/main.cpp
)
target_link_libraries(cosmo-soft
        PRIVATE
        cosmo-gui
        cosmo-backend
)
