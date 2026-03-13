
file(GLOB SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_library(Flight ${SOURCES})
target_include_directories(Flight PUBLIC include)

target_link_libraries(cosmo-backend Flight)

