# ═══════════════════════════════════════════════════════════
# OpenGeoStudio — Top-Level CMakeLists.txt
# ═══════════════════════════════════════════════════════════
#
# Native C++/Qt 6 desktop application.
# Builds the OpenGeoStudio executable linking against the
# C++ road engine (header-only) and LaneMaker (optional).
#
# Requirements:
#   - CMake 3.16+
#   - C++20 compiler (MSVC 2019+, GCC 10+, Clang 12+)
#   - Qt 6 (Core, Gui, Widgets) — installed via Qt installer or system package
#   - Ninja (recommended generator)
#
# Build:
#   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#
# The POST_BUILD step runs windeployqt to collect Qt DLLs
# into the build directory for a portable Windows deployment.
# ═══════════════════════════════════════════════════════════

cmake_minimum_required(VERSION 3.16)

project(OpenGeoStudio
    VERSION 0.1.0
    DESCRIPTION "Native C++/Qt 6 desktop application for geospatial road design"
    LANGUAGES CXX
)

# ─── C++ Standard ────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ─── Qt 6 ────────────────────────────────────────────────────
# Qt 6 is expected to be installed via the Qt installer or system
# package manager. Set CMAKE_PREFIX_PATH to point to your Qt install:
#   cmake -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.8.0/msvc2022_64"
#
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

# Enable Qt MOC/UIC/RCC auto-processing
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# ─── Road Engine Library (header-only, requires nlohmann-json) ──
# The road engine is a header-only C++20 library. We create a
# static library target with a dummy source to establish the
# linker language, matching the existing road_engine/CMakeLists.txt.
#
# Source: src/engine/road/ (Codebase A + B from the audit)
# Public API: src/engine/road/road_engine/public/road_engine.hpp
#
# Note: road_engine.hpp uses nlohmann::json for serialization, so
# nlohmann-json is a required dependency.
#
find_package(nlohmann_json CONFIG REQUIRED)

set(ROAD_ENGINE_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/road/geometry.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/road/geometry_segment.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/road/road.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/road/road_v2.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/road/road_adapter.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/road/arc.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/road/clothoid.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/road/intersection.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/road/mesh.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/road/opendrive.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/road/road_tools.hpp
)

# Dummy source to establish linker language for header-only library
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/road_engine_dummy.cpp
    "// Dummy source for header-only road engine library\n"
    "namespace road_engine { namespace internal { void dummy() {} } }\n"
)

add_library(road_engine STATIC
    ${ROAD_ENGINE_HEADERS}
    ${CMAKE_CURRENT_BINARY_DIR}/road_engine_dummy.cpp
)

target_include_directories(road_engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/road
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/road/road_engine/public
)

target_link_libraries(road_engine PUBLIC nlohmann_json::nlohmann_json)

# Alias target for namespaced access (matches existing convention)
add_library(road_engine::core ALIAS road_engine)

# ─── LaneMaker Library (optional — requires CGAL, Boost, spdlog) ──
# LaneMaker has heavy dependencies (CGAL, GMP, MPFR, Boost, spdlog).
# It is not needed for the initial Qt skeleton. Enable with:
#   cmake -B build -DENABLE_LANEMAKER=ON
#
option(ENABLE_LANEMAKER "Build LaneMaker library (requires CGAL, Boost, spdlog)" OFF)

