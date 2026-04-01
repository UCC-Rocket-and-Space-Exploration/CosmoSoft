file(GLOB posix_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_library(PosixComms ${posix_sources})
