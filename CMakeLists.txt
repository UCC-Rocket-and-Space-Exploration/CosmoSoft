cmake_minimum_required(VERSION 3.21)
project(cosmo-soft LANGUAGES CXX)
#qt_add_resources(PROJECT_RESOURCES assets/resources.qrc)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets)

add_library(cosmo-backend)
add_library(cosmo-gui assets/resources.qrc)
target_compile_features(cosmo-backend PUBLIC cxx_std_20)

target_include_directories(cosmo-backend PUBLIC include)
target_include_directories(cosmo-gui PUBLIC include)
add_subdirectory(src)

target_link_libraries(cosmo-gui
#        PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
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
