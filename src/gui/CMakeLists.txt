
file(GLOB SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB PAGES "${CMAKE_CURRENT_SOURCE_DIR}/pages/*.cpp")

add_library(gui ${SOURCES} ${PAGES})
target_include_directories(gui PUBLIC ${HEADERS})

target_link_libraries(gui
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
)

target_link_libraries(cosmo-backend gui)

