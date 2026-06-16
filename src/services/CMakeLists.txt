add_subdirectory(flight)
add_subdirectory(import)
add_subdirectory(persistence)
add_subdirectory(telemetry)

target_sources(cosmo-backend PRIVATE "RingBuffer.tpp")
