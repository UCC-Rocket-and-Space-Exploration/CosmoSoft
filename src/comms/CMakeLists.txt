add_library(Comms ${SOURCES})

if (WIN32)
    add_subdirectory(Windows)
endif ()
if(UNIX)
    add_subdirectory(Posix)
endif()

#file(GLOB SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

target_include_directories(Comms PUBLIC include)

target_link_libraries(cosmo-backend Comms)

#target_include_directories(cosmo-soft PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)

