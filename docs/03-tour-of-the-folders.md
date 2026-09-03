# 03 - Tour of the Folders (What Everything Is For)

This is a guided walk through every folder in the repository, using one
analogy: **OpenGeoStudio is a company, and each folder is a department.**

## Top level - the building

| Item | What it is |
|------|-----------|
| `src/` | **The workshop.** All the actual program code lives here |
| `docs/` | The library - beginner guides (the files you are reading) |
| `assets/` | Branding: the logo and app icons (PNG, SVG, ICO) |
| `resources/` | Files bundled inside the app: icons, SVG symbols for Road Studio, the OGRE compositor script, Windows version info (`app.rc`) |
| `installer/` | The NSIS script that builds the Windows installer (.exe) plus its icon |
| `cmake/` | Helper scripts for the build system (test launcher, packaging) |
| `scripts/` | Utility scripts: `package.ps1` (packaging), `ui-drive` (drives the app's UI for testing) |
| `.github/` | Instructions for GitHub: `ci.yml` (build + test on every change) and `release.yml` (build ZIP + installer for releases) |
| `build/` | The output kitchen - everything the build produces. Safe to delete; it is regenerated |
| `.qodo/` | Notes from an AI coding assistant |
| `CMakeLists.txt` | **The master recipe.** Tells the build system what to compile and how |
| `vcpkg.json` | The shopping list - the external libraries the app needs |
| `LICENSE` | The MIT license text |
| `AGENTS.md` | A cheat-sheet written for AI coding assistants (build commands, conventions) - also handy for humans |
| `implementation_plan.md` | An old planning note for a UI-refactor task |
| `PORTABLE_README.txt` | The readme shipped inside the portable ZIP |
| `README.md` | One-page overview pointing to these docs |
| `tok_test.obj` | A stray 3D model left over from testing |

## Inside `src/` - the departments

Code files come in two flavors:
- **`.hpp` (header)** - like a job description: what a component can do.
- **`.cpp` (source)** - the actual work: how it does it.

Some folders are "header-only" - just job descriptions, no separate
implementation files. The compiler fills in the rest.

### `src/app` - the front desk

- `main.cpp` - the **power button**. Every launch starts here.
- `AppMainWindow` - the **window shell**: menu bar, toolbar, status bar,
  the room-switcher, and docking panels.
- `SettingsDialog` - the settings window. `CommandPalette` - quick
  command search (like Ctrl+Shift+P in VS Code).
- `MapViewportWidget` - the satellite map panel (when MapLibre is available).

### `src/theme` - the interior decorator

One single file, `Theme.hpp`, defines **every color, font size and
spacing** used in the app (a refined GitHub-dark look). No other file
is allowed to hard-code colors - change it here and the whole app follows.

### `src/core` - the back office

Services the whole app shares:

| Subfolder | Department | What it does |
|-----------|-----------|--------------|
| `ApplicationContext.hpp` | The switchboard | Connects all services in one object; every room asks here first |
| `events/` | The pinboard | EventBus: one department can announce news ("project opened!") and others react |
| `project/` | The filing cabinet | Project + ProjectManager: create, open, save `.ogproj` files, remember recent projects |
| `workspace/` | Room service | Tracks which of the 5 rooms is active |
| `logger/` | The diary | Writes app activity to `log.txt` next to the exe - crucial for diagnosing crashes |
| `assets/` | Receiving dock | Imports 3D models (FBX/glTF/OBJ) via Assimp: geometry, materials, textures |
| `osm/` | The map translators | The full OpenStreetMap import pipeline (details below) |
| `terrain/` | The ground crew | Downloading and processing terrain: providers, tile manager, cache, validation, analysis, masks |
| `map/` | The older map helpers | Tile cache/matrix math and the legacy coordinate helpers |
| `world/` | World Authoring | A scene model: objects, splines (curves), procedural generation (PCGEngine), undo/redo |

**The `osm/` pipeline in plain words:** OpenStreetMap gives you raw map
data (XML text). This department turns it into a usable road network:
parse the XML, classify roads (motorway vs residential), build the network,
detect junctions, generate lanes, roundabouts, signs, markings, validate
everything, and finally export or hand it to the road editor. There is a
rail version too.

### `src/engine` - the factory floor

Two big machines:

**`engine/road/` - the road math engine (header-only).** Pure geometry:
lines, arcs, clothoids (the special curves real roads use so steering
feels natural), lane sections, 3D mesh generation, road markings,
OpenDRIVE export, intersections. It knows nothing about buttons or
windows - it is pure math. Its test suite has 504 checks.

**`engine/lanemaker/` - the road editor (an app within the app).**
Road Studio embeds this wholesale. Sub-folders:
- `ui/` - the editor screens: main window, road drawing/creation/deletion,
  action manager (undo/redo commands), sign system, marking graphics,
  touch support.
- `widgets/` - dialogs: LaneConfigWidget (lanes, sidewalks, curbs),
  DrawOptionDialog, ElevationProfileEditor, replay window.
- `engine/` - the OpenGL renderer: camera, GPU buffers, shaders (small
  programs that run on the graphics card), spatial index (fast "what is
  near the mouse?" lookups), OBJ loader.
- `xodr/` + `libOpenDRIVE/` - reads and writes the OpenDRIVE (.xodr)
  standard. Includes third-party helpers (pugixml for XML, earcut for
  triangulation).
- `traffic/` - simple vehicle and traffic-light simulation.
- `test/` - validation rules for the road network.

### `src/gis` - the coordinate translation office

The Earth is round; screens are flat. Every location can be described by
numbers in many different systems (called **CRS**, identified by **EPSG**
codes). This department, built on the **PROJ** library, translates between
them: `CRSManager` (looks up systems by name/code), `CoordinateTransform`
(converts coordinates), `CRSSearch` and the `CrsSelectorDialog` (a pick-a-
system dialog for users). Every studio - terrain, map, OSM, roads - uses
this before displaying or saving anything.

### `src/ui` - the showrooms

The five rooms described in doc 02, one subfolder each:
`home/`, `terrain/`, `roadstudio/`, `trainstudio/`, `studio3d/`.
These folders build what you see and click; the heavy lifting is delegated
to `src/core` and `src/engine`.

## The one-paragraph summary

You click in a **showroom** (`src/ui`). The showroom asks the
**back office** (`src/core`) for data and the **factory** (`src/engine`)
for road math and rendering, while **translation office** (`src/gis`)
keeps every coordinate consistent and the **interior decorator**
(`src/theme`) keeps everything pretty. The **front desk** (`src/app`)
starts it all and holds the window together.