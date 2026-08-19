# Cross-Section Studio — Architecture & Design

## Overview

The Cross-Section Studio unifies road/rail profile selection, lane
configuration, and cross-section visualization into a single panel
(`LaneConfigWidget`). It replaces the old top-bar profile dropdown that
was previously in `MainWidget`.

## Components

```
LaneConfigWidget
├── CrossSectionVisual        (custom-painted cross-section preview)
├── Profile combo             (road/rail preset selector)
├── Lane width spinner        (LM::LaneWidth, meters)
├── Lane +/- buttons          (left/right lane count)
├── Swap direction button     (swap left/right)
├── Flip lane button          (flip selected lane direction)
├── Speed limit spinner       (km/h)
├── Sidewalk checkbox         (has sidewalk?)
├── Curb checkbox             (has curb?)
├── Modified indicator        ("Modified from <profile>")
├── Reset button              (reload preset)
└── Save as Preset button     (save current as custom)
```

## Two Instances

| Instance | Created by | `verticalLayout` | `showProfileSelector` | Purpose |
|----------|-----------|-------------------|-----------------------|---------|
| Sidebar | `MainWidget` constructor | `false` | `true` | Full profile + lane editing in the Road Studio sidebar |
| Dialog | `DrawOptionDialog` constructor | `true` | `false` | Lane count/width only — no profile changes allowed in the popup |

### Why `showProfileSelector=false` in DrawOptionDialog

The `DrawOptionDialog` is a popup that appears when the user clicks "Draw
Options". It contains its own `LaneConfigWidget` for quick lane adjustments.
If the profile combo were visible, changing a preset would trigger
`LoadProfile()` → `SetOption()` → `OnOptionChange()` → `ActionManager::Record()`
→ `Save()`, but the dialog's `LaneConfigWidget` has no `ProfileChanged`
connection. This previously caused a crash in the event loop after the preset
change completed.

The fix: hide the profile combo and metadata fields in the dialog instance
so the user can only adjust lane counts and widths there. Profile selection
is only available in the sidebar.

## Profile Catalogs

### Road Profiles (`roads::RoadProfileCatalog`)

Defined in `src/ui/roadstudio/RoadTypes.hpp`.

| Key | Lanes | Width | Sidewalk | Curb | Speed | Description |
|-----|-------|-------|----------|------|-------|-------------|
| `city_2x1` | 1+1 | 3.5 | yes | yes | 40 | City — 1 lane each way |
| `city_2x2` | 2+2 | 3.5 | yes | yes | 50 | City — 2 lanes each way |
| `city_2x3` | 3+3 | 3.5 | yes | yes | 50 | City — 3 lanes each way |
| `city_oneway_1x2` | 0+2 | 3.5 | yes | yes | 40 | City one-way — 2 lanes |
| `city_oneway_1x3` | 0+3 | 3.5 | yes | yes | 40 | City one-way — 3 lanes |
| `country_2x1` | 1+1 | 3.5 | no | no | 80 | Country — 1 lane each way |
| `country_2x2` | 2+2 | 3.5 | no | no | 80 | Country — 2 lanes each way |
| `highway_2x2` | 2+2 | 3.75 | no | no | 100 | Highway — 2 lanes each way |
| `highway_2x3` | 3+3 | 3.75 | no | no | 100 | Highway — 3 lanes each way |
| `highway_2x4` | 4+4 | 3.75 | no | no | 110 | Highway — 4 lanes each way |
| `industrial_2x1` | 1+1 | 4.0 | no | yes | 40 | Industrial — 4.0m lanes |
| `industrial_2x2` | 2+2 | 4.0 | no | yes | 40 | Industrial — 2 lanes each way |
| `expressway_2x3` | 3+3 | 3.75 | no | no | 100 | Expressway — 1m median |
| `expressway_2x4` | 4+4 | 3.75 | no | no | 110 | Expressway — 4 lanes each way |

Default: `city_2x1`. Fallback for unknown key: `custom`.

