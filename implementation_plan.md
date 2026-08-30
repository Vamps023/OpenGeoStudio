# Implementation Plan

## Overview

Complete UI refactor of OpenGeoStudio: restructure the monolithic 1,151-line
`src/app/main.cpp` into proper components, introduce a single-source theme
system (design tokens + central stylesheet), sweep all hardcoded color values
out of the app and LaneMaker engine UI, and apply a visual polish pass on top.

**Design direction:** refined GitHub-dark. Both the app shell and LaneMaker
already use the same palette (`#0d1117/#161b22/#21262d/#30363d/#e6edf3`,
accents `#06b6d4`, `#3fb950`, `#f85149`, `#1f6feb`) — so instead of inventing
a new look, we formalize it as tokens, fix the inconsistencies (mixed accent
colors, ad-hoc spacing, per-widget one-off stylesheets), and polish
typography, spacing, radii, hover/focus states.

**Scope:** `src/app/main.cpp`, `src/ui/**` workspace widgets, and the styling
layer of `src/engine/lanemaker/ui/` + `src/engine/lanemaker/widgets/`
(47 `setStyleSheet` calls in `main_widget.cpp` alone). No behavior changes:
workspace switching, project I/O, pipelines, and signals stay as-is.
No new dependencies.

**Current problems being fixed:**

1. **Monolith** — `main.cpp` contains `SettingsDialog`, `CommandPalette`,
   `AppMainWindow` (~900 lines) plus `main()`; every future change lands here.
2. **No theme system** — a ~150-line stylesheet string inline in `main()`;
   60+ scattered per-widget `setStyleSheet` calls with hardcoded hex across
   12 app files and 4 LaneMaker files. Changing one color means grepping.
3. **Visual inconsistency** — accents mixed (`#06b6d4` cyan in shell,
   `#3fb950` green / `#1f6feb` blue in LaneMaker for the same roles:
   selection / primary action); inconsistent paddings, radii (4/6px), font
   sizes (11–14px); duplicated dock-title styles in `setupDockWidgets()`
   that the global stylesheet already covers.

## Types

No behavioral classes beyond those listed under Files. Theme is a header-only
namespace of constants — no QObject, no runtime theming.

```cpp
// src/theme/Theme.hpp  (new)
namespace ogs::theme {
    // Semantic color tokens (constexpr hex strings; QColor conveniences too)
    namespace c {
        inline constexpr const char* BgBase    = "#0d1117"; // window bg
        inline constexpr const char* BgSurface = "#161b22"; // panels/cards
        inline constexpr const char* BgOverlay = "#21262d"; // inputs/hover
        inline constexpr const char* BgActive  = "#1c2128"; // pressed
        inline constexpr const char* Border    = "#30363d";
        inline constexpr const char* BorderHi  = "#484f58";
        inline constexpr const char* Text      = "#e6edf3";
        inline constexpr const char* TextMuted = "#7d8590";
        inline constexpr const char* TextFaint = "#484f58";
        inline constexpr const char* Accent    = "#06b6d4"; // primary/cyan
        inline constexpr const char* AccentSoft= "rgba(6,182,212,0.15)";
        inline constexpr const char* Success   = "#3fb950";
        inline constexpr const char* Danger    = "#f85149";
        inline constexpr const char* Warning   = "#d29922";
        inline constexpr const char* Info      = "#1f6feb";
    }
    // Spacing / radius / font scale
    inline constexpr int SpaceS = 4, SpaceM = 8, SpaceL = 12;
    inline constexpr int RadiusS = 4, RadiusM = 6;
    inline constexpr int FontSmall = 11, FontNormal = 13, FontTitle = 14;

    QString appStylesheet();   // full global QSS (moved from main())
}
```

## Files

| File | Change |
|---|---|
| `src/theme/Theme.hpp` | **New.** Design tokens + `appStylesheet()` (header-only) |
| `src/app/AppMainWindow.hpp/.cpp` | **New.** `AppMainWindow` moved out of `main.cpp` |
| `src/app/SettingsDialog.hpp/.cpp` | **New.** Settings dialog moved out of `main.cpp` |
| `src/app/CommandPalette.hpp/.cpp` | **New.** Command palette moved out of `main.cpp` |
| `src/app/main.cpp` | **Slim to ~150 lines:** includes + dark palette + `theme::appStylesheet()` + `main()` |
| `CMakeLists.txt` | Add six new `src/app/*.{hpp,cpp}` files to the `OpenGeoStudio` target sources |
| `src/ui/home/HomeWidget.cpp` | Token-based styles; polish welcome screen |
| `src/ui/terrain/TerrainStudioWidget.hpp` | Replace inline hex stylesheets with objectName + global QSS |
| `src/ui/terrain/SearchBar.hpp`, `LayerStack.hpp`, `ExportPanel.cpp` | Token-ify stylesheets |
| `src/ui/roadstudio/RoadStudioWidget.cpp`, `OsmImportDialog.hpp` | Token-ify stylesheets |
| `src/ui/trainstudio/TrainStudioWidget.cpp` | Token-ify stylesheets |
| `src/ui/studio3d/Studio3DWidget.cpp`, `PropertiesEditor.cpp`, `EditorPanels.cpp` | Token-ify stylesheets |
| `src/engine/lanemaker/ui/main_widget.cpp` | Replace 47 hardcoded-hex `setStyleSheet` calls with tokens |
| `src/engine/lanemaker/widgets/LaneConfigWidget.*`, `DrawOptionDialog.*`, `AnimatedPopupDialog.cpp` | Same token sweep (~13 calls) |

