<<<<<<< HEAD
add_library(cosmo-backend "")
target_include_directories(cosmo-backend PUBLIC "${HEADERS}")

target_link_libraries(cosmo-backend
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Charts
)

add_subdirectory(gateway)
add_subdirectory(services/flight)
add_subdirectory(services/persistence)
add_subdirectory(services/telemetry)
add_subdirectory(gui)

add_executable(cosmo-soft-bin
    main.cpp
    "${CMAKE_SOURCE_DIR}/assets/resources.qrc"
)

target_link_libraries(cosmo-soft-bin PRIVATE cosmo-backend)
=======


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
>>>>>>> e7d53e9 (branching off from refactor to work on parsing thread)
