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
    VERSION 1.0.0
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
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Network Concurrent)

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
            ${QMAPLIBRE_DIR}
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
find_package(PROJ CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)

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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/ui/lanemaker_context.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/ui/sign_system.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/ui/marking_graphics.cpp
)
# LaneMaker widget sources
set(LANEMAKER_WIDGET_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/widgets/AnimatedPopupDialog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/widgets/DrawOptionDialog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/widgets/ElevationProfileEditor.cpp
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
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core/map
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/roadstudio
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

# ─── Main Application Executable ─────────────────────────────
set(APP_SOURCES
    src/app/main.cpp
    src/app/AppMainWindow.hpp
    src/app/AppMainWindow.cpp
    src/app/SettingsDialog.hpp
    src/app/CommandPalette.hpp
    # Theme (single source of truth for UI colors/styles)
    src/theme/Theme.hpp
    # Core services
    src/core/ApplicationContext.cpp
    src/core/events/EventBus.cpp
    src/core/project/ProjectManager.cpp
    src/core/workspace/WorkspaceManager.cpp
    # Core - model import (Assimp: FBX/glTF/OBJ → geometry data)
    src/core/assets/ImportedModel.cpp
    src/core/assets/MaterialAsset.cpp
    # UI - Home
    src/ui/home/HomeWidget.cpp
    # UI - Road Studio (LaneMaker's MainWindow is used directly)
    src/ui/roadstudio/RoadStudioWidget.cpp
    src/ui/roadstudio/OsmImportDialog.cpp
    src/ui/roadstudio/DrapeOntoTerrain.cpp
    # UI - Train Studio
    src/ui/trainstudio/TrainStudioWidget.cpp
    src/ui/trainstudio/RailOsmImportDialog.cpp
    # UI - Terrain Studio
    src/ui/terrain/TerrainStore.cpp
    src/ui/terrain/TerrainViewport.cpp
    src/ui/terrain/ExportPanel.cpp
    src/ui/terrain/ExportEngine.cpp
    src/ui/terrain/RasterWriter.cpp
    src/ui/terrain/LayerStack.cpp
    src/ui/terrain/SearchBar.cpp
    src/ui/terrain/TerrainStudioWidget.cpp
    # Terrain Pipeline (QGIS-like terrain data pipeline)
    # All header-only — no .cpp files needed
    # UI - 3D Studio (OGRE-Next embedded)
    src/ui/studio3d/Studio3DWidget.cpp
    src/ui/studio3d/OgreWidget.cpp
    src/ui/studio3d/EditorPanels.cpp
    src/ui/studio3d/PropertiesEditor.cpp
    src/ui/studio3d/NPanel.cpp
    # GIS / CRS (PROJ-backed)
    src/gis/crs/CRSManager.cpp
    src/gis/crs/CoordinateTransform.cpp
    src/gis/crs/CRSSearch.cpp
    src/gis/ui/CrsSelectorDialog.cpp
)

if(QMapLibre_FOUND)
    list(APPEND APP_SOURCES
        src/app/MapViewportWidget.cpp
    )
endif()

# Application icon resources (window icon + Windows executable icon)
qt_add_resources(APP_ICON_QRC_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/app_icons.qrc
)

# Road Studio SVG line icons (OpenGeoStudio-native, replacing LaneMaker PNGs)
qt_add_resources(ROAD_STUDIO_QRC_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/road_studio/road_studio.qrc
)

add_executable(OpenGeoStudio
    ${APP_SOURCES}
    ${LANEMAKER_QRC_SOURCES}
    ${LANEMAKER_SHADER_QRC_SOURCES}
    ${APP_ICON_QRC_SOURCES}
    ${ROAD_STUDIO_QRC_SOURCES}
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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/trainstudio
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/terrain
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/map
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/terrain
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/terrain/providers
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/terrain/masks
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/world
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/osm
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/assets
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gis
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gis/crs
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gis/ui
)

target_link_libraries(OpenGeoStudio PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Network
    Qt6::Concurrent
    road_engine::core
    TIFF::TIFF
    PROJ::proj
    assimp::assimp
)

# Single source of truth for application version
target_compile_definitions(OpenGeoStudio PRIVATE
    OGS_VERSION="${PROJECT_VERSION}"
    OGS_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    OGS_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    OGS_VERSION_PATCH=${PROJECT_VERSION_PATCH}
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

# ─── OGRE-Next integration ───
# OGRE-Next is embedded directly in the Qt app (no external process)
# Set OGRE_NEXT_DIR via CMake variable or OGRE_NEXT_DIR env var.
# Example: cmake -B build -DOGRE_NEXT_DIR=/path/to/ogre-next
set(OGRE_NEXT_DIR "$ENV{OGRE_NEXT_DIR}" CACHE PATH "Path to OGRE-Next build")
set(OGRE_NEXT_BUILD "${OGRE_NEXT_DIR}/build")

if(EXISTS "${OGRE_NEXT_BUILD}/lib/OgreNextMain.lib")
    message(STATUS "OGRE-Next found at ${OGRE_NEXT_DIR}")

    # Include paths
    target_include_directories(OpenGeoStudio PRIVATE
        "${OGRE_NEXT_DIR}/OgreMain/include"
        "${OGRE_NEXT_DIR}/Components/Hlms/Common/include"
        "${OGRE_NEXT_DIR}/Components/Hlms/Pbs/include"
        "${OGRE_NEXT_DIR}/Components/Hlms/Unlit/include"
        "${OGRE_NEXT_DIR}/Components/PlanarReflections/include"
        "${OGRE_NEXT_DIR}/Samples/2.0/Tutorials/Tutorial_Terrain/include"
        "${OGRE_NEXT_BUILD}/include"
    )

    # Link OGRE-Next libraries
    target_link_libraries(OpenGeoStudio PRIVATE
        "${OGRE_NEXT_BUILD}/lib/OgreNextMain.lib"
        "${OGRE_NEXT_BUILD}/lib/OgreNextHlmsPbs.lib"
        "${OGRE_NEXT_BUILD}/lib/OgreNextHlmsUnlit.lib"
        "${OGRE_NEXT_BUILD}/lib/OgreNextPlanarReflections.lib"
        "${OGRE_NEXT_BUILD}/lib/RenderSystem_Direct3D11.lib"
        "${OGRE_NEXT_BUILD}/lib/RenderSystem_GL3Plus.lib"
        "${OGRE_NEXT_BUILD}/lib/Plugin_ParticleFX.lib"
    )

    target_compile_definitions(OpenGeoStudio PRIVATE HAVE_OGRE=1)

    # Copy OGRE DLLs to deploy folder (same folder as exe)
    add_custom_command(TARGET OpenGeoStudio POST_BUILD
        # Copy all OGRE DLLs to the exe folder so Windows can find them
        COMMAND ${CMAKE_COMMAND} -E copy
        "${OGRE_NEXT_BUILD}/bin/OgreNextMain.dll" "$<TARGET_FILE_DIR:OpenGeoStudio>"
        COMMAND ${CMAKE_COMMAND} -E copy
        "${OGRE_NEXT_BUILD}/bin/OgreNextHlmsPbs.dll" "$<TARGET_FILE_DIR:OpenGeoStudio>"
        COMMAND ${CMAKE_COMMAND} -E copy
        "${OGRE_NEXT_BUILD}/bin/OgreNextHlmsUnlit.dll" "$<TARGET_FILE_DIR:OpenGeoStudio>"
        COMMAND ${CMAKE_COMMAND} -E copy
        "${OGRE_NEXT_BUILD}/bin/OgreNextPlanarReflections.dll" "$<TARGET_FILE_DIR:OpenGeoStudio>"
        COMMAND ${CMAKE_COMMAND} -E copy
        "${OGRE_NEXT_BUILD}/bin/OgreNextAtmosphere.dll" "$<TARGET_FILE_DIR:OpenGeoStudio>"
        COMMAND ${CMAKE_COMMAND} -E copy
        "${OGRE_NEXT_BUILD}/bin/OgreNextProperty.dll" "$<TARGET_FILE_DIR:OpenGeoStudio>"
        COMMAND ${CMAKE_COMMAND} -E copy
        "${OGRE_NEXT_BUILD}/bin/RenderSystem_Direct3D11.dll" "$<TARGET_FILE_DIR:OpenGeoStudio>"
        COMMAND ${CMAKE_COMMAND} -E copy
        "${OGRE_NEXT_BUILD}/bin/RenderSystem_GL3Plus.dll" "$<TARGET_FILE_DIR:OpenGeoStudio>"
        COMMAND ${CMAKE_COMMAND} -E copy
        "${OGRE_NEXT_BUILD}/bin/RenderSystem_NULL.dll" "$<TARGET_FILE_DIR:OpenGeoStudio>"
        COMMAND ${CMAKE_COMMAND} -E copy
        "${OGRE_NEXT_BUILD}/bin/Plugin_ParticleFX.dll" "$<TARGET_FILE_DIR:OpenGeoStudio>"
        COMMAND ${CMAKE_COMMAND} -E copy
        "${OGRE_NEXT_BUILD}/bin/Plugin_ParticleFX2.dll" "$<TARGET_FILE_DIR:OpenGeoStudio>"
        # Copy Hlms shader media
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${OGRE_NEXT_DIR}/Samples/Media/Hlms" "$<TARGET_FILE_DIR:OpenGeoStudio>/Hlms"
        # Also keep ogre/ subfolder for plugins.cfg compatibility
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${OGRE_NEXT_BUILD}/bin" "$<TARGET_FILE_DIR:OpenGeoStudio>/ogre"
        # Rail material used by Train Studio XML export and 3D rail assets
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:OpenGeoStudio>/assets"
        # Compositor scripts (PSSM shadow node used by the 3D Studio workspace)
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/compositor" "$<TARGET_FILE_DIR:OpenGeoStudio>/compositor"
    )
else()
    message(WARNING "OGRE-Next not found at ${OGRE_NEXT_BUILD}, building without 3D viewport")
    target_compile_definitions(OpenGeoStudio PRIVATE HAVE_OGRE=0)
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

    # Copy PROJ DLL and proj.db data directory to deploy folder
    add_custom_command(TARGET OpenGeoStudio POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:PROJ::proj>"
            "$<TARGET_FILE_DIR:OpenGeoStudio>"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:OpenGeoStudio>/proj"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/share/proj"
            "$<TARGET_FILE_DIR:OpenGeoStudio>/proj"
        COMMENT "Copying PROJ DLL and proj.db to deploy directory..."
    )

    # Copy Assimp DLL (model import: FBX/glTF/OBJ) to deploy folder
    add_custom_command(TARGET OpenGeoStudio POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:assimp::assimp>"
            "$<TARGET_FILE_DIR:OpenGeoStudio>"
        COMMENT "Copying Assimp DLL to deploy directory..."
    )

    # Also set up proj.db in the build directory for CTest tests
    # (tests look for PROJ_LIB=build/proj)
    add_custom_target(ogs_test_setup ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${CMAKE_BINARY_DIR}/proj"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/share/proj"
            "${CMAKE_BINARY_DIR}/proj"
        COMMENT "Setting up PROJ database for CTest..."
    )
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

    # ─── GPXZ DEM download + decode end-to-end test ───────────
    add_executable(test_gpxz_download
        src/ui/terrain/test_gpxz_download.cpp
    )
    set_target_properties(test_gpxz_download PROPERTIES AUTOMOC ON)
    target_include_directories(test_gpxz_download PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/terrain
    )
    target_link_libraries(test_gpxz_download PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Network
        TIFF::TIFF
    )
    if(MSVC)
        target_compile_options(test_gpxz_download PRIVATE /EHa /bigobj)
    endif()

    # ─── GeoTIFF writer test ──────────────────────────────────
    add_executable(test_geotiff_writer
        src/ui/terrain/test_geotiff_writer.cpp
        src/ui/terrain/RasterWriter.cpp
    )
    set_target_properties(test_geotiff_writer PROPERTIES AUTOMOC ON)
    target_include_directories(test_geotiff_writer PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/terrain
    )
    target_link_libraries(test_geotiff_writer PRIVATE
        Qt6::Core
        Qt6::Gui
        TIFF::TIFF
    )
    if(MSVC)
        target_compile_options(test_geotiff_writer PRIVATE /EHa /bigobj)
    endif()

    # ─── Terrain Pipeline test suite (26 tests) ───────────────
    add_executable(test_terrain_pipeline
        src/core/terrain/test_terrain_pipeline.cpp
        src/gis/crs/CRSManager.cpp
        src/gis/crs/CoordinateTransform.cpp
    )
    set_target_properties(test_terrain_pipeline PROPERTIES AUTOMOC ON)
    target_include_directories(test_terrain_pipeline PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core/terrain
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core/terrain/providers
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core/terrain/masks
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core/map
        ${CMAKE_CURRENT_SOURCE_DIR}/src/gis/crs
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/terrain
    )
    target_link_libraries(test_terrain_pipeline PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Network
        TIFF::TIFF
        PROJ::proj
    )
    if(MSVC)
        target_compile_options(test_terrain_pipeline PRIVATE /EHa /bigobj)
    endif()

    # ─── World Model test suite (3D Studio) ──────────────────
    add_executable(test_world_model
        src/core/world/test_world_model.cpp
    )
    set_target_properties(test_world_model PROPERTIES AUTOMOC ON)
    target_include_directories(test_world_model PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core/world
    )
    target_link_libraries(test_world_model PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
    )
    if(MSVC)
        target_compile_options(test_world_model PRIVATE /EHa /bigobj)
    endif()

    # ─── CRS System test suite (PROJ-backed) ────────────────
    add_executable(test_crs_system
        src/gis/test/test_crs_system.cpp
        src/gis/crs/CRSManager.cpp
        src/gis/crs/CoordinateTransform.cpp
        src/gis/crs/CRSSearch.cpp
    )
    set_target_properties(test_crs_system PROPERTIES AUTOMOC ON)
    target_include_directories(test_crs_system PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/gis
        ${CMAKE_CURRENT_SOURCE_DIR}/src/gis/crs
    )
    target_link_libraries(test_crs_system PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        PROJ::proj
    )
    if(MSVC)
        target_compile_options(test_crs_system PRIVATE /EHa /bigobj)
    endif()

    # ─── World Workflow test suite (3D Studio) ──────────────
    add_executable(test_world_workflow
        src/core/world/test_world_workflow.cpp
    )
    set_target_properties(test_world_workflow PROPERTIES AUTOMOC ON)
    target_include_directories(test_world_workflow PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core/world
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core/terrain
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core/terrain/providers
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core/terrain/masks
    )
    target_link_libraries(test_world_workflow PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network
    )
    if(MSVC)
        target_compile_options(test_world_workflow PRIVATE /EHa /bigobj)
    endif()

    # ─── Asset import test suite (Assimp: FBX/glTF/OBJ) ──────
    add_executable(test_asset_import
        src/core/assets/test_asset_import.cpp
        src/core/assets/ImportedModel.cpp
        src/core/assets/MaterialAsset.cpp
    )
    set_target_properties(test_asset_import PROPERTIES AUTOMOC ON)
    target_include_directories(test_asset_import PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core/logger
    )
    target_link_libraries(test_asset_import PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        assimp::assimp
    )
    if(MSVC)
        target_compile_options(test_asset_import PRIVATE /EHa /bigobj)
    endif()

    # ─── OSM Pipeline test suite ─────────────────────────────
    add_executable(test_osm_pipeline
        src/core/osm/test_osm_pipeline.cpp
    )
    set_target_properties(test_osm_pipeline PROPERTIES AUTOMOC ON)
    target_include_directories(test_osm_pipeline PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core/osm
    )
    target_link_libraries(test_osm_pipeline PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
    )
    if(MSVC)
        target_compile_options(test_osm_pipeline PRIVATE /EHa /bigobj)
    endif()

    # ─── Houston OSM round-trip test ──────────────────────────
    add_executable(test_houston_roundtrip
        src/core/osm/test_houston_roundtrip.cpp
    )
    set_target_properties(test_houston_roundtrip PROPERTIES AUTOMOC ON)
    target_include_directories(test_houston_roundtrip PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core/osm
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/libOpenDRIVE/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/libOpenDRIVE/thirdparty
    )
    target_link_libraries(test_houston_roundtrip PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        lanemaker::core
    )
    if(MSVC)
        target_compile_options(test_houston_roundtrip PRIVATE /EHa /bigobj)
    endif()

    # ─── Road Studio feature tests ────────────────────────────
    add_executable(test_road_studio
        src/engine/lanemaker/test/test_road_studio.cpp
        src/engine/lanemaker/ui/sign_system.cpp
        src/engine/lanemaker/xodr/cross_section_extender.cpp
    )
    set_target_properties(test_road_studio PROPERTIES AUTOMOC ON)
    target_include_directories(test_road_studio PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/ui
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/xodr
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/libOpenDRIVE/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker/libOpenDRIVE/thirdparty
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/roadstudio
    )
    target_link_libraries(test_road_studio PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        lanemaker::core
    )
    if(MSVC)
        target_compile_options(test_road_studio PRIVATE /EHa /bigobj)
        # Increase stack size to avoid stack overflow in LaneMaker junction tests
        # (deep recursion in Junction::CreateFrom with complex junction topologies)
        set_target_properties(test_road_studio PROPERTIES
            LINK_FLAGS "/STACK:16777216")
    endif()
    target_compile_definitions(test_road_studio PRIVATE G_TEST)

    # Copy QMapLibre DLLs next to the test executable so it can find them
    # without requiring a developer-specific PATH.
    if(QMapLibre_FOUND AND WIN32)
        add_custom_command(TARGET test_road_studio POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:QMapLibre::Core>"
                "$<TARGET_FILE_DIR:test_road_studio>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:QMapLibre::Widgets>"
                "$<TARGET_FILE_DIR:test_road_studio>"
            COMMENT "Copying QMapLibre DLLs for test_road_studio..."
        )
    endif()

    # Register all test targets with CTest (not just geometry_segment_tests).
    # This ensures `ctest --output-on-failure` runs every test in CI.

    # Common environment for all tests.
    # We generate a wrapper batch script that sets up the PATH for Qt and
    # QMapLibre DLLs, then runs the test executable. This avoids the
    # semicolon conflict between CMake list separators and Windows PATH.
    set(OGS_TEST_ENV
        "OGS_SKIP_ROAD_MODEL_TESTS=1;QT_QPA_PLATFORM=offscreen;PROJ_LIB=${CMAKE_BINARY_DIR}/proj")

    # Generate test wrapper script
    get_target_property(_qt_core_location Qt6::Core LOCATION_RELEASE)
    get_filename_component(_qt_bin_dir "${_qt_core_location}" DIRECTORY)
    set(_qmlib_dir "")
    if(QMapLibre_FOUND)
        get_target_property(_qmlib_core QMapLibre::Core IMPORTED_LOCATION_RELEASE)
        if(_qmlib_core)
            get_filename_component(_qmlib_dir "${_qmlib_core}" DIRECTORY)
        endif()
    endif()
    configure_file(
        "${CMAKE_SOURCE_DIR}/cmake/ogs_test_wrapper.bat.in"
        "${CMAKE_BINARY_DIR}/ogs_test_wrapper.bat"
        @ONLY)

    add_test(NAME test_gpxz_download     COMMAND "${CMAKE_BINARY_DIR}/ogs_test_wrapper.bat" $<TARGET_FILE:test_gpxz_download>)
    add_test(NAME test_geotiff_writer    COMMAND "${CMAKE_BINARY_DIR}/ogs_test_wrapper.bat" $<TARGET_FILE:test_geotiff_writer>)
    add_test(NAME test_terrain_pipeline  COMMAND "${CMAKE_BINARY_DIR}/ogs_test_wrapper.bat" $<TARGET_FILE:test_terrain_pipeline>)
    add_test(NAME test_world_model       COMMAND "${CMAKE_BINARY_DIR}/ogs_test_wrapper.bat" $<TARGET_FILE:test_world_model>)
    add_test(NAME test_crs_system        COMMAND "${CMAKE_BINARY_DIR}/ogs_test_wrapper.bat" $<TARGET_FILE:test_crs_system>)
    add_test(NAME test_world_workflow    COMMAND "${CMAKE_BINARY_DIR}/ogs_test_wrapper.bat" $<TARGET_FILE:test_world_workflow>)
    add_test(NAME test_osm_pipeline      COMMAND "${CMAKE_BINARY_DIR}/ogs_test_wrapper.bat" $<TARGET_FILE:test_osm_pipeline>)
    add_test(NAME test_houston_roundtrip COMMAND "${CMAKE_BINARY_DIR}/ogs_test_wrapper.bat" $<TARGET_FILE:test_houston_roundtrip>)
    add_test(NAME test_road_studio       COMMAND "${CMAKE_BINARY_DIR}/ogs_test_wrapper.bat" $<TARGET_FILE:test_road_studio>)
    add_test(NAME test_asset_import      COMMAND "${CMAKE_BINARY_DIR}/ogs_test_wrapper.bat" $<TARGET_FILE:test_asset_import>)

    # Apply common environment to all tests
    foreach(t_test IN ITEMS
        test_gpxz_download test_geotiff_writer test_terrain_pipeline
        test_world_model test_crs_system test_world_workflow
        test_osm_pipeline test_houston_roundtrip test_road_studio
        test_asset_import)
        set_tests_properties(${t_test} PROPERTIES
            ENVIRONMENT "${OGS_TEST_ENV}")
    endforeach()
endif()

# ─── Road Studio UI smoke test (headless) ───
# Tests LaneConfigWidget, profile switching, and preference system
# without requiring an OpenGL context (MainWindow/MapViewGL).
if(BUILD_TESTS)
    add_executable(test_road_studio_ui
        src/engine/lanemaker/test/test_road_studio_ui.cpp
    )
    set_target_properties(test_road_studio_ui PROPERTIES AUTOMOC ON)
    target_include_directories(test_road_studio_ui PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/lanemaker
    )
    target_link_libraries(test_road_studio_ui PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        lanemaker::core
    )
    if(MSVC)
        target_compile_options(test_road_studio_ui PRIVATE /EHa /bigobj)
    endif()
    add_test(NAME test_road_studio_ui    COMMAND "${CMAKE_BINARY_DIR}/ogs_test_wrapper.bat" $<TARGET_FILE:test_road_studio_ui>)
    set_tests_properties(test_road_studio_ui PROPERTIES
        TIMEOUT 30)
endif()

# ─── Print Configuration Summary ─────────────────────────────
message(STATUS "")
message(STATUS "═══ OpenGeoStudio Build Configuration ═══")
message(STATUS "  Version:           ${PROJECT_VERSION}")
message(STATUS "  C++ Standard:      ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type:        ${CMAKE_BUILD_TYPE}")
message(STATUS "  Generator:         ${CMAKE_GENERATOR}")
message(STATUS "  Qt6 Found:         ${Qt6_FOUND}")
message(STATUS "  LaneMaker:         always enabled")
message(STATUS "  MapLibre:          ${QMapLibre_FOUND}")
message(STATUS "  Tests:             ${BUILD_TESTS}")
if(WIN32)
    message(STATUS "  windeployqt:       ${WINDEPLOYQT_EXECUTABLE}")
endif()
    message(STATUS "  Deploy Dir:        ${CMAKE_BINARY_DIR}/deploy")
message(STATUS "")

# ─── Packaging (Portable ZIP) ────────────────────────────────
# The portable ZIP is created from the deploy/ directory after
# windeployqt has collected all runtime dependencies.
# Run: cmake --build build --target package_portable
if(WIN32)
    add_custom_target(package_portable
        COMMAND ${CMAKE_COMMAND}
            -DPACKAGE_DIR=${CMAKE_BINARY_DIR}/deploy
            -DPACKAGE_VERSION=${PROJECT_VERSION}
            -DOUTPUT_DIR=${CMAKE_BINARY_DIR}
            -P ${CMAKE_SOURCE_DIR}/cmake/PackagePortable.cmake
        DEPENDS OpenGeoStudio
        COMMENT "Creating portable ZIP package..."
    )
endif()
