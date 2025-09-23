cmake_minimum_required(VERSION 4.0)
project(cosmo-soft)

set(CMAKE_CXX_STANDARD 20)

add_executable(cosmos-soft
        src/main.cpp
        src/persistence/FlightLogManager.cpp
        src/comms/SocketComms.cpp
        src/telemetry/TelemetryParser.cpp
        src/flight/FlightCalculator.cpp)