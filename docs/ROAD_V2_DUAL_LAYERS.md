# RoadV2 — Dual Header Layout (Intentional)

There are **two** `road_v2.hpp` files in the repository. This is intentional
and NOT a mistake. Do not delete or merge them casually.

## The two copies

| Path | Role | LaneSection source | Consumers |
|------|------|--------------------|-----------|
| `src/engine/road/road_v2.hpp` | **Internal** full model | `lane_engine.hpp` (Phase 2.1 full definition: lanes, widths, types) | OSM pipeline (`src/core/osm/*`), `lane_geometry.hpp`, `lane_network.hpp`, `lane_sampling.hpp`, `road_adapter.hpp` |
| `src/engine/road/road_engine/public/road_v2.hpp` | **Public API facade** | Self-contained placeholder `struct LaneSection` | `road_engine.hpp` (public API), `road_engine/public/road_adapter.hpp` |

## Why two copies exist

1. The **public layer** (`road_engine/public/`) is the frozen Phase 1 public
   API. It must remain **self-contained**: it only depends on
   `geometry_segment.hpp`, so downstream consumers of the public API do not
   need to pull in `lane_engine.hpp` or other internal headers.
2. The **internal root layer** uses the real `LaneSection` defined in
   `lane_engine.hpp` (Phase 2.1+). The OSM pipeline builds real lane
   sections (lane widths, types, turn lanes) and needs the full model.

## The include-conflict constraint

Both files define `LaneSection` (one via `lane_engine.hpp`, one as its own
placeholder struct). Including both in the same translation unit causes a
redefinition conflict. This is why:

- OSM headers that use the full `RoadV2` (`src/core/osm/*`) must **only be
  included from `.cpp` files** in the main app — never from a header that
  also pulls in the public `road_engine.hpp` facade.
- See the comment in `src/ui/roadstudio/RoadStudioWidget.cpp`.

## If you must change RoadV2

- Keep both copies' public surface (RoadV2 class API) in sync.
- The root copy is the "source of truth" for LaneSection evolution.
- The public copy's placeholder LaneSection is intentionally minimal;
  expand it only if the public API requires it.
- Verify: `geometry_segment_tests`, `test_osm_pipeline`, and the main
  `OpenGeoStudio` target all compile after any change.
