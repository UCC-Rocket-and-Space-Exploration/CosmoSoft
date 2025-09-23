cmake_minimum_required(VERSION 4.0)
project(cosmo-soft)

set(CMAKE_CXX_STANDARD 20)

add_executable(cosmos-soft
        src/main.cpp
        resources/persistence/FlightLogManager.cpp
        resources/comms/SocketComms.cpp
        resources/telemetry/TelemetryParser.cpp
        resources/flight/FlightCalculator.cpp)