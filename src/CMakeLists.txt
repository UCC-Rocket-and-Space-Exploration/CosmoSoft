add_library(cosmo-backend "")
target_include_directories(cosmo-backend PUBLIC "${HEADERS}")

target_link_libraries(cosmo-backend
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Charts
    ZLIB::ZLIB
)

add_subdirectory(gateway/comms)
add_subdirectory(services)
add_subdirectory(gui)

add_executable(cosmo-soft-bin
    main.cpp
    "${CMAKE_SOURCE_DIR}/assets/resources.qrc"
)

set_target_properties(cosmo-soft-bin PROPERTIES
    OUTPUT_NAME "cosmo-soft"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
