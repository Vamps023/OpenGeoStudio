# 01 - What Is OpenGeoStudio?

## The one-sentence answer

OpenGeoStudio is a **Windows desktop program for designing terrain, roads,
and railways on top of real-world maps**.

Imagine Google Maps, a drawing app, and a 3D editor combined into one
program - that is roughly OpenGeoStudio.

## What problem does it solve?

People who plan roads or train lines need to work with **real geography**:
hills, valleys, satellite photos, and existing streets. Doing that by hand
is slow. OpenGeoStudio automates it:

1. **Pick an area** on a satellite map (for example, a suburb of Houston).
2. **Download real data** for that area: elevation (the hills) and
   imagery (photos from above).
3. **Draw roads or rail tracks** on top - or **import real roads** from
   OpenStreetMap (the free, editable map of the world).
4. **Design the details**: lane counts, widths, speed limits, sidewalks,
   curbs, traffic signs, road markings, junctions.
5. **Export** the design to standard formats that other engineering
   software understands (GeoTIFF, OpenDRIVE .xodr, 3D models).

## The 5 main screens ("workspaces")

The app is like a house with 5 rooms. You switch rooms from the
left side of the window:

| Room | Name | What you do there |
|------|------|-------------------|
| 1 | **Home** | Start a new project or open an existing one |
| 2 | **Terrain Studio** | Choose an area, download maps + elevation data |
| 3 | **Road Studio** | Draw and edit roads |
| 4 | **Train Studio** | Draw and edit railway tracks |
| 5 | **3D Studio** | View everything in full 3D, place 3D models |

Each room is explained in [02-the-five-workspaces.md](02-the-five-workspaces.md).

## A bit of history (why the code looks the way it does)

OpenGeoStudio was once built with **Electron** - a technology for making
desktop apps out of web pages (JavaScript/TypeScript). It worked, but such
apps can be slow and heavy.

This repository is the **rewrite**: the same idea, rebuilt as a **native
C++ application** using **Qt 6**. "Native" means the app speaks the
computer's own language directly - faster, smaller, and no hidden web
browser inside. You can still see traces of the old app in code comments
such as "Replaces the TypeScript AppContext".

## Quick facts

| Fact | Value |
|------|-------|
| Name | OpenGeoStudio |
| Version | 1.0.0 |
| Platform | Windows 10/11, 64-bit |
| Written in | C++ (the 2020 standard, called "C++20") |
| User-interface toolkit | Qt 6.8 |
| Size | about 244 C++ code files in `src/` |
| License | MIT (the app) + licenses of included libraries |
| Map rendering | MapLibre (satellite imagery), OGRE-Next (3D Studio) |
| 3D model import | FBX, glTF, OBJ (via the Assimp library) |

## What it is NOT

- **Not a web app** - it runs on your PC, no browser needed.
- **Not a game engine** - although the 3D Studio borrows game-engine
  ideas (fly camera, undo/redo) to make editing comfortable.
- **Not AutoCAD** - it is focused on geospatial road/rail/terrain design.
- **It does not need the internet to run** - except when downloading map
  data, and for optional online services that need their own API keys
  (see [06-build-run-and-test.md](06-build-run-and-test.md)).

## Who is this documentation for?

Anyone curious: new team members, students, managers, testers, or the
project owner. You do **not** need to read the code - but after these
docs you will know what every folder is for and what the programmers
mean when they talk about it.