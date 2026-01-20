
if (WIN32)
    message(STATUS "ADDING WIN FILES")
    add_subdirectory(Windows)
#    message(STATUS "${HEADERS}")
    target_include_directories(WinComms PUBLIC "${HEADERS}")
    target_link_libraries(cosmo-backend WinComms)

#    if (HAVE_MYLIB_H STREQUAL "TRUE")
#
#    endif ()
endif ()

#
#set(CODE "
##include <glob.h>
#int main() { return 0; }
#")

#check_source_compiles(C "
#${CODE}"
#        HAVE_GLOB_H
#)
if(UNIX)
    #ifdef glob.h
    add_subdirectory(Posix)
    target_include_directories(PosixComms PUBLIC "${HEADERS}")
    target_link_libraries(cosmo-backend PosixComms)

    #endif
endif()

file(GLOB SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_library(comms ${SOURCES})
target_include_directories(comms PUBLIC "${HEADERS}")

target_link_libraries(cosmo-backend comms)
