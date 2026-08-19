include(FetchContent)

# RSES-16: Catch2 backs the automated test target registered with CTest.
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
)
FetchContent_MakeAvailable(Catch2)

add_executable(cosmo-tests
    FileLoadCancellationTests.cpp
    EventLogFormattingTests.cpp
    FlightLogManagerAtomicExportTests.cpp
    FlightReplayControllerTests.cpp
    LineTelemetryPipelineTests.cpp
    LineTelemetryBatchMailboxTests.cpp
    MetricDisplayUnitTests.cpp
    MockFlightLinkTests.cpp
    SampleFileLoaderTests.cpp
    SerialPortScannerHardeningTests.cpp
    SerialWorkerLifecycleTests.cpp
    SkinLoaderTests.cpp
    "${CMAKE_SOURCE_DIR}/assets/resources.qrc"
        include/SerialWriter.h
)

target_include_directories(cosmo-tests PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(cosmo-tests PRIVATE
    Catch2::Catch2WithMain
    cosmo-backend
)

target_compile_definitions(cosmo-tests PRIVATE
    COSMO_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
include(Catch)
catch_discover_tests(cosmo-tests)
