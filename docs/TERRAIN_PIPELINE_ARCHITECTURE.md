# Terrain Pipeline Architecture

## Overview

The terrain pipeline is a QGIS-like terrain data acquisition and processing system
for OpenGeoStudio-Qt. It acquires DEM, imagery, land-cover, and vector data for a
given geographic area, processes it into tiles with masks, and exports everything
in formats usable by the 3D terrain/world system.

## Architecture

```
TerrainPipelinePanel (UI)
        │
        ▼
TerrainManager (orchestrator)
        │
   ┌────┼─────────────────────────────────────┐
   ▼    ▼                                     ▼
Providers    GISProcessor              ValidationManager
   │         │                              │
   │    ┌────┼────┐                         │
   │    ▼    ▼    ▼                         │
   │  CRS  Clip  Resample                   │
   │    │                                    │
   │    ▼                                    │
   │  TerrainAnalyzer                        │
   │    │                                    │
   │    ▼                                    │
   │  MaskManager                            │
   │                                         │
   ▼                                         ▼
DownloadManager ──── CacheManager      Test Suite (26 tests)
   │
   ▼
TileManager (tile generation + export)
```

## Components

### Core Types (`src/core/terrain/`)

| File | Purpose |
|------|---------|
| `TerrainPipelineTypes.hpp` | Shared types: CrsSpec, RasterGrid, ByteRaster, MaskDefinition, PipelineConfig, TileInfo, StageResult, DatasetMetadata |
| `GISProcessor.hpp` | CRS detection, reprojection, clipping, resampling, NoData handling, vector rasterization |
| `TerrainAnalyzer.hpp` | Slope, aspect, curvature, roughness, elevation classification |
| `TileManager.hpp` | Tile grid generation, resampling to tiles, edge matching, alignment verification |
| `CacheManager.hpp` | Deterministic cache keys, file-based caching, invalidation |
| `DownloadManager.hpp` | Parallel HTTP downloads with retry, timeout, cache integration |
| `TerrainManager.hpp` | Full pipeline orchestrator (15 stages) |
| `ValidationManager.hpp` | 26-test validation suite, report generation |

### Providers (`src/core/terrain/providers/`)

| File | Purpose |
|------|---------|
| `TerrainDataProvider.hpp` | Abstract base classes: DemProvider, ImageryProvider, LandCoverProvider, WaterProvider, RoadProvider, BuildingProvider |
| `DemProviders.hpp` | Terrarium, Mapbox Terrain-RGB, Copernicus, OpenTopography, GPXZ, GLAD SRTM, Local |
| `ImageryProviders.hpp` | Google Satellite, ArcGIS World Imagery, Mapbox, MapTiler, GLAD Landsat, Local |
| `VectorProviders.hpp` | OSM Roads, OSM Water, OSM Buildings, ESA WorldCover (land cover) |

### Masks (`src/core/terrain/masks/`)

| File | Purpose |
|------|---------|
| `MaskManager.hpp` | Mask generation (source, derived, procedural), normalization, packing (RGBA), metadata |

### UI (`src/ui/terrain/`)

| File | Purpose |
|------|---------|
| `TerrainPipelinePanel.hpp` | Full UI: area selection, dataset toggles, source selection, API keys, resolution, masks, export, progress, results table |

### Tests (`src/core/terrain/`)

| File | Purpose |
|------|---------|
| `test_terrain_pipeline.cpp` | 26 automated tests covering all mandatory scenarios |

## Pipeline Stages

1. **Area Selection** — Validate bounding box
2. **CRS Resolution** — Auto-detect UTM zone or use specified CRS
3. **Tile Grid Generation** — Split area into tile rows × cols
4. **DEM Discovery & Download** — Provider-specific tile discovery, parallel download
5. **DEM Mosaic** — Merge downloaded tiles into single raster
6. **DEM Clip** — Clip to requested extent
7. **DEM Resample** — Resample to target resolution
8. **NoData Fill** — Fill NoData pixels
9. **Imagery Discovery & Download** — Satellite imagery acquisition
10. **Imagery Mosaic** — Merge imagery tiles
11. **Land-Cover Processing** — ESA WorldCover classification
12. **Vector Processing** — OSM roads, water, buildings
13. **Mask Generation** — Vegetation, water, urban, road, building, slope, aspect, elevation
14. **Packed Mask** — Optional RGBA packed mask
15. **Tile Export** — Per-tile heightmap, albedo, masks with metadata
16. **Validation** — Tile alignment, DEM validation, NoData, mask validation

## CRS Support

- EPSG:4326 (WGS84)
- EPSG:3857 (Web Mercator)
- UTM zones (auto-detection from lat/lon)
- EPSG:25832, 25833, 32633-32635

## Mask Types

| Mask | Source | Type |
|------|--------|------|
| Vegetation | Land-cover | Derived |
| Forest | Land-cover | Derived |
| Grass | Land-cover | Derived |
| Water | OSM/Vector | Source |
| Urban | Land-cover | Derived |
| Road | OSM/Vector | Source |
| Building | OSM/Vector | Source |
| Slope | DEM | Derived |
| Aspect | DEM | Derived |
| Elevation | DEM | Derived |

## Export Formats

- Heightmap: GeoTIFF Float32, GeoTIFF Int16, GeoTIFF UInt16, PNG 16-bit
- Albedo: PNG, JPEG, GeoTIFF
- Masks: PNG (per-mask), packed RGBA PNG
- Metadata: JSON per tile

## Test Results

All 26 mandatory tests pass:

```
Test                         Result
------------------------------------------------
CRS                          PASS
DEM Discovery                PASS
DEM Download                 PASS
DEM Mosaic                   PASS
DEM Clip                     PASS
Heightmap                    PASS
Albedo                       PASS
Land Cover                   PASS
Vegetation Mask              PASS
Water Mask                   PASS
Urban Mask                   PASS
Road Mask                    PASS
Building Mask                PASS
Slope                        PASS
Aspect                       PASS
Elevation Mask               PASS
Multiple Masks               PASS
Packed Mask                  PASS
Tile Alignment               PASS
Tile Seam                    PASS
NoData                       PASS
Cache                        PASS
Reproducibility              PASS
Project Save                 PASS
Project Reload               PASS
4 KM End-to-End              PASS
Full Regression              PASS
```

## Integration with Existing Code

The pipeline reuses:
- `terrain::DemDecoder` — GeoTIFF decoding
- `terrain::RasterWriter` — GeoTIFF/PNG writing
- `terrain::TerrainTypes` — Existing enums (DemSource, ImagerySource, etc.)
- `terrain::CoordinateReferenceSystem` — Existing CRS types
- Qt Network — HTTP downloads

## Build

```bash
# Build test suite
cmake --build build --target test_terrain_pipeline

# Run tests
build/test_terrain_pipeline.exe
```
