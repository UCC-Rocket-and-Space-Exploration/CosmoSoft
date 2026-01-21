
file(GLOB SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_library(telemetry ${SOURCES})
target_include_directories(telemetry PUBLIC ${HEADERS})

target_link_libraries(cosmo-backend telemetry)


