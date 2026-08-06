import type { ExportPreset, HeightmapFormat, AlbedoFormat } from '@types/terrain';

export interface PresetConfig {
  id: ExportPreset;
  name: string;
  desc: string;
  icon: string;
  heightmapFormat: HeightmapFormat;
  albedoFormat: AlbedoFormat;
  recommendedRes: { heightmap: number; albedo: number };
  notes?: string;
}

export const PRESETS: PresetConfig[] = [
  {
    id: 'babylon',
    name: 'Babylon.js (Recommended)',
    desc: '3D viewport preview with roads and buildings',
    icon: 'BJS',
    heightmapFormat: 'float32',
    albedoFormat: 'png',
    recommendedRes: { heightmap: 512, albedo: 1024 },
    notes: 'Float32 GeoTIFF heightmap + PNG albedo + 3D roads/buildings, optimized for 3D viewer',
  },
  // Non-Babylon presets are retained for future versions but hidden from the UI.
  // Babylon.js is the only supported engine for OpenGeoStudio v1.
  {
    id: 'unreal',
    name: 'Unreal Engine',
    desc: '16-bit RAW + PNG albedo + splat',
    icon: 'UE',
    heightmapFormat: 'r16',
    albedoFormat: 'png',
    recommendedRes: { heightmap: 2017, albedo: 2048 },
    notes: 'R16 for heightmap, PNG for albedo',
  },
  {
    id: 'blender',
    name: 'Blender',
    desc: 'Displacement modifier terrain + albedo',
    icon: 'BL',
    heightmapFormat: 'float32',
    albedoFormat: 'png',
    recommendedRes: { heightmap: 2048, albedo: 2048 },
    notes: 'Float32 heightmap + PNG albedo for Blender Displacement modifier',
  },
  {
    id: 'unigine',
    name: 'UNIGINE',
    desc: 'LandscapeLayerMap (.lmap) + materials',
    icon: 'U',
    heightmapFormat: 'png',
    albedoFormat: 'png',
    recommendedRes: { heightmap: 4096, albedo: 4096 },
    notes: 'PNG format — Unigine\'s Image::load() supports PNG, not GeoTIFF',
  },
  {
    id: 'generic',
    name: 'Generic / Custom',
    desc: 'Float32 GeoTIFF bundle for any engine',
    icon: '*',
    heightmapFormat: 'float32',
    albedoFormat: 'geotiff',
    recommendedRes: { heightmap: 4096, albedo: 4096 },
    notes: 'Float32 GeoTIFF (full precision, GDAL compatible)',
  },
];

/**
 * Presets visible in the UI. Babylon.js is the only supported engine for v1.
 * Other presets are retained in PRESETS for future versions but excluded from the UI.
 */
export const VISIBLE_PRESETS: PresetConfig[] = PRESETS.filter(p => p.id === 'babylon');

export const getPresetConfig = (id: ExportPreset): PresetConfig =>
  PRESETS.find((p) => p.id === id) ?? PRESETS[0];
