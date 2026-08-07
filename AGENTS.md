# OpenGeoStudio — Agent Guide

## Build Commands

```bash
npm run build:electron    # TypeScript compile (app/) → dist-electron/
npm run build:vite        # Vite build (renderer/) → dist/
npm run dev               # Vite dev server (port 5173)
npm run dev:electron      # Full Electron dev (Vite + Electron)
npm run test              # Run all vitest tests
npm run test:watch        # Watch mode
```

## Architecture

- **Electron main process:** `app/` (compiled to `dist-electron/app/`)
- **Renderer:** `renderer/` + `modules/*/client/` (Vite, port 5173)
- **Shared core:** `core/` (DI, events, commands, filesystem, logger)
- **Modules:** `modules/` (terrain, export — active)
- **Tests:** `tests/` (vitest)

## Active Workspaces

1. **Home** — Recent projects, quick actions, create project
2. **Terrain** — Map area selection (shift+drag), TIFF/PNG download via Export panel
3. **Road Studio** — Road network design with C++ geometry engine

## C++ Road Engine

- **Native addon:** `app/native/road_engine/` (node-gyp, Electron target)
- **Source:** `app/native/src/road/` (header-only C++20)
- **Bridge:** `app/native/src/road_bridge.cpp` (N-API bindings)
- **Build:** `cd app/native/road_engine && npx node-gyp build`
- **Tests:** `tests/road-engine.test.ts` (130 tests, vitest)
- **C++ Tests:** `geometry_segment_tests.cpp` (261 doctest tests, 2210 assertions)
- **Architecture:** `docs/ROAD_ENGINE_PHASE1.md` (Phase 1 API reference)
- **Phase:** 1 Complete (API frozen, tagged `phase1-complete`)

### C++ Engine Files

| File | Purpose |
|------|---------|
| `geometry.hpp` | Math kernel (Point2D, Vec2, intersections, offsets, bezier) |
| `geometry_segment.hpp` | **[FROZEN]** GeometrySegment, LineSegment, ArcSegment, SpiralSegment, BezierSegment, SegmentSequence |
| `road.hpp` | Road data model (ControlPoint, Road, SegmentMetadata, SegmentKind) |
| `road_v2.hpp` | **[FROZEN]** RoadV2 — new segment-based road model |
| `road_adapter.hpp` | **[FROZEN]** Bidirectional adapter: roadToV2, roadToV2Legacy, roadToV2Auto, roadFromV2 |
| `arc.hpp` | Circular arc computation with tangent continuity |
| `clothoid.hpp` | Euler spiral (clothoid) for G2 continuity |
| `intersection.hpp` | Edge-based junction generation with fillet corners |
| `mesh.hpp` | Triangle mesh tessellation (ear-clipping, road strips) |
| `opendrive.hpp` | OpenDRIVE XML export |
| `road_tools.hpp` | SCANeR-style road creation tools (6 tools, emit SegmentMetadata) |

### Road Studio Key Files

- `modules/road-studio/client/RoadViewport.tsx` — MapLibre 2D + Babylon.js 3D rendering
- `modules/road-studio/client/RoadToolbar.tsx` — Tool buttons, debug toggles, elevation editor
- `modules/road-studio/client/store/roadStudioStore.ts` — Zustand store (roads, tools, undo/redo, debug)
- `modules/road-studio/shared/roadEngineClient.ts` — IPC client (19 C++ engine methods)
- `modules/road-studio/shared/types.ts` — Road, ControlPoint, Intersection, GeneratedIntersection types
- `app/handlers/roadEngineHandler.ts` — IPC handler (16 channels)
- `app/preload.ts` — Preload bridge (window.roadEngine.*)

### Debug Mode

- **Toggle:** Ctrl+Shift+G
- **14 layers:** centerline, leftEdge, rightEdge, laneBoundaries, samplePoints,
  intersectionPolygon, trimPoints, tangentPoints, filletArcs, triangulation,
  boundaryIntersections, trimLines, corners, polygonVertices

## Key Files

- `modules/terrain/client/MapViewport/MapViewport.tsx` — Terrain area selection map (MapLibre)
- `modules/export/client/ExportPanel/ExportPanel.tsx` — Export settings (TIFF, PNG, formats)
- `renderer/App.tsx` — Main application shell with workspace switching
- `renderer/panels/RecentProjects/RecentProjects.tsx` — Home/start screen with project templates
- `core/workspace/workspace-manager.ts` — Workspace definitions (home, terrain, road-studio)
- `core/project/project-manager.ts` — Project CRUD and persistence
- `app/handlers/coreIpcHandler.ts` — Core IPC (project, workspace, commands, jobs)
- `app/handlers/exportHandler.ts` — Export operations (heightmap, imagery download)
- `docs/ROAD_ENGINE_NEW_ARCHITECTURE.md` — New architecture design (esmini-inspired)

## Conventions

- TypeScript strict mode
- ESM imports in renderer, CJS in main process
- Server-only code uses `fs`, `child_process`, etc. — never import in renderer
- Client code uses MapLibre, React — never import Node modules
- Zustand for state management (useTerrainStore, useCoreStore)
- Panel system: lazy-loaded React components registered in panelRegistry
