
if (WIN32)
    add_subdirectory(Windows)
    target_link_libraries(cosmo-soft WinComms)
endif ()
if(UNIX)
    add_subdirectory(Posix)
    target_link_libraries(cosmo-soft PosixComms)
endif()

file(GLOB SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_library(comms ${SOURCES})
target_link_libraries(comms PUBLIC headers)
target_link_libraries(cosmo-soft comms)


