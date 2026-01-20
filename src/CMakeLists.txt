
add_library(cosmo-backend "")

target_include_directories(cosmo-backend PUBLIC "${HEADERS}")

#target_include_directories(cosmo-backend INTERFACE ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(cosmo-backend
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
)


add_subdirectory(comms)
add_subdirectory(flight)
add_subdirectory(persistence)
add_subdirectory(telemetry)
add_subdirectory(gui)
#add_subdirectory(gui)

add_executable(cosmo-soft-bin main.cpp)
target_link_libraries(cosmo-soft-bin cosmo-backend)

