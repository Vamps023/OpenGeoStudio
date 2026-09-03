# 05 - Glossary (Every Technical Word, Translated)

Alphabetical within groups. Skim it; come back when a word confuses you.

## The project and its languages

| Term | Plain meaning |
|------|---------------|
| OpenGeoStudio | This app: terrain + road + rail design on real maps |
| C++ | The programming language the app is written in. Fast and runs close to the metal |
| C++20 | The 2020 edition of C++; a modern dialect with convenient features |
| Qt (Qt 6) | A big toolkit of ready-made windows, buttons, menus, file dialogs used to build desktop apps |
| Widget | One UI piece: a button, a panel, a text box |
| Electron | The old web-based technology this app was migrated away from |
| TypeScript/JavaScript | Web languages used by the old Electron version |

## Building the program

| Term | Plain meaning |
|------|---------------|
| Source code | Human-readable recipe files (.cpp/.hpp) in `src/` |
| Compiler | Translates source code into machine instructions (MSVC here) |
| MSVC / Build Tools | Microsoft's compiler toolchain used on Windows |
| Build | Run compiler + linker to produce the .exe |
| Linker | Stitches compiled pieces together into one program |
| CMake | Reads `CMakeLists.txt` (the master recipe) and organizes the build |
| Ninja | A fast build runner CMake can drive |
| vcpkg | Microsoft's library manager; `vcpkg.json` is the shopping list |
| Dependency | An external library the app uses (Qt, PROJ, CGAL...) |
| DLL | A shared library (.dll file) the program loads at runtime |
| Static library | Code merged into the .exe at build time (e.g. LaneMaker) |
| Header-only library | A library shipped as headers only; compiled straight into users |
| Release build | Optimized build for daily use (vs "Debug", which is slow but inspectable) |
| Qt resource (.qrc) | Icons/shaders packed inside the exe |

## Maps, geography and road design

| Term | Plain meaning |
|------|---------------|
| GIS | Geographic Information Systems - the science of map data |
| Geospatial | Tied to a real place on Earth |
| CRS | Coordinate Reference System - the numbering scheme for locations |
| EPSG code | An ID number for a CRS (EPSG:4326 = lat/long) |
| PROJ | The library that converts between coordinate systems; `proj.db` is its lookup database |
| DEM | Digital Elevation Model - a grayscale image where brightness = height |
| GeoTIFF | A TIFF image with geo-information ("where am I on Earth?") embedded |
| Imagery | Satellite/aerial photos of the ground |
| Tile / tile grid | Square map pieces (like puzzle pieces) at a zoom level |
| OpenStreetMap (OSM) | Free, crowd-edited map of the world; data is XML |
| OSM XML (.osm) | The file/text format OSM data comes in |
| OpenDRIVE / .xodr | An open standard file format for road networks used by simulators and road design tools |
| Lane section | A stretch of road with a fixed lane arrangement |
| Clothoid | A spiral curve used in real road design so steering is smooth |
| Cross-section | A road viewed as a slice: lanes, shoulders, sidewalk, curb |
| Drape | Project (bend) roads/models onto the terrain surface so they follow hills |
| Junction | Where two roads connect |
| Rail profile | Preset for railway track type (standard, narrow, tram...) |

## 3D graphics

| Term | Plain meaning |
|------|---------------|
| Mesh | A 3D surface built from triangles; how shapes are drawn |
| Vertex | One corner point of a triangle |
| Texture | An image glued onto a mesh (the "skin") |
| DDS | A texture file format the GPU reads directly |
| Shader | A tiny program running on the graphics card to color pixels |
| OpenGL | The drawing API LaneMaker uses |
| OGRE-Next | The 3D scene engine used by the 3D Studio |
| Compositor script | OGRE setup for post-effects/passes (`.compositor` file) |
| Assimp | A library that reads many 3D model formats (FBX, glTF, OBJ, STL) |
| FBX / glTF / OBJ / STL | Common 3D model file formats |
| PBR | "Physically Based Rendering" - realistic material settings |
| Camera / fly camera | The viewpoint; "fly" = move through the scene like a drone |
| UV coordinates | How a texture is wrapped onto a mesh |

## Architecture words (how the code is organized)

| Term | Plain meaning |
|------|---------------|
| Class | A blueprint for a component (e.g. ProjectManager) |
| Object | A living instance of a class doing work |
| Header (.hpp) | The "what it offers" part of a component |
| Source (.cpp) | The "how it works" part |
| API | The set of functions one part offers to others |
| Event / signal | A broadcast message between components (Qt signals; EventBus) |
| Slot | A function that reacts to a signal |
| Service | A long-lived helper object (ProjectManager, TerrainStore...) |
| ApplicationContext | The switchboard object holding all services |
| Singleton-ish pattern | One shared instance used app-wide |
| Undo/redo stack | The list of changes enabling Ctrl+Z/Ctrl+Y |
| Template (UI) | A pre-configured starting point for a new project |
| Preset | A saved configuration (e.g. a "highway" road profile) |
| Serialization | Writing objects to a file (and reading them back) |

## Testing and delivery

| Term | Plain meaning |
|------|---------------|
| Unit test | A tiny automatic check that one piece behaves correctly |
| Test suite / target | A group of such checks (12 exist here, run by CTest) |
| CTest | The test runner that executes all suites |
| doctest | The lightweight testing framework used |
| CI (Continuous Integration) | GitHub automatically builds and tests every change (`.github/workflows/ci.yml`) |
| Release workflow | On a version tag (v1.2.3), GitHub builds ZIP + installer and publishes them |
| Portable ZIP | A zip you just extract and run - no installation |
| NSIS | The tool that builds the Windows installer (.exe setup) |
| Environment variable | A named setting for programs, e.g. `GPXZ_API_KEY` |
| API key | A personal password for an online service |
| MIT license | A permissive open-source license: use, change, share freely |