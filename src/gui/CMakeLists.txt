
file(GLOB SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_library(Gui ${SOURCES})

target_include_directories(Gui PUBLIC ../../include)

target_link_libraries(cosmo-gui Gui)

