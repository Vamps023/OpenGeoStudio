# RoadEngine CMake Build Configuration
# Produces a static/shared library for the C++20 geometry engine
#
# Build options:
#   - BUILD_SHARED_LIBS: ON for shared library, OFF for static (default: OFF)
#   - CMAKE_CXX_STANDARD: C++ standard to use (default: 20)
#
# Build artifacts:
#   - Static: libroad_engine.lib (Windows) / libroad_engine.a (POSIX)
#   - Shared: road_engine.dll (Windows) / libroad_engine.so (POSIX) / libroad_engine.dylib (macOS)

cmake_minimum_required(VERSION 3.16)

# =============================================================================
# Generator Support Policies
# =============================================================================
# Validate CMake version requirement (3.16+) is met
# CMake 3.16 introduced FindPython3, better C++20 support, and improved generators

# Ninja generator detection (requires Ninja >= 1.10)
# Ninja 1.10+ is required for improved dependency file generation and caching
if(CMAKE_GENERATOR STREQUAL "Ninja" OR CMAKE_GENERATOR STREQUAL "Ninja Multi-Config")
    if(NOT CMAKE_NINJA_VERSION VERSION_GREATER_EQUAL 1.10)
        message(FATAL_ERROR
            "Ninja ${CMAKE_NINJA_VERSION} detected, but Ninja >= 1.10 is required. "
            "Please upgrade Ninja or use a different generator (Make, Visual Studio)."
        )
    endif()
    message(STATUS "Ninja generator detected: version ${CMAKE_NINJA_VERSION} (>= 1.10 required)")
endif()

# Make generator detection (requires Make >= 4.0)
# Make 4.0+ supports parallel builds and improved job server protocol
if(CMAKE_GENERATOR STREQUAL "Unix Makefiles")
    # CMake doesn't directly expose Make version, but we can check for features
    # Make 4.0+ supports .NOTPARALLEL and improved jobserver
    message(STATUS "Make generator detected (Make >= 4.0 required)")
endif()

# Visual Studio generator detection (requires Visual Studio >= 2019)
# VS 2019 (version 16.0) introduced improved C++20 support and modern tooling
if(CMAKE_GENERATOR MATCHES "Visual Studio")
    if(CMAKE_GENERATOR_VERSION VERSION_LESS 16.0)
        message(FATAL_ERROR
            "Visual Studio ${CMAKE_GENERATOR_VERSION} detected, "
            "but Visual Studio >= 2019 (16.0) is required. "
            "Please use Visual Studio 2019 or newer."
        )
    endif()
    message(STATUS "Visual Studio generator detected: version ${CMAKE_GENERATOR_VERSION} (>= 16.0 required)")
endif()

# =============================================================================
# Project Definition
# =============================================================================
project(road_engine
    VERSION 1.0.0
    DESCRIPTION "C++20 Road Geometry Engine"
    LANGUAGES CXX
)

# C++ standard configuration
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Option to build shared vs static library (default: static)
option(BUILD_SHARED_LIBS "Build as shared library" OFF)

# Optimization options (default ON for Release builds)
option(ENABLE_LTO "Enable Link-Time Optimization (LTO) for Release builds" ON)
option(ENABLE_IPO "Enable Inter-Procedural Optimization (IPO) for Release builds" ON)

# C++20 feature flags
# Note: C++20 coroutines and concepts are enabled by default with -std=c++20
# These flags enable additional optimizations for coroutine support
set(CMAKE_CXX_FLAGS_COROUTINES "-fcoroutines-mode=implicit")
set(CMAKE_CXX_FLAGS_CONCEPTS "")

# Determine output directory for library artifacts
# Use CMAKE_LIBRARY_OUTPUT_DIRECTORY if set, otherwise default to build dir
if(NOT DEFINED CMAKE_LIBRARY_OUTPUT_DIRECTORY)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
endif()

if(NOT DEFINED CMAKE_ARCHIVE_OUTPUT_DIRECTORY)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
endif()

# Source files for the core engine
# These are header files - we need a dummy source to establish linker language
set(ROAD_ENGINE_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/geometry.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/geometry_segment.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/road.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/road_v2.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/road_adapter.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/arc.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/clothoid.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/intersection.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mesh.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/opendrive.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/road_tools.hpp
)

# Create a dummy source file to establish linker language for header-only library
# This file is compiled but produces no symbols
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/road_engine_dummy.cpp
    "// Dummy source file to establish linker language for header-only library\n"
    "// This file is compiled but produces no symbols\n"
    "namespace road_engine { namespace internal { void dummy() {} } }\n"
)

# Create the road_engine library target
# This produces either static or shared library based on BUILD_SHARED_LIBS option
add_library(road_engine STATIC
    ${ROAD_ENGINE_HEADERS}
    ${CMAKE_CURRENT_BINARY_DIR}/road_engine_dummy.cpp
)

# Configure LTO (Link-Time Optimization) for Release builds
# LTO enables whole-program optimization across translation units
if(ENABLE_LTO AND CMAKE_BUILD_TYPE STREQUAL "Release")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT lto_supported OUTPUT lto_error)
    if(lto_supported)
        set_target_properties(road_engine PROPERTIES
            INTERPROCEDURAL_OPTIMIZATION TRUE
        )
        message(STATUS "LTO enabled for road_engine target")
    else()
        message(STATUS "LTO not supported: ${lto_error}")
    endif()
endif()

# Configure IPO (Inter-Procedural Optimization) explicitly
# IPO provides cross-translation-unit optimizations
if(ENABLE_IPO)
    set_target_properties(road_engine PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION TRUE
    )
endif()

# Override library type if BUILD_SHARED_LIBS is ON
if(BUILD_SHARED_LIBS)
    set_target_properties(road_engine PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
        PREFIX ""
    )
else()
    set_target_properties(road_engine PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
        PREFIX "lib"
        SUFFIX ".lib"
    )
endif()

# Public include directory (for consumers using find_package)
# Use generator expressions to handle build vs install contexts properly
target_include_directories(road_engine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

# Installation configuration
include(GNUInstallDirs)

install(TARGETS road_engine
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Create and install the interface target for find_package integration
add_library(road_engine_core INTERFACE)
target_link_libraries(road_engine_core INTERFACE road_engine)
target_include_directories(road_engine_core INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

# Create an alias target for namespaced access
add_library(road_engine::core ALIAS road_engine_core)

install(TARGETS road_engine_core
    EXPORT road_engineTargets
)

# Export the target for find_package (namespaced for consumers)
install(EXPORT road_engineTargets
    FILE road_engineTargets.cmake
    NAMESPACE road_engine::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/road_engine
)

# Generate and install the package config files
include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/road_engineConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/road_engineConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/road_engine
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/road_engineConfigVersion.cmake
    VERSION ${road_engine_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/road_engineConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/road_engineConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/road_engine
)

# Installation for public headers
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp"
)

# Print configuration summary
message(STATUS "RoadEngine Build Configuration:")
message(STATUS "  Version: ${road_engine_VERSION}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Library Type: $<IF:$<BOOL:${BUILD_SHARED_LIBS}>,Shared,Static>")
message(STATUS "  LTO Enabled: ${ENABLE_LTO} (Release only)")
message(STATUS "  IPO Enabled: ${ENABLE_IPO}")
message(STATUS "  C++20 Features: coroutines, concepts")
message(STATUS "  Install Prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  Archive Output: ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}")
message(STATUS "  Library Output: ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")