add_library(cosmo-backend ""
        ../include/services/interfaces/IFramer.h
        ../include/services/interfaces/IFrameDecoder.h
        ../include/services/telemetry/framers/CsvFramer.h
        services/telemetry/framers/CsvFramer.cpp
        ../include/shared/exceptions/SerialPortNotOpened.h
        ../include/services/telemetry/FrameDecoderVault.h
        ../include/services/telemetry/frame_decoders/CsvFrameDecoder.h
        services/telemetry/FrameDecoderVault.cpp
        ../include/services/telemetry/frame_decoders/TeleMegaKalmanFrameDecoder.h
        ../include/services/telemetry/frame_decoders/TeleMiniV1FrameDecoder.h
        shared/abstraction/Worker.cpp
        services/telemetry/frame_decoders/CsvFrameDecoder.cpp
        services/telemetry/frame_decoders/TeleMegaKalmanFrameDecoder.cpp
        services/telemetry/frame_decoders/TeleMiniV1FrameDecoder.cpp
        ../include/services/telemetry/framers/AltosFramer.h
        services/telemetry/framers/AltosFramer.cpp
        ../include/shared/exceptions/SerialTimeout.h
        ../include/shared/exceptions/UnhandledSerialException.h
        ../include/shared/AltosPacketTypes.h
        ../include/shared/exceptions/IncorrectAltosPacketType.h
        ../include/services/telemetry/frame_decoders/TeleMiniV3FrameDecoder.h
        ../include/services/telemetry/frame_decoders/TeleMegaImuFrameDecoder.h
        ../include/services/telemetry/frame_decoders/GpsFrameDecoder.h
        ../include/services/telemetry/frame_decoders/ConfigFrameDecoder.h
        services/telemetry/frame_decoders/TeleMegaImuFrameDecoder.cpp
        services/telemetry/frame_decoders/TeleMiniV3FrameDecoder.cpp
        services/telemetry/frame_decoders/GpsFrameDecoder.cpp
        services/telemetry/frame_decoders/ConfigFrameDecoder.cpp
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
