# INTERFACE aggregate: no separate libcosmo-backend.a (avoids empty-archive ranlib warnings on stubs).
add_library(cosmo-backend INTERFACE)
target_include_directories(cosmo-backend INTERFACE "${HEADERS}")
target_link_libraries(cosmo-backend INTERFACE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Charts
)
target_compile_features(cosmo-backend INTERFACE cxx_std_20)

add_subdirectory(gateway/comms)
add_subdirectory(services/flight)
add_subdirectory(services/persistence)
add_subdirectory(services/telemetry)
add_subdirectory(services/import)
add_subdirectory(gui)

add_executable(cosmo-soft-bin
    main.cpp
    "${CMAKE_SOURCE_DIR}/assets/resources.qrc"
)
target_link_libraries(cosmo-soft-bin PRIVATE cosmo-backend)
# Default output dir is build/src (this file lives under src/). Flatten for single-config so ./build/cosmo-soft matches README.
set_target_properties(cosmo-soft-bin PROPERTIES OUTPUT_NAME "cosmo-soft")
if(CMAKE_CONFIGURATION_TYPES)
    set_target_properties(cosmo-soft-bin PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    )
else()
    set_target_properties(cosmo-soft-bin PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
endif()
