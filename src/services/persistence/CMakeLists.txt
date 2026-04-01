file(GLOB SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
list(APPEND SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../RingBuffer.cpp")
add_library(persistence OBJECT ${SOURCES})
target_include_directories(persistence PUBLIC ${HEADERS})

target_link_libraries(cosmo-backend INTERFACE persistence)
