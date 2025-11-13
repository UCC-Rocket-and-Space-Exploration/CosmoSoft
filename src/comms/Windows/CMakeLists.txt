
message(${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(cosmo-soft ${CMAKE_CURRENT_SOURCE_DIR}/SerialCommsWindows.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/SerialPortScannerWindows.cpp)


