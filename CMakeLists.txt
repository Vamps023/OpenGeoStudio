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
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)

# Enable Qt MOC/UIC/RCC auto-processing
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# ─── MapLibre Native Qt (Phase 2c rendering spike) ───────────
# Built from source at D:/git/maplibre-native-qt and installed to
# D:/git/maplibre-native-qt/install. Provides QMapLibre::Widgets
# (GLWidget) for 2D map rendering with Esri World Imagery tiles.
#
# If MapLibre is not found, the map viewport is disabled and the
# app falls back to a placeholder widget.
#
option(ENABLE_MAPLIBRE "Enable MapLibre Native Qt map viewport" ON)

if(ENABLE_MAPLIBRE)
    find_package(QMapLibre COMPONENTS Widgets QUIET
        HINTS
            ${CMAKE_CURRENT_SOURCE_DIR}/../maplibre-native-qt/install
            $ENV{QMAPLIBRE_DIR}
            "D:/git/maplibre-native-qt/install"
    )
    if(QMapLibre_FOUND)
        message(STATUS "MapLibre Native Qt found: ${QMapLibre_DIR}")
    else()
        message(WARNING "MapLibre Native Qt not found — map viewport disabled. "
                        "Build maplibre-native-qt first: https://github.com/maplibre/maplibre-native-qt")
    endif()
endif()

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
find_package(TIFF REQUIRED)
find_package(PNG REQUIRED)

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

# ─── LaneMaker Library (requires CGAL, Boost, spdlog, cereal, Qt6) ──
# LaneMaker is now the authoritative road model.
# It is always built (not optional) since the Road Studio depends on it.
#
find_package(CGAL REQUIRED)
find_package(Boost REQUIRED)
find_package(spdlog REQUIRED)
find_package(cereal CONFIG REQUIRED)

