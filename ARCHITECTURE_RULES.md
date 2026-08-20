# OpenGeoStudio-Qt — Architecture Rules

> **Mandatory rules for all developers and AI agents.**
> Violating these rules can break the build, cause runtime crashes, or
> introduce security vulnerabilities.

## 1. RoadV2 Header Safety

**Rule:** Never include both `road_v2.hpp` headers in the same translation unit.

- **Internal:** `src/engine/road/road_v2.hpp` (full `LaneSection` from `lane_engine.hpp`)
- **Public:** `src/engine/road/road_engine/public/road_v2.hpp` (placeholder `LaneSection`)

**Convention:** OSM headers that use `RoadV2` must only be included from `.cpp` files in the main app (not from headers) to avoid conflict with the public placeholder `road_v2.hpp` used by `road_engine.hpp`.

**Enforcement:** Manual (not enforced by build system). Code review required.

## 2. LaneMaker Global State

**Rule:** Do not assume Road Studio and Train Studio are independent instances.

LaneMaker uses process-wide global pointers that are rebound on `showEvent()`:
- `g_mainWindow`, `g_laneConfig`, `g_mapViewGL`, `g_Pointer*`, `g_preference`

And process-wide singletons that are **never** rebound:
- `World::Instance()`, `ChangeTracker::Instance()`, `ActionManager::Instance()`
- `SignRegistry::Instance()`, `MarkingRegistry::Instance()`, `FurnitureRegistry::Instance()`
- `SpatialIndexer::Instance()`, `SnapSettings::Instance()`, `MeasurementSystem::Instance()`

**Before changing** `MainWidget`, `MainWindow`, `MapViewGL`, `LaneConfigWidget`, `ActionManager`, or `World` (LaneMaker), verify global ownership and workspace rebinding.

**Use `LaneMakerContext::current()`** (from `lanemaker_context.h`) to get a snapshot of the active instance's pointers.

## 3. UI Must Not Block

**Rule:** UI must not perform blocking network or file operations.

- OSM import, terrain processing, and DEM downloads must run on worker threads or use async patterns.
- Use `QNetworkAccessManager` async signals, not `QEventLoop` blocking.
- Use `QtConcurrent::run()` or `QThread` for CPU-bound work.
- Worker threads must not touch `QWidget` directly — use signals to update UI.

## 4. Project Saves Must Be Transactional

**Rule:** Project saves must use temp file → validate → atomic rename.

- `ProjectManager::save()` and `World::saveToFile()` already follow this pattern.
- Any new save operation must follow the same pattern.
- Never write directly to the final file path without a temp file intermediate.

## 5. Credentials Must Never Be Committed

**Rule:** Never hardcode API keys, passwords, or credentials in source files.

- API keys must be loaded from environment variables (e.g., `GPXZ_API_KEY`, `GLAD_USER`, `GLAD_PASSWORD`).
- API keys must not be persisted in plaintext in `.ogproj` files.
- Use `QProcessEnvironment::systemEnvironment()` to read env vars.
- Test files must skip gracefully if env vars are not set.

## 6. Schema Changes Require Migrations

**Rule:** Any change to `.ogproj` schema must increment `Project::SCHEMA_VERSION` and add a migration function.

- Current schema version: `1` (see `Project.hpp`)
- `Project::fromJson()` auto-upgrades the version field.
- Future migrations: add `migrateV1ToV2()`, etc., and call from `fromJson()`.

## 7. Logging Conventions

**Rule:** Use `appLog()` from `src/core/logger/Logger.hpp` for all application logging.

- Do NOT add new `qDebug()` / `qWarning()` calls in application code.
- Do NOT log API keys, credentials, or sensitive file contents.
- LaneMaker's spdlog usage is separate — leave untouched.
- Test executables may use `qDebug()` directly.

## 8. Error Handling

**Rule:** Core pipelines must return `Result{success, errorMessage}` — not throw exceptions across module boundaries.

- Use structured error information where practical.
- UI should show user-facing errors via `QMessageBox`.
- Logs should contain technical details.

## 9. Build Environment

**Rule:** Use VS 2022 **BuildTools** vcvars64.bat, NOT Community.

- Community edition causes `STL1001: Unexpected compiler version`.
- Path: `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat`

## 10. Q_OBJECT and AUTOMOC

**Rule:** Q_OBJECT headers included from `.cpp` files need a corresponding `.cpp` entry in CMakeLists.txt for AUTOMOC.

- If you include a Q_OBJECT header from a `.cpp` file, add that `.cpp` to the CMake target sources.
- Otherwise MOC generation fails with link errors.

## 11. Hardcoded Paths

**Rule:** Do not hardcode machine-specific paths in CMakeLists.txt or source files.

- Use `CACHE PATH` variables with `$ENV{...}` fallbacks.
- OGRE-Next: `OGRE_NEXT_DIR` (CMake var or env var)
- MapLibre: `QMAPLIBRE_DIR` (CMake var or env var)
- Qt: `CMAKE_PREFIX_PATH` (standard CMake)
- vcpkg: `CMAKE_TOOLCHAIN_FILE` (standard CMake)

## 12. CTest Registration

**Rule:** All test targets must be registered with `add_test()`.

- All tests are under `if(BUILD_TESTS)` — not `BUILD_TESTING`.
- CI passes `BUILD_TESTS=ON`.
- Each `add_executable(test_*)` must have a corresponding `add_test(NAME test_* COMMAND test_*)`.

## 13. Worker Thread Safety

**Rule:** Worker threads must not touch `QWidget` or `QOpenGLWidget` directly.

- Use signal/slot connections (queued) to update UI from worker threads.
- `QImage` and other non-GUI Qt objects are safe to use from worker threads.
- OpenGL calls must only happen on the GUI thread.

## 14. Dependency Management

**Rule:** Only add dependencies that are actually used.

- All vcpkg dependencies must have at least one consumer in the codebase.
- Before removing a dependency, verify it's not used (grep for includes).
- `boost-optional` is used by `change_tracker.h` — keep it.
- `cereal` is used by `action_manager.cpp`, `replay_window.cpp`, `preference.cpp` — keep it.
- `gtest` is used by legacy LaneMaker test headers — keep it.

## 15. Do Not Touch LaneMaker Internals

**Rule:** LaneMaker's internal spdlog, cereal, and CGAL usage must be left untouched.

- LaneMaker is an embedded engine with its own logging and serialization.
- Changes to LaneMaker internals can break both Road Studio and Train Studio.
- Use `rebindGlobals()` and `LaneMakerContext` for external access.
