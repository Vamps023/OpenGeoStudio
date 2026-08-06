import { create } from 'zustand';
import type { StoreState, TerrainProfile } from '../../shared/types/terrain';

const defaultProfile: TerrainProfile = {
  id: 'balanced',
  name: 'Balanced',
  description: 'Good quality with reasonable download sizes',
  resolution: {
    heightmapSize: 1024,
    albedoSize: 1024,
    pixelSizeM: 10,
  },
  sources: {
    demSource: 'aws-terrarium',
    imagerySource: 'arcgis',
    enableOSM: false,
  },
  processing: {
    normalizeHeights: true,
    heightScale: 1.0,
    seamStitching: true,
    fillNodata: true,
    generateRoadMasks: true,
    generateWaterMasks: true,
    generateVegetationMasks: false,
    generateBuildingMasks: false,
    generateCliffMasks: true,
    cliffThresholdDegrees: 45.0,
  },
};

export const useTerrainStore = create<StoreState>((set) => ({
  // Initial state
  selectedBounds: null,
  activeProfile: defaultProfile,
  generationPlan: null,
  activeJobId: null,
  jobProgress: null,
  outputPath: null,
  selectedPreset: 'babylon',
  heightmapFormat: 'float32',     // matches Babylon preset default
  albedoFormat: 'png',            // matches Babylon preset default
  heightmapResolution: 512,       // matches Babylon preset default
  albedoResolution: 1024,         // matches Babylon preset default
  exportedManifest: null,
  exportedPackagePath: null,
  // Live scene data from SceneBuilder (built from ProjectContext, no export required)
  liveSceneData: null as any,
  demSource: 'aws-terrarium',
  imagerySource: 'arcgis',
  imageryZoom: 0,
  crsSource: 'EPSG:4326',
  gladArdInterval: 920,
  tileSizeKm: 4,
  tileGrid: null,
  selectedTiles: new Set<string>(),
  maskSettings: {
    generateRoadMask: false,
    generateWaterMask: false,
    generateVegetationMask: false,
    generateBuildingMask: false,
    generateCliffMask: false,
    cliffThresholdDegrees: 45,
    roadLineWidthPx: 3,
  },
  buildingsVisible: true,
  roadsVisible: true,
  signsVisible: true,
  satelliteVisible: true,
  demVisible: true,
  activeTab: 'map',
  exportProgress: null,
  exportResult: null,
  exportStartTime: null,
  notifications: [],

  // Actions
  setSelectedBounds: (bounds) => set({ selectedBounds: bounds }),
  setActiveProfile: (profile) => set({ activeProfile: profile }),
  setGenerationPlan: (plan) => set({ generationPlan: plan }),
  setActiveJobId: (jobId) => set({ activeJobId: jobId }),
  setJobProgress: (progress) => set({ jobProgress: progress }),
  setOutputPath: (path) => set({ outputPath: path }),
  setSelectedPreset: (preset) => set({ selectedPreset: preset }),
  setHeightmapFormat: (format) => set({ heightmapFormat: format }),
  setAlbedoFormat: (format) => set({ albedoFormat: format }),
  setDEMSource: (source) => set({ demSource: source }),
  setImagerySource: (source) => set({ imagerySource: source }),
  setCRSSource: (crs) => set({ crsSource: crs }),
  setGladArdInterval: (interval) => set({ gladArdInterval: interval }),
  setImageryZoom: (zoom) => set({ imageryZoom: zoom }),
  setHeightmapResolution: (res) => set({ heightmapResolution: res }),
  setAlbedoResolution: (res) => set({ albedoResolution: res }),
  setTileSizeKm: (size) => set({ tileSizeKm: size }),
  setTileGrid: (grid) => set({ tileGrid: grid }),
  toggleTileSelection: (row, col) => set((state) => {
    const key = `${row},${col}`;
    const newSet = new Set(state.selectedTiles);
    if (newSet.has(key)) {
      newSet.delete(key);
    } else {
      newSet.add(key);
    }
    return { selectedTiles: newSet };
  }),
  selectAllTiles: () => set((state) => {
    if (!state.tileGrid) return {};
    const all = new Set<string>();
    for (const tile of state.tileGrid.tiles) {
      all.add(`${tile.row},${tile.col}`);
    }
    return { selectedTiles: all };
  }),
  deselectAllTiles: () => set({ selectedTiles: new Set<string>() }),
  setSelectedTiles: (tiles) => set({ selectedTiles: tiles }),
  setMaskSettings: (settings) => set((state) => ({
    maskSettings: { ...state.maskSettings, ...settings },
  })),
  setBuildingsVisible: (visible) => set({ buildingsVisible: visible }),
  setRoadsVisible: (visible) => set({ roadsVisible: visible }),
  setSignsVisible: (visible) => set({ signsVisible: visible }),
  setSatelliteVisible: (visible) => set({ satelliteVisible: visible }),
  setDemVisible: (visible) => set({ demVisible: visible }),
  setActiveTab: (tab) => set({ activeTab: tab }),
  setExportedData: (manifest, packagePath) => set({ exportedManifest: manifest, exportedPackagePath: packagePath }),
  setLiveSceneData: (data) => set({ liveSceneData: data }),
  setExportProgress: (p) => set({ exportProgress: p }),
  setExportResult: (r) => set({ exportResult: r }),
  setExportStartTime: (t) => set({ exportStartTime: t }),
  addNotification: (n) => set((state) => ({
    notifications: [...state.notifications, { id: crypto.randomUUID(), ...n }],
  })),
  removeNotification: (id) => set((state) => ({
    notifications: state.notifications.filter((n) => n.id !== id),
  })),

  resetGeneration: () => set({
    generationPlan: null,
    activeJobId: null,
    jobProgress: null,
  }),
  // ─── Project state capture / restore ─────────────────────────
  // Serializes the terrain editing session into a plain object that
  // can be stored in Project.moduleState.terrain and restored on reopen.
  captureTerrainState: () => {
    const s = useTerrainStore.getState();
    return {
      selectedBounds: s.selectedBounds,
      selectedPreset: s.selectedPreset,
      heightmapFormat: s.heightmapFormat,
      albedoFormat: s.albedoFormat,
      heightmapResolution: s.heightmapResolution,
      albedoResolution: s.albedoResolution,
      demSource: s.demSource,
      imagerySource: s.imagerySource,
      imageryZoom: s.imageryZoom,
      crsSource: s.crsSource,
      gladArdInterval: s.gladArdInterval,
      tileSizeKm: s.tileSizeKm,
      tileGrid: s.tileGrid,
      selectedTiles: Array.from(s.selectedTiles),
      maskSettings: s.maskSettings,
      buildingsVisible: s.buildingsVisible,
      roadsVisible: s.roadsVisible,
      signsVisible: s.signsVisible,
      satelliteVisible: s.satelliteVisible,
      demVisible: s.demVisible,
      activeTab: s.activeTab,

    };
  },
  restoreTerrainState: (state: Record<string, unknown> | undefined) => {
    if (!state) return;
    const t = state as {
      selectedBounds?: import('../../shared/types/terrain').GeoBounds | null;
      selectedPreset?: import('../../shared/types/terrain').ExportPreset;
      heightmapFormat?: import('../../shared/types/terrain').HeightmapFormat;
      albedoFormat?: import('../../shared/types/terrain').AlbedoFormat;
      heightmapResolution?: number;
      albedoResolution?: number;
      demSource?: import('../../shared/types/terrain').DEMSource;
      imagerySource?: import('../../shared/types/terrain').ImagerySource;
      imageryZoom?: number;
      crsSource?: import('../../shared/types/terrain').CRSSource;
      gladArdInterval?: number;
      tileSizeKm?: number;
      tileGrid?: import('../../shared/types/terrain').TileGrid | null;
      selectedTiles?: string[];
      maskSettings?: import('../../shared/types/terrain').MaskSettings;
      buildingsVisible?: boolean;
      roadsVisible?: boolean;
      signsVisible?: boolean;
      satelliteVisible?: boolean;
      demVisible?: boolean;
      activeTab?: string;
    };
    set({
      selectedBounds: t.selectedBounds ?? null,
      selectedPreset: t.selectedPreset ?? 'babylon',
      heightmapFormat: t.heightmapFormat ?? 'float32',
      albedoFormat: t.albedoFormat ?? 'png',
      heightmapResolution: t.heightmapResolution ?? 512,
      albedoResolution: t.albedoResolution ?? 1024,
      demSource: t.demSource ?? 'aws-terrarium',
      imagerySource: t.imagerySource ?? 'arcgis',
      imageryZoom: t.imageryZoom ?? 0,
      crsSource: t.crsSource ?? 'EPSG:4326',
      gladArdInterval: t.gladArdInterval ?? 920,
      tileSizeKm: t.tileSizeKm ?? 4,
      tileGrid: t.tileGrid ?? null,
      selectedTiles: new Set(t.selectedTiles ?? []),
      maskSettings: t.maskSettings ?? {
        generateRoadMask: false,
        generateWaterMask: false,
        generateVegetationMask: false,
        generateBuildingMask: false,
        generateCliffMask: false,
        cliffThresholdDegrees: 45,
        roadLineWidthPx: 3,
      },
      buildingsVisible: t.buildingsVisible ?? true,
      roadsVisible: t.roadsVisible ?? true,
      signsVisible: t.signsVisible ?? true,
      satelliteVisible: t.satelliteVisible ?? true,
      demVisible: t.demVisible ?? true,
      activeTab: t.activeTab ?? 'map',

    });
  },
  resetAll: () => set({
    selectedBounds: null,
    generationPlan: null,
    activeJobId: null,
    jobProgress: null,
    outputPath: null,
    selectedPreset: 'babylon',
    heightmapFormat: 'float32',
    albedoFormat: 'png',
    heightmapResolution: 512,
    albedoResolution: 1024,
    exportedManifest: null,
    exportedPackagePath: null,
    liveSceneData: null as any,
    demSource: 'aws-terrarium',
    imagerySource: 'arcgis',
    imageryZoom: 0,
    crsSource: 'EPSG:4326',
    gladArdInterval: 920,
    tileSizeKm: 4,
    tileGrid: null,
    selectedTiles: new Set<string>(),
    maskSettings: {
      generateRoadMask: false,
      generateWaterMask: false,
      generateVegetationMask: false,
      generateBuildingMask: false,
      generateCliffMask: false,
      cliffThresholdDegrees: 45,
      roadLineWidthPx: 3,
    },
    buildingsVisible: true,
    roadsVisible: true,
    signsVisible: true,
    satelliteVisible: true,
    demVisible: true,
    activeTab: 'map',
    exportProgress: null,
    exportResult: null,
    exportStartTime: null,
    notifications: [],
  }),
}));

// Expose store on window for debugging/testing
if (typeof window !== 'undefined') {
  (window as unknown as { __TERRAIN_STORE__?: typeof useTerrainStore }).__TERRAIN_STORE__ = useTerrainStore;
  (window as unknown as { useTerrainStore?: typeof useTerrainStore }).useTerrainStore = useTerrainStore;
}