if(ENABLE_LANEMAKER)
    find_package(CGAL REQUIRED)
    find_package(Boost REQUIRED)
    find_package(spdlog REQUIRED)

    # LaneMaker sources (Codebase C from the audit)
    file(GLOB LANEMAKER_XODR_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/xodr/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/xodr/*.h
    )
    file(GLOB LANEMAKER_LIBOPENDRIVE_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/libOpenDRIVE/src/*.cpp
    )

    add_library(lanemaker STATIC
        ${LANEMAKER_XODR_SOURCES}
        ${LANEMAKER_LIBOPENDRIVE_SOURCES}
    )

    target_include_directories(lanemaker
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/xodr
            ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/libOpenDRIVE/include
            ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/libOpenDRIVE/thirdparty
    )

    target_link_libraries(lanemaker
        PUBLIC
            CGAL::CGAL
            Boost::boost
            spdlog::spdlog
            road_engine::core
    )

    add_library(lanemaker::core ALIAS lanemaker)
endif()

# ─── Plugin API (header-only, requires Qt) ───────────────────
add_library(plugin_api INTERFACE)
target_include_directories(plugin_api INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/plugin
)
target_link_libraries(plugin_api INTERFACE Qt6::Core)

# ─── Main Application Executable ─────────────────────────────
add_executable(OpenGeoStudio
    src/app/main.cpp
)

target_link_libraries(OpenGeoStudio PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    road_engine::core
    plugin_api
)

if(ENABLE_LANEMAKER)
    target_link_libraries(OpenGeoStudio PRIVATE lanemaker::core)
endif()

# Set output directory for portable deployment
set_target_properties(OpenGeoStudio PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/deploy
)

# ─── Windows Deployment: windeployqt ─────────────────────────
# After building, windeployqt copies all required Qt DLLs,
# plugins, and platform files into the deploy directory,
# creating a portable folder that runs on any Windows 10/11
# machine without installing Qt.
#
if(WIN32)
    # Find windeployqt.exe — it lives in the Qt bin directory
    # alongside qmake.exe
    find_program(WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt
        HINTS
            ${QT_BIN_DIR}
            ${_qt6Core_install_prefix}/bin
            $ENV{QT_BIN_DIR}
            "C:/Qt/6.8.0/msvc2022_64/bin"
            "C:/Qt/6.7.0/msvc2022_64/bin"
            "C:/Qt/6.6.0/msvc2022_64/bin"
        REQUIRED
    )

    message(STATUS "windeployqt found: ${WINDEPLOYQT_EXECUTABLE}")

    add_custom_command(TARGET OpenGeoStudio POST_BUILD
        COMMAND
            ${WINDEPLOYQT_EXECUTABLE}
            --release
            --no-translations
            --no-opengl-sw
            --no-system-d3d-compiler
            --compiler-runtime
            "$<TARGET_FILE:OpenGeoStudio>"
        COMMENT "Running windeployqt to collect Qt runtime dependencies..."
    )
endif()

# ─── doctest Tests (C++ road engine unit tests) ──────────────
option(BUILD_TESTS "Build C++ doctest tests" ON)

if(BUILD_TESTS)
    enable_testing()

    add_executable(geometry_segment_tests
        src/engine/road/geometry_segment_tests.cpp
    )

    target_link_libraries(geometry_segment_tests PRIVATE
        road_engine::core
    )

    # MSVC needs /EHa for structured exception handling (LaneMaker SEH guard)
    # /bigobj needed because the test file has many sections
    if(MSVC)
        target_compile_options(geometry_segment_tests PRIVATE /EHa /bigobj)
    endif()

    include(CTest)
    add_test(NAME geometry_segment_tests
        COMMAND geometry_segment_tests
    )
endif()

# ─── Print Configuration Summary ─────────────────────────────
message(STATUS "")
message(STATUS "═══ OpenGeoStudio Build Configuration ═══")
message(STATUS "  Version:           ${PROJECT_VERSION}")
message(STATUS "  C++ Standard:      ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type:        ${CMAKE_BUILD_TYPE}")
message(STATUS "  Generator:         ${CMAKE_GENERATOR}")
message(STATUS "  Qt6 Found:         ${Qt6_FOUND}")
message(STATUS "  LaneMaker:         ${ENABLE_LANEMAKER}")
message(STATUS "  Tests:             ${BUILD_TESTS}")
if(WIN32)
    message(STATUS "  windeployqt:       ${WINDEPLOYQT_EXECUTABLE}")
endif()
    message(STATUS "  Deploy Dir:        ${CMAKE_BINARY_DIR}/deploy")
message(STATUS "")
