# 02 - The Five Workspaces (The 5 Rooms of the App)

When you open OpenGeoStudio you see one window. Inside, the left edge lets
you switch between 5 "workspaces" - think of them as 5 rooms in a house.
Each room has its own job, its own panels, and its own part of the code.

| # | Room | Folder that powers it |
|---|------|----------------------|
| 1 | Home | `src/ui/home` |
| 2 | Terrain Studio | `src/ui/terrain` + `src/core/terrain` |
| 3 | Road Studio | `src/ui/roadstudio` + `src/engine/lanemaker` |
| 4 | Train Studio | `src/ui/trainstudio` + `src/engine/lanemaker` |
| 5 | 3D Studio | `src/ui/studio3d` + the OGRE-Next library |

The room-switching itself is managed by `src/core/workspace/WorkspaceManager`
and the window shell `src/app/AppMainWindow` (see doc 03).

## 1. Home - the reception desk

The first screen. It shows:

- A **welcome area** with template cards: "Terrain" and "Road Studio".
  Clicking one starts a new project with a sensible setup.
- A **search box** to filter your projects by name.
- A **recent projects list** - click to reopen a project.

Everything here is handled by `HomeWidget`. A "project" is saved as a
`.ogproj` file (see doc 07) and managed by `ProjectManager`.

## 2. Terrain Studio - the map room

Here you work with the real world:

- **Search for a place** (SearchBar) and see it on a **satellite map**
  (the MapViewportWidget uses the MapLibre library to draw map tiles).
- **Select an area** - for example a 3x3 grid of map tiles.
- **Download data**: elevation grids (DEM files) and imagery tiles.
  Different online providers are supported (`src/core/terrain/providers`):
  GPXZ and OpenTopography for elevation, Mapbox/Esri for imagery.
  Some need a personal **API key** (like a password for that service).
- **Export** the selected area as files other tools can use:
  GeoTIFF heightmaps, PNG images, tile packages. The heavy lifting is
  done by `ExportEngine`, `RasterWriter` and `DemDecoder`.
- **Masks** (paint-over areas that include/exclude zones) and basic
  **terrain analysis** (slope, elevation stats) live in `src/core/terrain`.

The downloaded terrain is stored in a shared **TerrainStore**, so all other
rooms can use the same ground data.

## 3. Road Studio - the road-drawing hall

The heart of the app. It embeds a complete road editor called
**LaneMaker** (`src/engine/lanemaker`). What you can do:

- **Draw roads** with the Road tool (keyboard shortcut **R**), or
  **View** mode (**Esc**) to select, pan and zoom. Only these two tools
  are exposed - the UI is deliberately simple.
- **Cross-Section Studio**: pick a road profile preset (city street,
  highway, country road, ... - 14 presets), then adjust lane count,
  lane widths, direction, speed limit, sidewalks, curbs.
- **Junctions** are detected automatically where roads meet.
- **Traffic signs, road markings and furniture** are generated for you.
- **Import real roads** from OpenStreetMap (OsmImportDialog) - the
  import pipeline in `src/core/osm` converts them into editable roads.
- **Drape roads onto terrain** so they follow the hills you downloaded.
- **Export** the network as an OpenDRIVE `.xodr` file - an open standard
  used by driving simulators and road-design software.
- **Undo/redo, traffic simulation (vehicles + signals), and a replay
  window** to watch your design in action.

## 4. Train Studio - the railway hall

A sibling of Road Studio, tuned for **railways**:

- Draw and edit **rail tracks** with the same LaneMaker engine.
- A **rail profile catalog**: standard, narrow, broad, high-speed,
  subway, tram - 9 presets.
- **Import rail networks** from OpenStreetMap (RailOsmImportDialog,
  powered by `src/core/osm/RailImportPipeline`).

## 5. 3D Studio - the model workshop

A full 3D scene editor powered by the **OGRE-Next** graphics library:

- Fly through your scene like in a game engine: hold the **right mouse
  button** and use WASD-style movement.
- **Select, move, duplicate** objects (multi-select and undo/redo are
  supported).
- **Import 3D models**: FBX, glTF, OBJ (via the Assimp library in
  `src/core/assets`), including their materials and textures.
- **Drape models onto the terrain** so buildings sit on the ground.
- Panels: EditorPanels (left/right panels), PropertiesEditor (details of
  the selected object), NPanel.

## How the rooms share data

All rooms work on the **same project**. The terrain you download in
Terrain Studio becomes the ground for Road Studio's "drape" feature and
for the 3D Studio. When you save, everything (terrain settings + road
network) is written into the project folder and the `.ogproj` file.