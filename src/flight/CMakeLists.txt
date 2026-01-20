
file(GLOB SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_library(flight ${SOURCES})
target_include_directories(flight PUBLIC ${HEADERS})

target_link_libraries(cosmo-backend flight)
