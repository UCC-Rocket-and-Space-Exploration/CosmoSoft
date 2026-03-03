

target_include_directories(cosmo-soft PUBLIC "${HEADERS}")

target_include_directories(cosmo-soft INTERFACE ${CMAKE_SOURCE_DIR}/include)

add_library(cosmo-backend "")

add_subdirectory(gateway/comms)
add_subdirectory(services/flight)
add_subdirectory(services/persistence)
add_subdirectory(services/telemetry)
add_subdirectory(frontend)
#add_subdirectory(gui)

add_executable(cosmo-soft-bin main.cpp)

target_link_libraries(cosmo-soft
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
)

target_link_libraries(cosmo-soft-bin cosmo-backend)