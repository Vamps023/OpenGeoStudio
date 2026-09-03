# 07 - The Files: Formats the App Reads and Writes

A tour of every file type you will meet, what it contains, and who uses it.

## Project files (what you create as a user)

| Format | What it is |
|--------|-----------|
| `.ogproj` | **An OpenGeoStudio project file.** The main file you open/save. It records your project settings, which workspaces you used, terrain selection, and pointers to the data folders. Opening one makes all 5 studios share the same work |
| Terrain tiles (in the project folder) | The downloaded elevation (DEM) and imagery tiles for your chosen area, managed by TerrainStore/TileManager |
| `.xodr` (OpenDRIVE) | **The road network export.** An international standard (XML) describing roads, lanes, markings, signals - readable by driving simulators and road design tools. Also importable back |
| GeoTIFF (`.tif`) | **Heightmap export** from Terrain Studio: an image whose pixels are heights, with geo-location embedded so GIS software knows where it belongs (WGS84 or UTM flavors) |
| PNG imagery/heightmaps | Plain image exports of your map area |

## Files the app can read (inputs)

| Format | What it is |
|--------|-----------|
| `.osm` / OSM XML | OpenStreetMap data: streets, railways, buildings as text. The import pipeline (doc 04) turns it into editable roads/rails |
| FBX, glTF, OBJ, STL | 3D model files imported in the 3D Studio via Assimp, including materials and textures |
| `.ogsmat` | OpenGeoStudio's own material file - saves a model's color/texture settings so they survive a round-trip |
| GeoTIFF / DEM tiles | Elevation data from various providers (GPXZ, OpenTopography) |
| Satellite imagery tiles | From Mapbox / Esri World Imagery (some need an API key) |

## Files inside the repository (for developers)

| File | What it is |
|------|-----------|
| `CMakeLists.txt` | The master build recipe (see doc 06) |
| `vcpkg.json` | The library shopping list |
| `.hpp` / `.cpp` | C++ headers (descriptions) and sources (implementations) |
| `.qrc` | Qt resource lists - icons/shaders/SVGs packed inside the .exe at build time |
| `.compositor` | OGRE-Next setup script for 3D rendering passes |
| `app.rc` / `.ico` / `.png` / `.svg` | Windows program metadata (version 1.0.0) and the app icon in various sizes |
| `.nsi` | The NSIS installer script |
| `.dds` | GPU-ready texture (the rail trackbed texture) |
| `log.txt` (next to the .exe) | The app's diary - written at every run; the first place to look when something misbehaves |
| `.yml` (in `.github/`) | The CI and release automation recipes |
| `LICENSE` | MIT license text |

## Who writes what, at a glance

```
You (the user):
  create  -  .ogproj project
  download - DEM tiles + imagery tiles
  draw    -  roads/rails (stored in the project, exported as .xodr)
  export  -  GeoTIFF heightmaps, PNG images
  import  -  OSM data, FBX/glTF/OBJ models

The build system:
  produces - OpenGeoStudio.exe + supporting DLLs/resources
  packages - portable ZIP, NSIS installer

The app at runtime:
  writes  - log.txt next to the exe
```

## A note on "standards"

The exports are deliberately **open standards** (OpenDRIVE, GeoTIFF), so
your work is not locked inside this app. Any road simulator or GIS tool
that speaks those formats can pick up where OpenGeoStudio leaves off.