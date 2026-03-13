file(GLOB posix_sources "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_library(PosixComms ${posix_sources})
target_include_directories(PosixComms PUBLIC include)

target_link_libraries(Comms PosixComms)

