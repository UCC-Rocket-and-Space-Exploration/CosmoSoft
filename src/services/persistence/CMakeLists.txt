file(GLOB SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_library(persistence ${SOURCES})
target_include_directories(persistence PUBLIC ${HEADERS})

target_link_libraries(cosmo-backend persistence)

