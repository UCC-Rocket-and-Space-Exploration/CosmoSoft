file(GLOB win_sources "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_library(WinComms ${win_sources})
target_link_libraries(WinComms headers)

