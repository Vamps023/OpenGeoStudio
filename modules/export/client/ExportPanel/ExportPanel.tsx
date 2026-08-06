import React, { useState, useEffect } from 'react';
import { Download, Check, Key, FileUp } from 'lucide-react';
import { useTerrainStore } from '@renderer/core/store';
import { useCoreStore } from '@renderer/core/coreStore';
import { Native, Settings, Dialog, ProjectContextIPC } from '@renderer/core/ipc';
import {
  setPresetWithUndo, setHeightmapFormatWithUndo, setAlbedoFormatWithUndo,
  setDEMSourceWithUndo, setImagerySourceWithUndo,
} from '@renderer/core/undoRedoBridge';
import type { HeightmapFormat, AlbedoFormat, DEMSource, ImagerySource, TerrainManifest, ApiKeys } from '@types/terrain';
import { FsAPI } from '@renderer/core/ipc';
import { getPresetConfig } from './presets';

export const ExportPanel: React.FC = () => {
  const selectedPreset = useTerrainStore((s) => s.selectedPreset);
  const setSelectedPreset = setPresetWithUndo;
  const heightmapFormat = useTerrainStore((s) => s.heightmapFormat);
  const setHeightmapFormat = setHeightmapFormatWithUndo;
  const albedoFormat = useTerrainStore((s) => s.albedoFormat);
  const setAlbedoFormat = setAlbedoFormatWithUndo;
  const outputPath = useTerrainStore((s) => s.outputPath);
  const setOutputPath = useTerrainStore((s) => s.setOutputPath);
  const selectedBounds = useTerrainStore((s) => s.selectedBounds);
  const setExportedData = useTerrainStore((s) => s.setExportedData);
  const setActiveTab = useTerrainStore((s) => s.setActiveTab);
  // Quality settings
  const heightmapResolution = useTerrainStore((s) => s.heightmapResolution);
  const setHeightmapResolution = useTerrainStore((s) => s.setHeightmapResolution);
  const albedoResolution = useTerrainStore((s) => s.albedoResolution);
  const setAlbedoResolution = useTerrainStore((s) => s.setAlbedoResolution);
  const imageryZoom = useTerrainStore((s) => s.imageryZoom);
  const setImageryZoom = useTerrainStore((s) => s.setImageryZoom);
  const demSource = useTerrainStore((s) => s.demSource);
  const setDEMSource = setDEMSourceWithUndo;
  const imagerySource = useTerrainStore((s) => s.imagerySource);
  const setImagerySource = setImagerySourceWithUndo;
  const crsSource = useTerrainStore((s) => s.crsSource);
  const gladArdInterval = useTerrainStore((s) => s.gladArdInterval);
  const selectedTiles = useTerrainStore((s) => s.selectedTiles);
  const setExportProgress = useTerrainStore((s) => s.setExportProgress);
  const exportResult = useTerrainStore((s) => s.exportResult);
  const setExportResult = useTerrainStore((s) => s.setExportResult);
  const setExportStartTime = useTerrainStore((s) => s.setExportStartTime);
  const addNotification = useTerrainStore((s) => s.addNotification);

  // Mask settings (used by export, controlled from Layers tab)
  const maskSettings = useTerrainStore((s) => s.maskSettings);

  const [isExporting, setIsExporting] = useState(false);
  const [showAdvanced, setShowAdvanced] = useState(false);

  // API Keys — loaded from the centralized Settings store (read-only here)
  const [apiKeys, setApiKeys] = useState<ApiKeys>({});

  // Load API keys on mount and when settings dialog may have updated them
  useEffect(() => {
    Settings.getApiKeys().then(setApiKeys).catch(() => {});
    const onSettingsChanged = () => Settings.getApiKeys().then(setApiKeys).catch(() => {});
    window.addEventListener('ogstudio:api-keys-changed', onSettingsChanged);
    return () => window.removeEventListener('ogstudio:api-keys-changed', onSettingsChanged);
  }, []);

  // Auto-set output path from the active project's Exports folder
  // This eliminates the need for the user to browse for a folder
  const { activeProject, getProjectExportPath } = useCoreStore();
  useEffect(() => {
    if (activeProject?.basePath) {
      getProjectExportPath().then((exportPath) => {
        if (exportPath && !outputPath) {
          setOutputPath(exportPath);
        }
      }).catch(() => {});
    }
  }, [activeProject, outputPath, getProjectExportPath, setOutputPath]);

  // Force Babylon.js preset on mount — only supported engine for v1.0
  useEffect(() => {
    const config = getPresetConfig('babylon');
    setSelectedPreset('babylon');
    setHeightmapFormat(config.heightmapFormat);
    setAlbedoFormat(config.albedoFormat);
    setHeightmapResolution(config.recommendedRes.heightmap);
    setAlbedoResolution(config.recommendedRes.albedo);
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []); // intentionally only on mount

  const handleExport = async () => {
    // Resolve output path from the active project's Exports folder
    let exportPath = outputPath;
    if (!exportPath && activeProject?.basePath) {
      try {
        exportPath = await getProjectExportPath();
      } catch { /* fall through */ }
    }
    if (!exportPath) {
      addNotification({ type: 'info', message: 'No project folder found. Create or open a project first.' });
      return;
    }
    if (!selectedBounds) {
      addNotification({ type: 'info', message: 'Please select an area on the map first.' });
      return;
    }
    if (selectedTiles.size === 0) {
      addNotification({ type: 'info', message: 'Please select at least one tile to export.' });
      return;
    }

    // Validate mask settings before export
    if (maskSettings.generateCliffMask && (maskSettings.cliffThresholdDegrees < 0 || maskSettings.cliffThresholdDegrees > 90)) {
      addNotification({ type: 'error', message: 'Cliff threshold must be between 0 and 90 degrees.' });
      return;
    }
    if (maskSettings.generateRoadMask && (maskSettings.roadLineWidthPx < 1 || maskSettings.roadLineWidthPx > 10)) {
      addNotification({ type: 'error', message: 'Road width must be between 1 and 10 pixels.' });
      return;
    }

    try {
      setIsExporting(true);
      setExportResult(null);
      setExportStartTime(Date.now());
      setExportProgress({
        stage: 'init',
        current: 0,
        total: 100,
        message: 'Preparing export...',
        percent: 0,
      });

      // Multi-tile export (works for all presets including Babylon.js)
      const grid = useTerrainStore.getState().tileGrid;
      const tilesToExport = grid?.tiles.filter((t: { row: number; col: number }) =>
        selectedTiles.has(`${t.row},${t.col}`)
      ) ?? [];

      if (tilesToExport.length === 0) {
        addNotification({ type: 'info', message: 'No tiles selected for export.' });
        return;
      }

      const total = tilesToExport.length;
      // Sort tiles by row then col to ensure correct offset calculation
      const sortedTiles = [...tilesToExport].sort((a, b) => {
        if (a.row !== b.row) return a.row - b.row;
        return a.col - b.col;
      });


      // Compute cumulative world offsets for variable-sized tiles
      const centerLat = tilesToExport.reduce((sum, t) => sum + (t.bounds.north + t.bounds.south) / 2, 0) / tilesToExport.length;
      const kmPerDegLat = 111.32;
      const kmPerDegLng = 111.32 * Math.cos((centerLat * Math.PI) / 180);
      const mPerDegLat = kmPerDegLat * 1000;
      const mPerDegLng = kmPerDegLng * 1000;

      const colWidths = new Map<number, number>();
      const rowHeights = new Map<number, number>();
      for (const t of sortedTiles) {
        if (!colWidths.has(t.col)) {
          const widthM = Math.max(1, (t.bounds.east - t.bounds.west) * mPerDegLng);
          colWidths.set(t.col, widthM);
        }
        if (!rowHeights.has(t.row)) {
          const heightM = Math.max(1, (t.bounds.north - t.bounds.south) * mPerDegLat);
          rowHeights.set(t.row, heightM);
        }
      }

      const colOffsetX = new Map<number, number>();
      const rowOffsetZ = new Map<number, number>();
      const minCol = Math.min(...tilesToExport.map(t => t.col));
      const maxCol = Math.max(...tilesToExport.map(t => t.col));
      const minRow = Math.min(...tilesToExport.map(t => t.row));
      const maxRow = Math.max(...tilesToExport.map(t => t.row));

      let currentX = 0;
      for (let c = minCol; c <= maxCol; c++) {
        colOffsetX.set(c, currentX);
        currentX += colWidths.get(c) ?? 0;
      }
      let currentZ = 0;
      for (let r = minRow; r <= maxRow; r++) {
        rowOffsetZ.set(r, currentZ);
        currentZ += rowHeights.get(r) ?? 0;
      }

      // Export each tile
      const failedTiles: string[] = [];
      for (let i = 0; i < sortedTiles.length; i++) {
        const tile = sortedTiles[i];
        const sessionId = `session-${Date.now()}-${i}`;

        // Set progress BEFORE starting this tile (percent = tiles completed so far)
        setExportProgress({
          stage: 'download_dem',
          current: i + 1,
          total,
          message: `Downloading DEM for tile ${i + 1} of ${total}...`,
          percent: Math.round((i / total) * 100),
        });

        // Pass base export path and tile row/col; main process constructs tile path with path.join
        const currentMaskSettings = useTerrainStore.getState().maskSettings;
        const currentDemVisible = useTerrainStore.getState().demVisible;
        try {
          await Native.exportPackage(
            sessionId,
            exportPath,
            selectedPreset,
            tile.bounds,
            heightmapFormat,
            albedoFormat,
            heightmapResolution,
            albedoResolution,
            imageryZoom,
            demSource,
            imagerySource,
            apiKeys,
            tile.row,
            tile.col,
            currentMaskSettings,
            currentDemVisible,
            crsSource,
            gladArdInterval,
          );
        } catch (tileErr) {
          const tileId = `${tile.row},${tile.col}`;
          failedTiles.push(tileId);
          addNotification({ type: 'error', message: `Tile ${tileId} failed: ${tileErr instanceof Error ? tileErr.message : String(tileErr)}` });
        }

        // Update progress AFTER tile completes
        setExportProgress({
          stage: 'write',
          current: i + 1,
          total,
          message: `Tile ${i + 1} of ${total} written.`,
          percent: Math.round(((i + 1) / total) * 100),
        });
      }

      if (failedTiles.length > 0) {
        addNotification({ type: 'error', message: `${failedTiles.length} of ${total} tile(s) failed: ${failedTiles.join(', ')}` });
      }

      // For Babylon.js preset, switch to 3D view after export
      if (selectedPreset === 'babylon') {
        const tileManifests: TerrainManifest[] = [];

        for (const tile of tilesToExport) {
          const tileFolder = `tile_${tile.row}_${tile.col}`;
          // Use forward slash — main process resolves via path.join in fs:readManifest handler
          const tilePath = `${exportPath}/${tileFolder}`;
          const manifest = await FsAPI.readManifest(tilePath) as any;

          if (manifest && manifest.error) {
            addNotification({ type: 'error', message: `Failed to read manifest for tile ${tile.row},${tile.col}: ${manifest.error}` });
            continue;
          }

          const manifestTile = (manifest as TerrainManifest).tiles[0];

          tileManifests.push({
            ...manifest,
            tiles: [{
              ...manifestTile,
              row: tile.row,
              col: tile.col,
              bounds: tile.bounds,
              files: {
                heightmap: manifestTile.files.heightmap ? `${tileFolder}/${manifestTile.files.heightmap}` : undefined,
                albedo: manifestTile.files.albedo ? `${tileFolder}/${manifestTile.files.albedo}` : undefined,
                roadMask: manifestTile.files.roadMask ? `${tileFolder}/${manifestTile.files.roadMask}` : undefined,
                waterMask: manifestTile.files.waterMask ? `${tileFolder}/${manifestTile.files.waterMask}` : undefined,
                vegetationMask: manifestTile.files.vegetationMask ? `${tileFolder}/${manifestTile.files.vegetationMask}` : undefined,
                buildingMask: manifestTile.files.buildingMask ? `${tileFolder}/${manifestTile.files.buildingMask}` : undefined,
                cliffMask: manifestTile.files.cliffMask ? `${tileFolder}/${manifestTile.files.cliffMask}` : undefined,
                buildings3D: manifestTile.files.buildings3D ? `${tileFolder}/${manifestTile.files.buildings3D}` : undefined,
                roads3D: manifestTile.files.roads3D ? `${tileFolder}/${manifestTile.files.roads3D}` : undefined,
              },
            }],
          });
        }

        if (tileManifests.length > 0) {
          const allTiles = tileManifests.flatMap((m) => m.tiles);
          const minRow = Math.min(...allTiles.map((t) => t.row));
          const maxRow = Math.max(...allTiles.map((t) => t.row));
          const minCol = Math.min(...allTiles.map((t) => t.col));
          const maxCol = Math.max(...allTiles.map((t) => t.col));
          const combinedManifest: TerrainManifest = {
            ...tileManifests[0],
            tileGrid: {
              ...tileManifests[0].tileGrid,
              rows: Math.max(1, maxRow - minRow + 1),
              cols: Math.max(1, maxCol - minCol + 1),
            },
            tiles: allTiles,
          };

          await FsAPI.writeManifest(exportPath, combinedManifest);
          setExportedData(combinedManifest, exportPath);

          // Sync terrain metadata to ProjectContext (single source of truth)
          // This allows downstream commands (GIS, Scene) to read terrain data
          // without the renderer passing args.
          const terrainState = useTerrainStore.getState();
          const terrainBounds = terrainState.selectedBounds;
          if (terrainBounds) {
            ProjectContextIPC.syncTerrain({
              bounds: terrainBounds,
              crs: terrainState.crsSource,
              tileSizeKm: terrainState.tileSizeKm,
              heightmapResolution: terrainState.heightmapResolution,
              albedoResolution: terrainState.albedoResolution,
              packagePath: exportPath,
              manifestPath: `${exportPath}/manifest.json`,
              demSource: terrainState.demSource,
              imagerySource: terrainState.imagerySource,
              generatedAt: new Date().toISOString(),
            }).catch(() => { /* non-fatal — sync is best-effort */ });
          }
        }

        setExportResult(`Terrain exported for 3D viewing. ${total} tile(s) saved to: ${exportPath}`);
        setActiveTab('view3d');
      } else {
        setExportResult(`Export complete. ${total} tile(s) saved to: ${exportPath}`);
      }
    } catch (err) {
      setExportResult(`Export failed: ${err instanceof Error ? err.message : String(err)}`);
      addNotification({ type: 'error', message: 'Export failed. See console for details.' });
    } finally {
      setIsExporting(false);
      setExportProgress(null);
      setExportStartTime(null);
    }
  };

  const selectedCount = selectedTiles.size;

  return (
    <div className="flex flex-col h-full bg-surface-panel text-fg-primary">
      {/* Header — compact */}
      <div className="px-3 py-2 border-b border-edge flex items-center justify-between">
        <span className="text-xs font-semibold text-fg-primary">EXPORT</span>
        {selectedBounds && selectedCount > 0 && (
          <span className="text-[10px] text-accent font-medium">{selectedCount} tile{selectedCount !== 1 ? 's' : ''}</span>
        )}
      </div>

      <div className="flex-1 overflow-y-auto p-3 space-y-4">
        {/* Engine badge — minimal */}
        <div className="flex items-center gap-2 text-[10px] text-fg-muted">
          <Check className="w-3 h-3 text-accent" />
          <span>Babylon.js &middot; Float32 GeoTIFF + PNG</span>
        </div>

        {/* Advanced Settings Toggle */}
        <button
          onClick={() => setShowAdvanced(!showAdvanced)}
          className="w-full flex items-center justify-between text-[10px] font-semibold text-fg-secondary uppercase tracking-wider py-1.5 border-t border-edge hover:text-fg-primary transition-colors"
        >
          <span>Settings</span>
          <span className="text-fg-muted">{showAdvanced ? '−' : '+'}</span>
        </button>

        {/* Advanced Settings (collapsed by default) */}
        {showAdvanced && (
          <div className="space-y-6 pb-2">
        {/* Resolution & Quality */}
        <div>
          <h3 className="text-xs font-semibold text-fg-secondary uppercase tracking-wider mb-3">
            Resolution & Quality
          </h3>
          <div className="space-y-3">
            <div className="space-y-1">
              <label className="text-xs text-fg-secondary">Heightmap Resolution</label>
              <select
                value={heightmapResolution}
                onChange={(e) => setHeightmapResolution(Number(e.target.value))}
                className="w-full bg-surface-hover border border-edge rounded text-sm py-1.5 px-2 text-fg-primary"
              >
                <option value={512}>512 × 512 (~150m/pixel)</option>
                <option value={1024}>1024 × 1024 (~75m/pixel)</option>
                <option value={2048}>2048 × 2048 (~37m/pixel)</option>
                <option value={4096}>4096 × 4096 (~18m/pixel)</option>
              </select>
            </div>
            <div className="space-y-1">
              <label className="text-xs text-fg-secondary">Albedo Resolution</label>
              <select
                value={albedoResolution}
                onChange={(e) => setAlbedoResolution(Number(e.target.value))}
                className="w-full bg-surface-hover border border-edge rounded text-sm py-1.5 px-2 text-fg-primary"
              >
                <option value={1024}>1024 × 1024</option>
                <option value={2048}>2048 × 2048</option>
                <option value={4096}>4096 × 4096</option>
                <option value={8192}>8192 × 8192 (Ultra)</option>
              </select>
            </div>
            <div className="space-y-1">
              <label className="text-xs text-fg-secondary">Imagery Zoom Level</label>
              <select
                value={imageryZoom}
                onChange={(e) => setImageryZoom(Number(e.target.value))}
                className="w-full bg-surface-hover border border-edge rounded text-sm py-1.5 px-2 text-fg-primary"
              >
                <option value={0}>Auto (recommended)</option>
                <option value={10}>10 — Low</option>
                <option value={12}>12 — Medium</option>
                <option value={14}>14 — Good</option>
                <option value={16}>16 — High</option>
                <option value={18}>18 — Very High</option>
                <option value={19}>19 — Ultra</option>
                <option value={20}>20 — Extreme (ArcGIS/Mapbox)</option>
                <option value={21}>21 — Max Detail (limited areas)</option>
                <option value={22}>22 — Micro (city blocks only)</option>
              </select>
            </div>
          </div>
        </div>

        {/* API Keys — managed centrally in Settings dialog.
             Show a small warning if a key required by the current data sources is missing. */}
        {(() => {
          const needsOpenTopo = demSource.startsWith('opentopo-');
          const needsMapbox = imagerySource === 'mapbox' || demSource === 'mapbox-terrain-rgb';
          const needsMaptiler = imagerySource === 'maptiler';
          const needsGpxz = demSource === 'gpxz';
          const needsStadia = false; // stadia used by opendrive module, not export
          const missing: string[] = [];
          if (needsOpenTopo && !apiKeys.opentopography) missing.push('OpenTopography');
          if (needsMapbox && !apiKeys.mapbox) missing.push('Mapbox');
          if (needsMaptiler && !apiKeys.maptiler) missing.push('MapTiler');
          if (needsGpxz && !apiKeys.gpxz) missing.push('GPXZ');
          if (needsStadia && !apiKeys.stadia) missing.push('Stadia');
          if (missing.length > 0) {
            return (
              <div className="flex items-center gap-2 bg-warn/10 border border-warn/30 rounded-lg px-3 py-2">
                <Key className="w-3.5 h-3.5 text-warn shrink-0" />
                <span className="text-xs text-fg-secondary flex-1">
                  Missing API key: {missing.join(', ')}
                </span>
                <button
                  onClick={() => window.dispatchEvent(new CustomEvent('ogstudio:open-settings'))}
                  className="text-xs text-accent hover:underline shrink-0"
                >
                  Open Settings →
                </button>
              </div>
            );
          }
          return null;
        })()}

        {/* Data Sources */}
        <div>
          <h3 className="text-xs font-semibold text-fg-secondary uppercase tracking-wider mb-3">
            Data Sources
          </h3>
          <div className="space-y-3">
            <div className="space-y-1">
              <label className="text-xs text-fg-secondary">DEM Source</label>
              <select
                value={demSource}
                onChange={(e) => setDEMSource(e.target.value as DEMSource)}
                className="w-full bg-surface-hover border border-edge rounded text-sm py-1.5 px-2 text-fg-primary"
              >
                <optgroup label="Tiled (no API key)">
                  <option value="aws-terrarium">AWS Terrarium (~30m, free)</option>
                  <option value="mapzen">Mapzen Terrarium (~30m, free)</option>
                  <option value="mapbox-terrain-rgb">Mapbox Terrain-RGB (HD 0.1m, Mapbox token)</option>
                </optgroup>
                <optgroup label="Copernicus (free, no API key, no rate limit)">
                  <option value="nasa-earthdata">Copernicus GLO-30 (~30m, free, no key)</option>
                </optgroup>
                <optgroup label="OpenTopography (free API key, 50 calls/day)">
                  <option value="opentopo-cop30">Copernicus GLO-30 (~30m, best quality)</option>
                  <option value="opentopo-nasadem">NASADEM (~30m, reprocessed)</option>
                  <option value="opentopo-srtmgl1">SRTM GL1 (~30m, global)</option>
                  <option value="opentopo-srtmgl3">SRTM GL3 (~90m, global)</option>
                  <option value="opentopo-aw3d30">ALOS AW3D30 (~30m, global)</option>
                  <option value="opentopo-usgs10m">USGS 3DEP (~10m, USA only)</option>
                </optgroup>
                <optgroup label="GPXZ (high-res, LiDAR-enhanced)">
                  <option value="gpxz">GPXZ LiDAR (5m resolution, API key)</option>
                </optgroup>
                <optgroup label="GLAD (free, no API key)">
                  <option value="glad-srtm">GLAD SRTM (~30m, free, UMD)</option>
                </optgroup>
                <optgroup label="Local File">
                  <option value="local-file">Import GeoTIFF DEM from file...</option>
                </optgroup>
              </select>
              {demSource === 'local-file' && (
                <LocalFileImport
                  label="Import DEM GeoTIFF"
                  filters={[{ name: 'GeoTIFF DEM', extensions: ['tif', 'tiff'] }]}
                  onFileSelected={(filePath) => {
                    useTerrainStore.getState().setDEMSource('local-file');
                    (useTerrainStore.getState() as any).localDemPath = filePath;
                    useTerrainStore.getState().addNotification({
                      type: 'success',
                      title: 'DEM File Selected',
                      message: filePath,
                      timeout: 3000,
                    });
                  }}
                />
              )}
            </div>
            <div className="space-y-1">
              <label className="text-xs text-fg-secondary">Imagery Source</label>
              <select
                value={imagerySource}
                onChange={(e) => setImagerySource(e.target.value as ImagerySource)}
                className="w-full bg-surface-hover border border-edge rounded text-sm py-1.5 px-2 text-fg-primary"
              >
                <option value="google">Google Satellite (free, up-to-date)</option>
                <option value="arcgis">ArcGIS World Imagery (free)</option>
                <option value="mapbox">Mapbox Satellite (token req)</option>
                <option value="maptiler">MapTiler Satellite (token req)</option>
                <option value="glad-ard">GLAD ARD Landsat (free, 30m, UMD)</option>
                <option value="local-file">Import imagery from file...</option>
              </select>
              {imagerySource === 'local-file' && (
                <LocalFileImport
                  label="Import Imagery"
                  filters={[{ name: 'Imagery', extensions: ['png', 'jpg', 'jpeg', 'tif', 'tiff'] }]}
                  onFileSelected={(filePath) => {
                    (useTerrainStore.getState() as any).localImageryPath = filePath;
                    useTerrainStore.getState().addNotification({
                      type: 'success',
                      title: 'Imagery File Selected',
                      message: filePath,
                      timeout: 3000,
                    });
                  }}
                />
              )}
            </div>
            {imagerySource === 'glad-ard' && (
              <div className="space-y-1">
                <label className="text-xs text-fg-secondary">GLAD ARD 16-day Interval ID</label>
                <input
                  type="number"
                  value={gladArdInterval}
                  onChange={(e) => useTerrainStore.getState().setGladArdInterval(parseInt(e.target.value) || 920)}
                  className="w-full bg-surface-hover border border-edge rounded text-sm py-1.5 px-2 text-fg-primary"
                  min={1}
                  max={1000}
                />
                <p className="text-xs text-fg-muted">Interval ~920 ≈ mid-2022. See GLAD 16-day interval table.</p>
              </div>
            )}
            <div className="space-y-1">
              <label className="text-xs text-fg-secondary">Coordinate Reference System (CRS)</label>
              <select
                value={crsSource}
                onChange={(e) => useTerrainStore.getState().setCRSSource(e.target.value as any)}
                className="w-full bg-surface-hover border border-edge rounded text-sm py-1.5 px-2 text-fg-primary"
              >
                <option value="EPSG:4326">EPSG:4326 — WGS84 (lat/lon)</option>
                <option value="EPSG:3857">EPSG:3857 — Web Mercator</option>
                <option value="EPSG:32633">EPSG:32633 — UTM Zone 33N</option>
                <option value="EPSG:32634">EPSG:32634 — UTM Zone 34N</option>
                <option value="EPSG:32635">EPSG:32635 — UTM Zone 35N</option>
                <option value="EPSG:25832">EPSG:25832 — ETRS89 UTM Zone 32N</option>
                <option value="EPSG:25833">EPSG:25833 — ETRS89 UTM Zone 33N</option>
                <option value="auto">Auto (UTM from bounds centroid)</option>
              </select>
              <p className="text-xs text-fg-muted">Used for GeoTIFF georeferencing and OpenDRIVE projection.</p>
            </div>
          </div>
        </div>

        {/* Format Selection */}
        <div>
          <h3 className="text-xs font-semibold text-fg-secondary uppercase tracking-wider mb-3">
            Export Formats
          </h3>
          <div className="space-y-3">
            <div className="space-y-1">
              <label className="text-xs text-fg-secondary">Heightmap Format</label>
              <select
                value={heightmapFormat}
                onChange={(e) => setHeightmapFormat(e.target.value as HeightmapFormat)}
                className="w-full bg-surface-hover border border-edge rounded text-sm py-1.5 px-2 text-fg-primary"
              >
                <option value="none">None (Albedo only)</option>
                <option value="float32">Float32 GeoTIFF (full precision)</option>
                <option value="dem">DEM (Int16 GeoTIFF)</option>
                <option value="geotiff">UInt16 GeoTIFF (normalized)</option>
                <option value="r16">R16 (Raw 16-bit)</option>
                <option value="png">PNG (16-bit grayscale)</option>
              </select>
            </div>
            <div className="space-y-1">
              <label className="text-xs text-fg-secondary">Albedo Format</label>
              <select
                value={albedoFormat}
                onChange={(e) => setAlbedoFormat(e.target.value as AlbedoFormat)}
                className="w-full bg-surface-hover border border-edge rounded text-sm py-1.5 px-2 text-fg-primary"
              >
                <option value="png">PNG (RGB)</option>
                <option value="geotiff">GeoTIFF (RGB)</option>
              </select>
            </div>
          </div>
        </div>
        </div>
        )}
      </div>

      {/* Export Button + Result */}
      <div className="p-3 border-t border-edge space-y-2">
        <button
          onClick={handleExport}
          disabled={isExporting || selectedCount === 0}
          className={`w-full flex items-center justify-center gap-2 py-2 rounded text-xs font-medium transition-colors ${
            selectedCount === 0
              ? 'bg-surface-hover text-fg-muted cursor-not-allowed'
              : isExporting
                ? 'bg-surface-active text-fg-secondary cursor-wait'
                : 'bg-accent hover:bg-accent/90 text-fg-primary'
          }`}
        >
          <Download className="w-3.5 h-3.5" />
          {isExporting
            ? 'Exporting...'
            : selectedCount === 0
              ? 'Select tiles to export'
              : `Export ${selectedCount} Tile${selectedCount !== 1 ? 's' : ''}`}
        </button>

        {exportResult && (
          <p className="text-[10px] text-ok">{exportResult}</p>
        )}
      </div>
    </div>
  );
};

// ─── Local File Import Component ──────────────────────────────
const LocalFileImport: React.FC<{
  label: string;
  filters: Array<{ name: string; extensions: string[] }>;
  onFileSelected: (filePath: string) => void;
}> = ({ label, filters, onFileSelected }) => {
  const [selectedPath, setSelectedPath] = useState<string | null>(null);

  const handleImport = async () => {
    const filePath = await Dialog.importFile({ title: label, filters });
    if (!filePath) return;
    setSelectedPath(filePath);
    onFileSelected(filePath);
  };

  return (
    <div className="flex items-center gap-2 mt-1">
      <button
        onClick={handleImport}
        className="flex items-center gap-1 px-2 py-1 text-xs bg-accent/10 text-accent border border-accent/30 rounded hover:bg-accent/20 transition-colors"
      >
        <FileUp size={12} />
        {selectedPath ? 'Change File...' : label}
      </button>
      {selectedPath && (
        <span className="text-2xs text-fg-muted truncate flex-1">{selectedPath}</span>
      )}
    </div>
  );
};
