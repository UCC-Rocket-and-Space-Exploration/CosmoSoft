file(GLOB win_sources "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_library(PosixComms ${win_sources})
target_link_libraries(PosixComms headers)
