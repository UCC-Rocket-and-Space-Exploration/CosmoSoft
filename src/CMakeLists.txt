
add_library(cosmo-backend ""
        Domain/AngularVelocityCoordinates.h
        Domain/AccelerationCoordinates.h
        Domain/LocationCoordinates.h
        Domain/FlightSample.h)

target_include_directories(cosmo-backend PUBLIC "${HEADERS}")

#target_include_directories(cosmo-backend INTERFACE ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(cosmo-backend
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
)

add_subdirectory(Gateway/comms)
add_subdirectory(Services/flight)
add_subdirectory(Services/persistence)
add_subdirectory(Services/telemetry)
add_subdirectory(gui)
#add_subdirectory(gui)

add_executable(cosmo-soft-bin main.cpp)
target_link_libraries(cosmo-soft-bin cosmo-backend)