### Rail Profiles (`roads::RailProfileCatalog`)

| Key | Tracks | Gauge | Spacing | Max Speed | Description |
|-----|--------|-------|---------|-----------|-------------|
| `single_standard` | 1 | 1435mm | 4.0m | 120 | Standard gauge, UIC60 |
| `single_narrow` | 1 | 1067mm | 3.5m | 80 | Narrow gauge, wood sleepers |
| `single_broad` | 1 | 1676mm | 4.5m | 100 | Broad gauge (Indian) |
| `double_standard` | 2 | 1435mm | 4.0m | 120 | Double track |
| `triple_standard` | 3 | 1435mm | 4.0m | 120 | Triple track |
| `quad_standard` | 4 | 1435mm | 4.0m | 120 | Quad track |
| `high_speed` | 1 | 1435mm | 4.5m | 350 | High-speed rail |
| `subway` | 1 | 1435mm | 3.5m | 80 | Subway/metro |
| `tram` | 1 | 1435mm | 3.0m | 60 | Tram/streetcar |

Default: `single_standard`. Fallback for unknown key: `custom_rail`.

## Mode Switching

### User-initiated (shows widget)

- `GotoRoadMode()` — switches to road mode, shows widget, populates road profiles
- `GotoRailMode()` — switches to rail mode, shows widget, populates rail profiles
- `GotoLaneMode()` — switches to lane-only mode, shows widget

### Construction-time (does NOT show widget)

- `SetRoadModeOnly()` — switches to road mode without `show()`, populates
  profiles only if `hasProfileSelector` is true
- `SetRailModeOnly()` — switches to rail mode without `show()`, populates
  profiles only if `hasProfileSelector` is true

These are called by `MainWidget::SetRailMode()` during construction to set
the correct mode without making the widget visible before the user picks
the Road tool.

## Signal Flow

```
User changes profile combo
  → OnProfileComboChanged(index)
  → LoadProfile(key)
     → applyingProfile = true
     → visual->SetOption(left, right)   [QSignalBlocker on visual]
     → laneWidthSpinner->setValue(...)   [QSignalBlocker on spinners]
     → speedLimitSpinner->setValue(...)
     → sidewalkCheck->setChecked(...)
     → curbCheck->setChecked(...)
     → modifiedFromProfile = false
     → applyingProfile = false
  → emit ProfileChanged(key)
  → MainWidget slot: mapViewGL->update()
```

```
User drags lane handle in CrossSectionVisual
  → OptionChangedByUser(left, right)
  → LaneConfigWidget::OnOptionChange(left, right)
     → if (!applyingProfile) ActionManager::Record(left, right)
     → CheckModified()
```

## Modified-from-profile Tracking

- `applyingProfile` guard: prevents `CheckModified()` and
  `RoadMetadataChanged` from firing while a preset is being loaded
- `modifiedFromProfile` flag: set by `CheckModified()` when the current
  config differs from `loadedProfile`
- `modifiedLabel`: shows "Modified from \<profile\>" when true
- `resetButton`: reloads the original preset via `LoadProfile(currentProfileKey)`
- `savePresetButton`: saves the current config as a new custom preset

## Related Files

| File | Purpose |
|------|---------|
| `src/engine/lanemaker/widgets/LaneConfigWidget.h` | Widget declarations |
| `src/engine/lanemaker/widgets/LaneConfigWidget.cpp` | Implementation |
| `src/engine/lanemaker/widgets/DrawOptionDialog.cpp` | Popup dialog (uses `showProfileSelector=false`) |
| `src/engine/lanemaker/ui/main_widget.cpp` | Sidebar integration, `SetRailMode()` |
| `src/ui/roadstudio/RoadTypes.hpp` | `RoadProfile`, `RoadProfileCatalog`, `RailProfile`, `RailProfileCatalog` |
| `src/engine/lanemaker/libOpenDRIVE/include/road_profile.h` | `LM::LanePlan`, `LM::LaneProfile`, `LM::LaneWidth` |
