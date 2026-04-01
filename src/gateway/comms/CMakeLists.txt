if(WIN32)
    message(STATUS "Adding Windows comms sources")
    add_subdirectory(Windows)
    target_include_directories(WinComms PUBLIC "${HEADERS}")
    target_link_libraries(cosmo-backend INTERFACE WinComms)
endif()

if(UNIX)
    add_subdirectory(Posix)
    target_include_directories(PosixComms PUBLIC "${HEADERS}")
    target_link_libraries(cosmo-backend INTERFACE PosixComms)
endif()

file(GLOB SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_library(comms ${SOURCES})
target_include_directories(comms PUBLIC "${HEADERS}")

target_link_libraries(cosmo-backend INTERFACE comms)
