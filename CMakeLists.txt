cmake_minimum_required(VERSION 3.25)
project(cerebellum LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "" FORCE)

option(CEREBELLUM_BUILD_DATAPLANE "Build the DPDK dataplane" ON)
option(CEREBELLUM_BUILD_CONTROLPLANE "Build the userver controlplane" ON)

include(cmake/ProjectOptions.cmake)
include(cmake/CompilerWarnings.cmake)
include(cmake/Sanitizers.cmake)
include(cmake/Coverage.cmake)
include(cmake/Dependencies.cmake)

if(CEREBELLUM_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(libs)
if(CEREBELLUM_BUILD_DATAPLANE)
    add_subdirectory(dataplane)
endif()
if(CEREBELLUM_BUILD_CONTROLPLANE)
    add_subdirectory(controlplane)
endif()
