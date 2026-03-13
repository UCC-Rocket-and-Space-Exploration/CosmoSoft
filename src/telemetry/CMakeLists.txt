
file(GLOB SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_library(teleme ${SOURCES})
target_include_directories(teleme PUBLIC include)

target_link_libraries(cosmo-backend teleme)


