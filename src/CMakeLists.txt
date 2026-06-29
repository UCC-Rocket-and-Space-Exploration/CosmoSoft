add_library(cosmo-backend ""
        ../include/services/interfaces/IFramer.h
        ../include/services/interfaces/IFrameDecoder.h
        ../include/services/telemetry/framers/CsvFramer.h
        services/telemetry/framers/CsvFramer.cpp
        ../include/shared/SerialPortNotOpened.h
        ../include/services/telemetry/FrameDecoderFactory.h
        ../include/services/telemetry/frame_decoders/CsvFrameDecoder.h
        services/telemetry/FrameDecoderFactory.cpp
        ../include/services/telemetry/frame_decoders/TeleMegaFrameDecoder.h
        ../include/services/telemetry/frame_decoders/TeleMiniFrameDecoder.h
        shared/abstraction/Worker.cpp
        services/telemetry/frame_decoders/CsvFrameDecoder.cpp
        services/telemetry/frame_decoders/TeleMegaDecoder.cpp
        services/telemetry/frame_decoders/TeleMiniFrameDecoder.cpp
        ../include/services/telemetry/framers/TeleFramer.h
        services/telemetry/framers/TeleFramer.cpp
        ../include/shared/SerialTimeout.h
        ../include/shared/UnhandledSerialException.h
)
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
add_subdirectory(shared)
#add_subdirectory(gui)

add_executable(cosmo-soft-bin
    main.cpp
    "${CMAKE_SOURCE_DIR}/assets/resources.qrc"
)
