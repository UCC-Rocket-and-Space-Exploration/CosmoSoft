
file(GLOB SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_library(Persistence ${SOURCES})
target_include_directories(Persistence PUBLIC include)

target_link_libraries(cosmo-backend Persistence)

