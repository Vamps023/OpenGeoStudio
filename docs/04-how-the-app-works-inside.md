# 04 - How the App Works Inside (Behind the Screen)

You click a button; something happens. This document follows the most
common actions step by step, in plain language.

## The four layers

Every feature touches up to four layers, like floors of a building:

```
Floors you visit        What lives there            Folder
------------------------------------------------------------
4. Showrooms            buttons, panels, maps       src/ui
3. Back office          projects, events, OSM,      src/core
                        terrain, world model
2. Factory              road math, road editor,     src/engine
                        rendering
1. Foundations          coordinate translation      src/gis
------------------------------------------------------------
(-1) The decorator      colors, fonts, spacing      src/theme
```

A rule of thumb: **showrooms ask, offices answer, factories do.**
And one shared switchboard - `ApplicationContext` - hands every room
references to the services it needs (projects, events, terrain store).

## What happens when you start the app

`src/app/main.cpp` is the power button. Step by step:

1. **Create the application** (a Qt `QApplication` - the runtime backbone).
2. **Lift Qt's image memory limit** so huge terrain heightmaps can load.
3. **Find the coordinate database** (`proj.db`) next to the program, so
   the translation office (gis) works in a portable install.
4. **Set the window icon** and register built-in resources (icons,
   shaders for LaneMaker).
5. **Open the log file** (`log.txt` next to the exe) - everything the app
   does gets written here, which is the first place to look after a crash.
6. **Apply the dark theme** (palette + stylesheet from `src/theme`).
7. **Create the services** (ApplicationContext) and the **main window**
   (AppMainWindow), which builds the menu bar, toolbar, status bar and
   the 5-room switcher.
8. If you double-clicked a `.ogproj` file (or passed one on the command
   line), it is opened automatically and the app jumps to the 3D Studio.
9. **Wait for events.** From now on the app just reacts: clicks, keys,
   timer ticks. This wait-and-react loop is the heartbeat of every
   desktop app.

## What happens when you create/open a project

- The Home room asks `ProjectManager` to create a project from a
  template (Terrain or Road Studio) or open an existing `.ogproj`.
- A project is a folder on disk plus a small index file. Recent projects
  are remembered in a list.
- On save, `AppMainWindow.saveProjectState` collects everything the
  rooms changed - terrain store settings and the road network (as .xodr)
  - into the project folder so all rooms stay in sync.
- News travels on the **EventBus** (the pinboard): "project opened",
  "project changed". Any room listening updates itself (title bar,
  status bar, recent lists).

## What happens when you draw a road

1. You press **R** (Road tool) in Road Studio and click points on the map.
2. LaneMaker records the points, then asks the **road math engine** to
   turn them into a proper road: smooth curves (arcs/clothoids), a
   **cross-section** (lanes, sidewalks, curbs) copied from your chosen
   preset, and **junctions** where roads touch.
3. The engine turns the math into a **mesh** - thousands of small
   triangles the graphics card can draw - and pushes it to OpenGL.
4. Road markings and signs are generated as extra layers.
5. Every change goes through the **undo/redo** system, so Ctrl+Z works.
6. On export, the network is written as **OpenDRIVE (.xodr)** XML, a
   standard format understood by simulators and road-design tools.

## What happens when you import from OpenStreetMap

1. You pick an area in the import dialog; the pipeline downloads the
   map XML for it.
2. `src/core/osm` then runs an assembly line: parse XML, classify each
   street (motorway, residential, footpath...), build connected network
   segments, detect junctions, generate sensible lanes, roundabouts,
   signs and markings, and validate the result.
3. Everything is converted from OSM's latitude/longitude into your
   project's coordinate system (the gis translation office).
4. The finished roads appear in Road Studio (or rails in Train Studio),
   fully editable like hand-drawn roads.

## What the coordinates really mean (CRS in 2 minutes)

A place on Earth can be written as numbers in many systems - degrees of
latitude/longitude, or meters from some origin (like UTM zones). Each
system is a **Coordinate Reference System (CRS)** with an **EPSG code**
(for example EPSG:4326 = plain lat/long).

Mixing systems without converting = your road ends up in the ocean.
So the app keeps **one project CRS**, and `src/gis` converts anything
that comes in (OSM, tiles, DEM) into it before use. When exporting,
it converts back if the target format needs a specific system.

## Where things live on your disk

| Location | Contents |
|----------|----------|
| Next to `OpenGeoStudio.exe` | `log.txt`, the PROJ database, Qt DLLs |
| Your project folder | `.ogproj` file, terrain tiles, exported .xodr |
| Windows Registry / settings | Recent projects, preferences |

## Why the app rarely crashes when one part fails

Core pipelines return a **result with a success flag and an error
message** instead of blowing up. The UI shows failures as a message box,
and details land in `log.txt`. Deliberate crashes (exceptions) are not
allowed to cross between departments - errors are passed as data.