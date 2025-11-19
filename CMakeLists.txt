cmake_minimum_required(VERSION 3.21)
project(cosmo-soft LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

#file(GLOB_RECURSE SOURCES "src/*.cpp")
#add_executable(cosmo-soft ${SOURCES})
add_library(headers INTERFACE)
target_include_directories(headers INTERFACE ${CMAKE_SOURCE_DIR}/include)

add_subdirectory(src)
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets Charts)

set(BACKEND_SOURCES
        src/flight/FlightCalculator.cpp
        src/comms/SerialPortScannerPosix.cpp
        src/comms/SerialPortScannerFactory.cpp
        src/comms/SerialWorker.cpp
        src/comms/SerialPortScannerWindows.cpp
        src/comms/SerialCommsPosix.cpp
        src/comms/SerialCommsWindows.cpp
        src/comms/CommsFactory.cpp
        src/persistence/FlightLogManager.cpp
        src/persistence/FileManager.cpp
        src/telemetry/TelemetryParser.cpp
)

add_library(cosmo-backend STATIC ${BACKEND_SOURCES})
target_include_directories(cosmo-backend
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/comms>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/flight>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/persistence>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/telemetry>
        $<INSTALL_INTERFACE:include/comms>
        $<INSTALL_INTERFACE:include/flight>
        $<INSTALL_INTERFACE:include/persistence>
        $<INSTALL_INTERFACE:include/telemetry>
)
target_compile_features(cosmo-backend PUBLIC cxx_std_20)

set(GUI_SOURCES
        include/gui/MainWindow.h
        include/gui/pages/MonitoringPage.h
        include/gui/pages/FlightDataPage.h
        include/gui/pages/SettingsPage.h
        include/gui/pages/ChartPage.h
        src/gui/FlightDataModel.cpp
        src/gui/MainWindow.cpp
        src/gui/pages/MonitoringPage.cpp
        src/gui/pages/FlightDataPage.cpp
        src/gui/pages/SettingsPage.cpp
        src/gui/pages/ChartPage.cpp
        assets/resources.qrc
)

add_library(cosmo-gui STATIC ${GUI_SOURCES})
target_include_directories(cosmo-gui
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/gui>
        $<INSTALL_INTERFACE:include/gui>
)
target_link_libraries(cosmo-gui
        PUBLIC
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
