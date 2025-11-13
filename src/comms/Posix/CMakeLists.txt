<<<<<<< HEAD
file(GLOB win_sources "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_library(PosixComms ${win_sources})
target_link_libraries(PosixComms headers)
=======

target_link_libraries(cosmo-soft ${CMAKE_CURRENT_SOURCE_DIR}/SerialCommsPosix.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/SerialPortScannerPosix.cpp)
>>>>>>> d7e44e4 (Main CmakeLists files were created)
