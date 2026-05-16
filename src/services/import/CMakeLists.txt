add_library(import STATIC "SampleFileLoader.cpp")
target_include_directories(import PUBLIC ${HEADERS})
target_link_libraries(import PUBLIC telemetry)

target_link_libraries(cosmo-backend import)
