add_subdirectory(flight)
add_subdirectory(import)
add_subdirectory(persistence)
add_subdirectory(telemetry)

#include(RingBuffer.tpp)
#target_sources(cosmo-backend PRIVATE "../../include/services/RingBuffer.tpp")
