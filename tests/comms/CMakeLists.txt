file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
list(FILTER SOURCES EXCLUDE REGEX ".*/Windows/.*$")

target_sources(cosmo-tests PRIVATE ${SOURCES})
if (WIN32)
    message(STATUS "Adding win tests")
    file(GLOB WIN_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Windows/*.cpp")
    target_sources(cosmo-tests PRIVATE "${WIN_SOURCES}")
endif()
