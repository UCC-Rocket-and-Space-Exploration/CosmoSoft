
if (WIN32)
    message(STATUS "ADDING WIN FILES")
    add_subdirectory(Windows)
    target_include_directories(WinComms PUBLIC "${HEADERS}")
    target_link_libraries(cosmo-backend WinComms)

endif ()

if(UNIX)
    add_subdirectory(Posix)
    target_include_directories(PosixComms PUBLIC "${HEADERS}")
    target_link_libraries(cosmo-backend PosixComms)

endif()

file(GLOB SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_library(comms ${SOURCES}
        SerialFramerWorker.cpp)
target_include_directories(comms PUBLIC "${HEADERS}")

target_link_libraries(cosmo-backend comms)
