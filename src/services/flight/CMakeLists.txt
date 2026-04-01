file(GLOB SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_library(flight OBJECT ${SOURCES})
target_include_directories(flight PUBLIC ${HEADERS})

target_link_libraries(cosmo-backend INTERFACE flight)
