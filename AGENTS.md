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

## Key Files

- `modules/terrain/client/MapViewport/MapViewport.tsx` — Terrain area selection map (MapLibre)
- `modules/export/client/ExportPanel/ExportPanel.tsx` — Export settings (TIFF, PNG, formats)
- `renderer/App.tsx` — Main application shell with workspace switching
- `renderer/panels/RecentProjects/RecentProjects.tsx` — Home/start screen with project templates
- `core/workspace/workspace-manager.ts` — Workspace definitions (home, terrain)
- `core/project/project-manager.ts` — Project CRUD and persistence
- `app/handlers/coreIpcHandler.ts` — Core IPC (project, workspace, commands, jobs)
- `app/handlers/exportHandler.ts` — Export operations (heightmap, imagery download)

## Conventions

- TypeScript strict mode
- ESM imports in renderer, CJS in main process
- Server-only code uses `fs`, `child_process`, etc. — never import in renderer
- Client code uses MapLibre, React — never import Node modules
- Zustand for state management (useTerrainStore, useCoreStore)
- Panel system: lazy-loaded React components registered in panelRegistry