Include path note: `Theme.hpp` at `src/theme/` lets both sides include it
relatively — `../theme/Theme.hpp` from `src/app/`, `../../theme/Theme.hpp`
from `src/engine/lanemaker/ui/`. No new CMake target (header-only; both
targets already link Qt Widgets).

## Functions

### Moved (no logic change)

- `AppMainWindow` — all methods (`setupMenuBar`, `setupRoadStudioMenus`,
  `setupToolBar`, `setupStatusBar`, `setupCenterWidget`, `setupDockWidgets`,
  `onWorkspaceActivated`, `onProjectChanged`/`onProjectOpened`,
  `saveProjectState`, `onNewProject`, `onOpenProject`, command-palette
  wiring) → `src/app/AppMainWindow.{hpp,cpp}`: class decl + members in the
  header, method bodies in the .cpp.
- `SettingsDialog` → `src/app/SettingsDialog.{hpp,cpp}`
- `CommandPalette` → `src/app/CommandPalette.{hpp,cpp}`

### Modified

- `main()` (slimmed `main.cpp`) — replace the inline ~150-line
  `app.setStyleSheet(...)` literal with
  `app.setStyleSheet(ogs::theme::appStylesheet())`; keep dark-palette block.
- `AppMainWindow::setupDockWidgets()` — delete per-dock `setStyleSheet`
  calls duplicating the global QDockWidget title rule; placeholder label
  gets an objectName covered by global QSS.

### New

- `ogs::theme::appStylesheet()` — builds the global QSS from tokens.

### Removed

- The inline stylesheet literal in `main()`.
- Hardcoded hex literals inside widget-level `setStyleSheet` calls
  (replaced by token references, or deleted where the global sheet suffices).

## Classes

| Class | File | Change |
|---|---|---|
| `AppMainWindow` | `src/app/AppMainWindow.{hpp,cpp}` | New home (moved), unchanged interface |
| `SettingsDialog` | `src/app/SettingsDialog.{hpp,cpp}` | Moved |
| `CommandPalette` | `src/app/CommandPalette.{hpp,cpp}` | Moved |
| `HomeWidget`, `TerrainStudioWidget`, `ExportPanel`, `LayerStack`, `SearchBar`, `RoadStudioWidget`, `OsmImportDialog`, `TrainStudioWidget`, `Studio3DWidget`, `PropertiesEditor`, `EditorPanels` | existing | Styling only — signatures untouched |
| LaneMaker `MainWidget`, `LaneConfigWidget`, `DrawOptionDialog`, `AnimatedPopupDialog` | existing | Styling only — signatures untouched |

## Visual refresh specifics ("modern and clean" pass)

1. **One accent** — cyan `#06b6d4` is the single primary accent everywhere;
   green reserved strictly for success states, red for danger/delete.
   LaneMaker's green primary buttons become accent-cyan.
2. **Uniform chrome** — toolbars at consistent content height, 6px radii,
   13px base font, 11px muted hints; one shared panel-header style rule via
   global QSS (objectName convention).
3. **Focus & hover** — consistent accent focus ring on inputs, `BgOverlay`
   hover on rows/buttons app-wide, extended to LaneMaker widgets.
4. **Empty states** — muted-text placeholders share one objectName rule
   instead of three ad-hoc copies.
5. **No layout changes** — splitters/docks/workspaces behave exactly as now.

## Dependencies

None added. Header-only `Theme.hpp`; no new CMake target.

## Testing

1. Full rebuild green (`cmake --build build`), then
   `ctest --output-on-failure` — all 12 targets pass. `test_road_studio_ui`
   exercises LaneConfigWidget / ActionManager / Preferences and guards the
   LaneMaker styling sweep.
2. Manual checklist:
   - All 5 workspaces open, switch, render as before except intended polish.
   - Settings dialog, Command palette, menu bar, toolbar, docks all work
     after the main.cpp split.
   - Project open/save works (`saveProjectState` path untouched).
   - Road Studio menus appear/disappear on workspace switch as before.
   - Grep gate: `#[0-9a-fA-F]{6}` finds no hex outside `src/theme/Theme.hpp`
     in `src/app/`, `src/ui/`, and LaneMaker `ui/` + `widgets/`.
   - Portable package target still builds.

## Implementation Order

1. Create `src/theme/Theme.hpp`: tokens + `appStylesheet()` — port the
   existing global sheet verbatim first (pure move).
2. Slim `src/app/main.cpp`: extract `SettingsDialog`, `CommandPalette`,
   `AppMainWindow` into new files; update `CMakeLists.txt`; build + smoke run.
3. Sweep `src/ui/**`: per-widget hex stylesheets → tokens / objectName
   rules; add shared panel-header + placeholder rules to global QSS.
4. Sweep LaneMaker `ui/` + `widgets/`: hex → tokens; unify accent usage
   (green primaries → cyan).
5. Visual polish pass (spacing/radii/focus consistency) on top of the
   tokenized styles.
6. Full rebuild + ctest + manual checklist + grep gate.