# LaneMaker XODR sources
file(GLOB LANEMAKER_XODR_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/xodr/*.cpp
)
# LaneMaker libOpenDRIVE sources
file(GLOB LANEMAKER_LIBOPENDRIVE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/libOpenDRIVE/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/libOpenDRIVE/src/Geometries/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/libOpenDRIVE/src/Geometries/Spiral/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/libOpenDRIVE/thirdparty/pugixml/pugixml.cpp
)
# LaneMaker engine sources (OpenGL rendering, buffers, camera, spatial indexer)
file(GLOB LANEMAKER_ENGINE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/engine/*.cpp
)
# LaneMaker UI sources (road drawing sessions, road graphics, action manager)
set(LANEMAKER_UI_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/ui/road_graphics.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/ui/road_drawing.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/ui/road_creation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/ui/lane_creation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/ui/road_destruction.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/ui/road_modification.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/ui/road_overlaps.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/ui/action_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/ui/touch_controller.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/ui/util.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/ui/main_widget.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/ui/main_window.cpp
)
# LaneMaker widget sources
set(LANEMAKER_WIDGET_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/widgets/AnimatedPopupDialog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/widgets/DrawOptionDialog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/widgets/LaneConfigWidget.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/widgets/replay_window.cpp
)
# LaneMaker util sources
set(LANEMAKER_UTIL_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/util/preference.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/util/stats.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/util/multi_segment.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/util/triangulation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/util/label_with_link.cpp
)
# LaneMaker traffic sources
set(LANEMAKER_TRAFFIC_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/traffic/vehicle.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/traffic/vehicle_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/traffic/signal.cpp
)
# LaneMaker test/validation sources
set(LANEMAKER_TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/test/validation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/test/junction_validation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/test/road_validation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/test/randomization_utils.cpp
)

# Qt resource files for LaneMaker — compiled into the main executable
# (not the static library) to ensure they are not stripped by the linker
qt_add_resources(LANEMAKER_QRC_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/ui/images.qrc
)
qt_add_resources(LANEMAKER_SHADER_QRC_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/engine/shaders.qrc
)

add_library(lanemaker STATIC
    ${LANEMAKER_XODR_SOURCES}
    ${LANEMAKER_LIBOPENDRIVE_SOURCES}
    ${LANEMAKER_ENGINE_SOURCES}
    ${LANEMAKER_UI_SOURCES}
    ${LANEMAKER_WIDGET_SOURCES}
    ${LANEMAKER_UTIL_SOURCES}
    ${LANEMAKER_TRAFFIC_SOURCES}
    ${LANEMAKER_TEST_SOURCES}
)

# Ensure AUTOMOC processes LaneMaker headers with Q_OBJECT
set_target_properties(lanemaker PROPERTIES AUTOMOC ON)

target_include_directories(lanemaker
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/xodr
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/libOpenDRIVE/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/libOpenDRIVE/thirdparty
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/engine
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/ui
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/widgets
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/util
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/traffic
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/test
)

# NOMINMAX to prevent Windows min/max macros conflicting with std::min/std::max
# G_TEST is not defined — we're not building LaneMaker's own tests
target_compile_definitions(lanemaker PRIVATE NOMINMAX)
target_compile_definitions(lanemaker PUBLIC SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO)

target_link_libraries(lanemaker
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::OpenGL
        Qt6::OpenGLWidgets
        Qt6::Network
        CGAL::CGAL
        Boost::boost
        spdlog::spdlog
        cereal::cereal
        road_engine::core
)

if(QMapLibre_FOUND)
    target_link_libraries(lanemaker PUBLIC QMapLibre::Widgets)
    target_compile_definitions(lanemaker PUBLIC HAVE_MAPLIBRE=1)
endif()

add_library(lanemaker::core ALIAS lanemaker)

# ─── Plugin API (header-only, requires Qt) ───────────────────
add_library(plugin_api INTERFACE)
target_include_directories(plugin_api INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/plugin
)
target_link_libraries(plugin_api INTERFACE Qt6::Core)

# ─── Main Application Executable ─────────────────────────────
set(APP_SOURCES
    src/app/main.cpp
    # Core services
    src/core/ApplicationContext.cpp
    src/core/events/EventBus.cpp
    src/core/project/ProjectManager.cpp
    src/core/workspace/WorkspaceManager.cpp
    # UI - Home
    src/ui/home/HomeWidget.cpp
    # UI - Road Studio (LaneMaker's MainWindow is used directly)
    src/ui/roadstudio/RoadStudioWidget.cpp
    # UI - Train Studio
    src/ui/trainstudio/TrainStudioStore.cpp
    src/ui/trainstudio/TrainViewport2D.cpp
    src/ui/trainstudio/TrainStudioWidget.cpp
    # UI - Terrain Studio
    src/ui/terrain/TerrainStore.cpp
    src/ui/terrain/TerrainViewport.cpp
    src/ui/terrain/ExportPanel.cpp
    src/ui/terrain/ExportEngine.cpp
    src/ui/terrain/TerrainStudioWidget.cpp
)

if(QMapLibre_FOUND)
    list(APPEND APP_SOURCES
        src/app/MapViewportWidget.hpp
        src/app/MapViewportWidget.cpp
    )
endif()

# Application icon resources (window icon + Windows executable icon)
qt_add_resources(APP_ICON_QRC_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/app_icons.qrc
)

add_executable(OpenGeoStudio
    ${APP_SOURCES}
    ${LANEMAKER_QRC_SOURCES}
    ${LANEMAKER_SHADER_QRC_SOURCES}
    ${APP_ICON_QRC_SOURCES}
)

# Embed the icon in the Windows executable (.exe) metadata
if(WIN32)
    target_sources(OpenGeoStudio PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/app.rc
    )
endif()

target_include_directories(OpenGeoStudio PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/events
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/logger
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/project
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/workspace
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/home
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/roadstudio
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/roadstudio/widgets
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/roadstudio/editor
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/trainstudio
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/terrain
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/map
)

target_link_libraries(OpenGeoStudio PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Network
    road_engine::core
    plugin_api
    TIFF::TIFF
    PNG::PNG
)

# Link OpenGL library for 3D viewport
if(WIN32)
    target_link_libraries(OpenGeoStudio PRIVATE opengl32)
endif()

if(QMapLibre_FOUND)
    target_link_libraries(OpenGeoStudio PRIVATE QMapLibre::Widgets)
    target_compile_definitions(OpenGeoStudio PRIVATE HAVE_MAPLIBRE=1)
endif()

# LaneMaker is always built and linked
target_link_libraries(OpenGeoStudio PRIVATE lanemaker::core)

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

    # Copy QMapLibre DLLs to deploy directory (windeployqt doesn't know about them)
    if(QMapLibre_FOUND)
        add_custom_command(TARGET OpenGeoStudio POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:QMapLibre::Core>"
                "$<TARGET_FILE_DIR:OpenGeoStudio>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:QMapLibre::Widgets>"
                "$<TARGET_FILE_DIR:OpenGeoStudio>"
            COMMENT "Copying QMapLibre DLLs to deploy directory..."
        )
    endif()
endif()

# ─── doctest Tests (C++ road engine unit tests) ──────────────
option(BUILD_TESTS "Build C++ doctest tests" ON)

if(BUILD_TESTS)
    enable_testing()

    add_executable(geometry_segment_tests
        src/engine/road/geometry_segment_tests.cpp
        src/core/map/map_tests.cpp
    )

    set_target_properties(geometry_segment_tests PROPERTIES AUTOMOC ON)

    target_link_libraries(geometry_segment_tests PRIVATE
        road_engine::core
        Qt6::Core
        Qt6::Gui
        Qt6::Network
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
message(STATUS "  MapLibre:          ${QMapLibre_FOUND}")
message(STATUS "  Tests:             ${BUILD_TESTS}")
if(WIN32)
    message(STATUS "  windeployqt:       ${WINDEPLOYQT_EXECUTABLE}")
endif()
    message(STATUS "  Deploy Dir:        ${CMAKE_BINARY_DIR}/deploy")
message(STATUS "")
