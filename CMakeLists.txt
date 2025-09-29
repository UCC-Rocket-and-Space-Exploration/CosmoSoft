cmake_minimum_required(VERSION 4.0)
project(cosmo-soft)

set(CMAKE_CXX_STANDARD 20)

add_executable(cosmos-soft
        src/main.cpp
        src/persistence/FlightLogManager.cpp
        src/comms/SerialCommsPosix.cpp
        src/telemetry/TelemetryParser.cpp
        src/flight/FlightCalculator.cpp
        resources/persistence/FileManager.cpp
        src/gui/FlightDataModel.cpp
        src/persistence/FileManager.cpp
        src/persistence/FileManager.cpp
        include/comms/SerialPortScanner.cpp
        src/comms/SerialCommsWindows.cpp
        src/comms/SerialPortScannerWindows.cpp
        src/comms/CommsFactory.cpp
    )