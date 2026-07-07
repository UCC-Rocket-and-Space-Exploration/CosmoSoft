add_library(video STATIC
    "${CMAKE_SOURCE_DIR}/assets/video/video_export.qrc"
    "${CMAKE_SOURCE_DIR}/include/services/video/ITerrainProvider.h"
    "${CMAKE_SOURCE_DIR}/include/services/video/MapTilerTerrainProvider.h"
    "${CMAKE_SOURCE_DIR}/include/services/video/VideoExportService.h"
    FlightAnalyzer.cpp
    MapTilerTerrainProvider.cpp
    VideoExportService.cpp
    VideoExportTypes.cpp
    VideoTimeline.cpp
)

target_include_directories(video PUBLIC "${HEADERS}")
target_link_libraries(video
    PUBLIC
    Qt6::Core          # QObject orchestration and deterministic timing.
    Qt6::Gui           # QImage terrain assets and frame composition.
    Qt6::Multimedia    # H.264 MP4 frame-input encoding with backpressure.
    Qt6::Network       # Bounded MapTiler terrain preflight and cache.
    Qt6::Qml           # Bundled cinematic scene loading.
    Qt6::Quick         # Exact-size off-screen scene capture.
    Qt6::Quick3D       # Native cross-platform cinematic terrain rendering.
)

target_link_libraries(cosmo-backend video)
