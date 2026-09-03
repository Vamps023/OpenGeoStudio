# OpenGeoStudio

A native **C++20 / Qt 6** Windows desktop application for **geospatial road
and railway design**: pick an area on a satellite map, download real terrain
elevation and imagery, draw roads and rail tracks (or import them from
OpenStreetMap), and export to open standards (OpenDRIVE, GeoTIFF).

## New here? Read the Beginner's Guide.

The [docs/](docs/README.md) folder contains a complete plain-English tour
written for readers with **zero coding background**:

| Doc | Contents |
|-----|----------|
| [Start Here](docs/README.md) | Reading order and the 30-second summary |
| [What Is This App?](docs/01-what-is-this-app.md) | The one-page answer |
| [The Five Workspaces](docs/02-the-five-workspaces.md) | Home, Terrain, Road, Train, 3D |
| [Tour of the Folders](docs/03-tour-of-the-folders.md) | What every folder is for |
| [How the App Works Inside](docs/04-how-the-app-works-inside.md) | Behind the screen |
| [Glossary](docs/05-glossary.md) | Every technical word translated |
| [Build, Run and Test](docs/06-build-run-and-test.md) | For when you want to compile it |
| [File Formats](docs/07-file-formats.md) | .ogproj, .xodr, GeoTIFF, OSM... |

## The five workspaces

Home (projects) - Terrain Studio (maps + elevation) - Road Studio
(drawing/editing roads, powered by the LaneMaker engine) - Train Studio
(railways) - 3D Studio (OGRE-Next scene editor with FBX/glTF/OBJ import).

## Building from source

See [docs/06-build-run-and-test.md](docs/06-build-run-and-test.md) for the
full walkthrough. Short version: MSVC Build Tools + CMake/Ninja + Qt 6.8 +
vcpkg (`vcpkg.json` lists the libraries), configure once, then
`cmake --build build --target OpenGeoStudio`.

Developers and AI agents: see [AGENTS.md](AGENTS.md) for build commands,
test targets and project conventions.

## Running

Use a portable ZIP or installer from GitHub Releases, or run the freshly
built `OpenGeoStudio.exe` from the build output. No installation required
for the portable build.

## License

MIT (the application). Included third-party libraries carry their own
licenses (Apache-2.0 for libOpenDRIVE/LaneMaker, MIT for pugixml,
ISC for earcut) - see [docs/03-tour-of-the-folders.md](docs/03-tour-of-the-folders.md)
and the source headers.