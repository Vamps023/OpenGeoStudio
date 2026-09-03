# 06 - How the App Is Built, Run and Tested

## Two ways to get OpenGeoStudio

1. **Download a ready-made release** (easiest): from the GitHub Releases
   page, grab the portable ZIP (extract, run `OpenGeoStudio.exe`) or the
   installer (.exe setup). No tools needed.
2. **Build it yourself from source** - what this document explains.

## What "building" means (kitchen analogy)

| Step | Kitchen | In this project |
|------|---------|-----------------|
| 1 | Read the recipe | CMake reads `CMakeLists.txt` |
| 2 | Buy groceries | vcpkg downloads the libraries in `vcpkg.json` (Qt, PROJ, CGAL...) |
| 3 | Translate the recipe | The compiler (MSVC) turns each .cpp into machine code |
| 4 | Assemble the dish | The linker stitches everything into `OpenGeoStudio.exe` |
| 5 | Taste test | Automated tests verify nothing is broken |
| 6 | Package for delivery | Scripts build the portable ZIP and the installer |

## The tools you would need (one-time setup)

- **Visual Studio 2022 Build Tools** - Microsoft's compiler (the free
  "Build Tools" edition is enough).
- **CMake + Ninja** - the build organizers.
- **Qt 6.8** (msvc2022_64 flavor) - the UI toolkit.
- **vcpkg** - the library shopper.
- Optional: **MapLibre Native Qt** (satellite map) and **OGRE-Next** (3D
  Studio) built from source. Without MapLibre the map panel is disabled;
  the rest of the app still works.

## The actual commands (copy-paste)

All commands first load the compiler environment (`vcvars64.bat`), then
run the tool. This project's convention:

**1. Configure** (once, or after changing CMakeLists.txt) - CMake reads
the master recipe and prepares the build folder:

```powershell
cmd /c "call ""C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"" && cmake -B D:\git\OpenGeoStudio-Qt\build -S D:\git\OpenGeoStudio-Qt -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH=C:/Qt/6.8.0/msvc2022_64 -DQt6_DIR=C:/Qt/6.8.0/msvc2022_64/lib/cmake/Qt6"
```

**2. Build the app**:

```powershell
cmd /c "call ""C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"" && cmake --build D:\git\OpenGeoStudio-Qt\build --target OpenGeoStudio"
```

**3. Build everything** (app + all tests): same command without
`--target OpenGeoStudio`.

The finished program lands in `build/` (deployed with all its DLLs).
The `build/` folder is disposable - delete it and rebuild anytime.

## The automated taste tests

```powershell
cd /d D:\git\OpenGeoStudio-Qt\build
ctest --output-on-failure --timeout 120
```

12 test suites protect the project. If any fails, the code that changed
probably broke something:

| Test suite | What it checks |
|------------|----------------|
| `geometry_segment_tests` | Road math engine curves/geometry (504 checks) |
| `test_terrain_pipeline` | Terrain download/processing (28) |
| `test_world_model` | World Authoring data model (34) |
| `test_world_workflow` | World Authoring workflows (22) |
| `test_crs_system` | Coordinate systems and transforms (89) |
| `test_osm_pipeline` | OpenStreetMap import pipeline (155) |
| `test_houston_roundtrip` | Houston OSM data round-trip (56) |
| `test_geotiff_writer` | GeoTIFF export metadata (WGS84 + UTM) |
| `test_gpxz_download` | Elevation download (skips without API key) |
| `test_road_studio` | Road editor features: signs, markings, lanes, junctions... (335) |
| `test_road_studio_ui` | Editor UI smoke test, headless (12) |
| `test_asset_import` | FBX/glTF/OBJ model import (42) |

Tests even run without a screen: the setting `QT_QPA_PLATFORM=offscreen`
makes Qt pretend a display exists (used by the CI robots).

## Packaging for delivery

- **Portable ZIP** (about 41 MB, extract-and-run):
  build the target `package_portable`.
- **Installer**: `installer/OpenGeoStudio.nsi` (NSIS script) builds the
  setup .exe with Start-Menu shortcuts and an uninstaller.
- **Releases are automated**: pushing a version tag like `v1.2.3` to
  GitHub triggers `.github/workflows/release.yml`, which builds, tests,
  packages both formats, and publishes a GitHub Release.

## Settings you can pass via environment variables

| Variable | Purpose |
|----------|---------|
| `GPXZ_API_KEY` | Personal key for the GPXZ elevation service |
| `OPENTOPO_API_KEY` | Key for OpenTopography elevation data |
| `MAPBOX_TOKEN` | Key for Mapbox satellite imagery |
| `OGS_SKIP_ROAD_MODEL_TESTS` | Set to `1` to skip heavy road-model tests (CI uses this) |
| `QT_QPA_PLATFORM` | `offscreen` runs the app/tests without a display |

## Famous pitfall (already solved, but worth knowing)

There are two `vcvars64.bat` files (Build Tools vs VS Community). Using
the wrong one causes the cryptic error `STL1001: Unexpected compiler
version`. The project convention: always use the **Build Tools** path
shown in the commands above.