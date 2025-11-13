
if (WIN32)
    add_subdirectory(Windows)
endif ()
if(UNIX)
    add_subdirectory(Posix)
endif()

file(GLOB SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

target_link_libraries(cosmo-soft ${SOURCES})


