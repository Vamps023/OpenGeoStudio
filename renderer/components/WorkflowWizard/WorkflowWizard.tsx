/**
 * WorkflowWizard — step-by-step guided terrain generation flow.
 *
 * Guides the user through:
 *   Step 1: Choose Area (draw bbox on map)
 *   Step 2: Choose DEM Source
 *   Step 3: Choose Satellite Imagery
 *   Step 4: Choose Target Engine
 *   Step 5: Generate Terrain
 *   Step 6: Validate & Continue
 *
 * The wizard is a floating overlay panel that sits on top of the map viewport.
 * It uses progressive disclosure — only showing the current step.
 * Each step unlocks the next.
 */

import React, { useState, useMemo, useEffect } from 'react';
import {
  MapPin, Mountain, Satellite, Gamepad, Download, CheckCircle,
  ChevronRight, ChevronLeft, AlertCircle, Loader2, Zap, X,
} from 'lucide-react';
import { useTerrainStore } from '../../core/store';
import { useCoreStore } from '../../core/coreStore';
import { Native, Settings } from '../../core/ipc';
import type { ExportPreset, DEMSource, ImagerySource, ApiKeys } from '../../../shared/types/terrain';
import { getPresetConfig } from '../../../modules/export/client/ExportPanel/presets';

// ─── Step Definitions ─────────────────────────────────────────

type StepId = 'area' | 'dem' | 'imagery' | 'engine' | 'generate' | 'validate';

interface StepDef {
  id: StepId;
  label: string;
  icon: React.ComponentType<{ size?: number; className?: string }>;
  description: string;
}

const STEPS: StepDef[] = [
  { id: 'area',     label: 'Choose Area',     icon: MapPin,    description: 'Draw a bounding box on the map' },
  { id: 'dem',      label: 'DEM Source',      icon: Mountain,  description: 'Select elevation data provider' },
  { id: 'imagery',  label: 'Satellite',       icon: Satellite, description: 'Select satellite imagery source' },
  { id: 'engine',   label: 'Target Engine',   icon: Gamepad,   description: 'Choose export format' },
  { id: 'generate', label: 'Generate',        icon: Download,  description: 'Export terrain package' },
  { id: 'validate', label: 'Validate',        icon: CheckCircle, description: 'Check output quality' },
];

// ─── DEM Source Options (simplified) ──────────────────────────

const DEM_SOURCES: { id: DEMSource; label: string; description: string; requiresKey: boolean }[] = [
  { id: 'aws-terrarium',    label: 'AWS Terrarium (Mapzen)',  description: '~30m resolution, free, no API key',  requiresKey: false },
  { id: 'nasa-earthdata',   label: 'Copernicus DEM (NASA)',   description: '~30m resolution, free, no API key',  requiresKey: false },
  { id: 'glad-srtm',        label: 'GLAD SRTM',               description: '~30m resolution, free, no API key',  requiresKey: false },
  { id: 'opentopo-cop30',   label: 'OpenTopography (Copernicus)', description: '~30m, best quality, requires API key', requiresKey: true },
  { id: 'opentopo-srtmgl1', label: 'OpenTopography (SRTM)',  description: '~30m, global, requires API key',     requiresKey: true },
  { id: 'opentopo-usgs10m', label: 'OpenTopography (USGS 3DEP)', description: '~10m, USA only, requires API key', requiresKey: true },
  { id: 'gpxz',             label: 'GPXZ LiDAR',              description: '~5m LiDAR, requires API key',        requiresKey: true },
];

const IMAGERY_SOURCES: { id: ImagerySource; label: string; description: string }[] = [
  { id: 'arcgis',     label: 'ArcGIS World Imagery',  description: 'High quality, free' },
  { id: 'google',     label: 'Google Satellite',      description: 'High quality, free' },
  { id: 'mapbox',     label: 'Mapbox Satellite',      description: 'Requires API key' },
  { id: 'maptiler',   label: 'MapTiler',              description: 'Requires API key' },
  { id: 'glad-ard',   label: 'GLAD ARD Landsat',      description: 'Free, lower resolution' },
];

const ENGINE_PRESETS: { id: ExportPreset; label: string; description: string; recommended?: boolean }[] = [
  { id: 'babylon',    label: 'Babylon.js (Recommended)', description: 'Float32 GeoTIFF + PNG albedo, 512×1024, 3D roads', recommended: true },
  // Non-Babylon engines are hidden for v1. Babylon.js is the only supported engine.
  // { id: 'unreal',     label: 'Unreal Engine',            description: 'R16 heightmap + PNG albedo, 2048×2048' },
  // { id: 'blender',    label: 'Blender',                  description: 'Float32 GeoTIFF + PNG albedo, 2048×2048' },
  // { id: 'unigine',    label: 'UNIGINE',                  description: 'PNG heightmap + PNG albedo, 4096×4096' },
  // { id: 'generic',    label: 'Generic/Custom',           description: 'Float32 GeoTIFF + GeoTIFF albedo, 4096×4096' },
];

// ─── Component ────────────────────────────────────────────────

interface WorkflowWizardProps {
  onDismiss?: () => void;
}

export const WorkflowWizard: React.FC<WorkflowWizardProps> = ({ onDismiss }) => {
  const [currentStep, setCurrentStep] = useState(0);
  const [completedSteps, setCompletedSteps] = useState<Set<number>>(new Set());

  // Terrain store state
  const selectedBounds = useTerrainStore((s) => s.selectedBounds);
  const demSource = useTerrainStore((s) => s.demSource);
  const setDEMSource = useTerrainStore((s) => s.setDEMSource);
  const imagerySource = useTerrainStore((s) => s.imagerySource);
  const setImagerySource = useTerrainStore((s) => s.setImagerySource);
  const selectedPreset = useTerrainStore((s) => s.selectedPreset);
  const setSelectedPreset = useTerrainStore((s) => s.setSelectedPreset);
  const setHeightmapFormat = useTerrainStore((s) => s.setHeightmapFormat);
  const setAlbedoFormat = useTerrainStore((s) => s.setAlbedoFormat);
  const setHeightmapResolution = useTerrainStore((s) => s.setHeightmapResolution);
  const setAlbedoResolution = useTerrainStore((s) => s.setAlbedoResolution);
  const outputPath = useTerrainStore((s) => s.outputPath);
  const setOutputPath = useTerrainStore((s) => s.setOutputPath);
  const selectedTiles = useTerrainStore((s) => s.selectedTiles);
  const exportProgress = useTerrainStore((s) => s.exportProgress);
  const exportResult = useTerrainStore((s) => s.exportResult);
  const setExportProgress = useTerrainStore((s) => s.setExportProgress);
  const setExportResult = useTerrainStore((s) => s.setExportResult);
  const setExportStartTime = useTerrainStore((s) => s.setExportStartTime);
  const addNotification = useTerrainStore((s) => s.addNotification);
  const maskSettings = useTerrainStore((s) => s.maskSettings);
  const crsSource = useTerrainStore((s) => s.crsSource);
  const gladArdInterval = useTerrainStore((s) => s.gladArdInterval);
  const heightmapFormat = useTerrainStore((s) => s.heightmapFormat);
  const albedoFormat = useTerrainStore((s) => s.albedoFormat);
  const heightmapResolution = useTerrainStore((s) => s.heightmapResolution);
  const albedoResolution = useTerrainStore((s) => s.albedoResolution);
  const imageryZoom = useTerrainStore((s) => s.imageryZoom);

  const { activeProject, getProjectExportPath } = useCoreStore();
  const [isExporting, setIsExporting] = useState(false);
  const [apiKeys, setApiKeys] = useState<ApiKeys>({});

  // Load API keys on mount and when centralized settings change
  useEffect(() => {
    Settings.getApiKeys().then(setApiKeys).catch(() => {});
    const onSettingsChanged = () => Settings.getApiKeys().then(setApiKeys).catch(() => {});
    window.addEventListener('ogstudio:api-keys-changed', onSettingsChanged);
    return () => window.removeEventListener('ogstudio:api-keys-changed', onSettingsChanged);
  }, []);

  const step = STEPS[currentStep];

  // Check if each step is complete
  const stepComplete = useMemo(() => {
    const checks: Record<StepId, boolean> = {
      'area':     !!selectedBounds,
      'dem':      !!demSource,
      'imagery':  !!imagerySource,
      'engine':   !!selectedPreset,
      'generate': !!exportResult,
      'validate': !!exportResult,
    };
    return checks;
  }, [selectedBounds, demSource, imagerySource, selectedPreset, exportResult]);

  const canProceed = stepComplete[step.id];

  // Auto-advance completed steps
  useEffect(() => {
    if (canProceed && !completedSteps.has(currentStep)) {
      setCompletedSteps(prev => new Set([...prev, currentStep]));
    }
  }, [canProceed, currentStep, completedSteps]);

  const handleNext = () => {
    if (currentStep < STEPS.length - 1 && canProceed) {
      setCurrentStep(currentStep + 1);
    }
  };

  const handlePrev = () => {
    if (currentStep > 0) {
      setCurrentStep(currentStep - 1);
    }
  };

  const handleSelectPreset = (presetId: ExportPreset) => {
    const config = getPresetConfig(presetId);
    setSelectedPreset(presetId);
    setHeightmapFormat(config.heightmapFormat);
    setAlbedoFormat(config.albedoFormat);
    setHeightmapResolution(config.recommendedRes.heightmap);
    setAlbedoResolution(config.recommendedRes.albedo);
  };

  const handleGenerate = async () => {
    if (!selectedBounds) return;

    // Auto-set output path from project if not set
    let outPath = outputPath;
    if (!outPath && activeProject?.basePath) {
      outPath = await getProjectExportPath();
      if (outPath) setOutputPath(outPath);
    }
    if (!outPath) return;

    setIsExporting(true);
    setExportStartTime(Date.now());
    setExportProgress({ stage: 'Starting', current: 0, total: 1, message: 'Initializing export...', percent: 0 });

    try {
      const sessionId = `wizard-${Date.now()}`;
      const tilesArray = Array.from(selectedTiles);
      const tileToExport = tilesArray[0] ?? '0,0';
      const [row, col] = tileToExport.split(',').map(Number);

      const result = await Native.exportPackage(
        sessionId,
        outPath,
        selectedPreset,
        selectedBounds,
        heightmapFormat,
        albedoFormat,
        heightmapResolution,
        albedoResolution,
        imageryZoom,
        demSource,
        imagerySource,
        apiKeys,
        row,
        col,
        maskSettings,
        true,
        crsSource,
        gladArdInterval,
      );

      setExportResult(result);
      setExportProgress(null);
      addNotification({
        type: 'success',
        title: 'Terrain Generated',
        message: `Export complete: ${result}`,
        timeout: 5000,
      });
    } catch (err: any) {
      setExportResult(null);
      setExportProgress(null);
      addNotification({
        type: 'error',
        title: 'Export Failed',
        message: err?.message ?? 'Failed to generate terrain',
        timeout: 8000,
      });
    } finally {
      setIsExporting(false);
    }
  };

  // ─── Render ─────────────────────────────────────────────────

  return (
    <div className="absolute top-3 right-3 z-20 w-[320px] bg-surface-elevated/95 backdrop-blur-sm border border-edge rounded-lg shadow-overlay overflow-hidden">
      {/* Header */}
      <div className="flex items-center gap-2 px-3 py-2 border-b border-edge bg-surface-panel">
        <Zap size={14} className="text-accent" />
        <span className="text-2xs font-semibold uppercase tracking-wider text-fg-primary">
          Terrain Workflow
        </span>
        <span className="ml-auto text-3xs text-fg-muted">
          Step {currentStep + 1} of {STEPS.length}
        </span>
        {onDismiss && (
          <button
            onClick={onDismiss}
            className="ml-2 p-0.5 rounded hover:bg-white/10 text-fg-muted hover:text-fg-primary transition-colors"
            title="Dismiss wizard"
          >
            <X size={12} />
          </button>
        )}
      </div>

      {/* Step Progress Bar */}
      <div className="flex items-center px-3 py-2 gap-1">
        {STEPS.map((s, i) => (
          <div
            key={s.id}
            className={`h-1 flex-1 rounded-full transition-colors ${
              i < currentStep || completedSteps.has(i)
                ? 'bg-ok'
                : i === currentStep
                  ? 'bg-accent'
                  : 'bg-surface-hover'
            }`}
          />
        ))}
      </div>

      {/* Current Step Content */}
      <div className="px-4 py-3 max-h-[400px] overflow-y-auto">
        {/* Step Header */}
        <div className="flex items-center gap-2 mb-3">
          <div className={`w-7 h-7 rounded flex items-center justify-center ${
            canProceed ? 'bg-ok/20 text-ok' : 'bg-accent/20 text-accent'
          }`}>
            {canProceed ? <CheckCircle size={14} /> : <step.icon size={14} />}
          </div>
          <div>
            <div className="text-xs font-semibold text-fg-primary">{step.label}</div>
            <div className="text-3xs text-fg-muted">{step.description}</div>
          </div>
        </div>

        {/* Step Body */}
        {step.id === 'area' && (
          <div className="space-y-2">
            {selectedBounds ? (
              <div className="bg-ok/10 border border-ok/30 rounded p-2 space-y-1">
                <div className="flex justify-between text-3xs">
                  <span className="text-fg-muted">North</span>
                  <span className="text-fg-primary">{selectedBounds.north.toFixed(4)}°</span>
                </div>
                <div className="flex justify-between text-3xs">
                  <span className="text-fg-muted">South</span>
                  <span className="text-fg-primary">{selectedBounds.south.toFixed(4)}°</span>
                </div>
                <div className="flex justify-between text-3xs">
                  <span className="text-fg-muted">East</span>
                  <span className="text-fg-primary">{selectedBounds.east.toFixed(4)}°</span>
                </div>
                <div className="flex justify-between text-3xs">
                  <span className="text-fg-muted">West</span>
                  <span className="text-fg-primary">{selectedBounds.west.toFixed(4)}°</span>
                </div>
                <div className="flex justify-between text-3xs pt-1 border-t border-ok/20">
                  <span className="text-fg-muted">Tiles</span>
                  <span className="text-ok font-medium">{selectedTiles.size}</span>
                </div>
              </div>
            ) : (
              <div className="bg-surface-hover rounded p-3 text-center">
                <MapPin size={20} className="mx-auto text-fg-muted mb-1" />
                <p className="text-3xs text-fg-secondary">
                  Hold <kbd className="px-1 bg-surface-panel border border-edge rounded text-2xs">Shift</kbd> and drag on the map to draw a selection box
                </p>
              </div>
            )}
          </div>
        )}

        {step.id === 'dem' && (
          <div className="space-y-1.5">
            {DEM_SOURCES.map(src => (
              <button
                key={src.id}
                onClick={() => setDEMSource(src.id)}
                className={`w-full text-left p-2 rounded border transition-colors ${
                  demSource === src.id
                    ? 'bg-accent/15 border-accent/40 text-fg-primary'
                    : 'bg-surface-panel/50 border-edge text-fg-secondary hover:bg-surface-hover'
                }`}
              >
                <div className="flex items-center justify-between">
                  <span className="text-2xs font-medium">{src.label}</span>
                  {src.requiresKey && (
                    <span className="text-3xs text-warn px-1 py-0.5 bg-warn/10 rounded">API Key</span>
                  )}
                </div>
                <p className="text-3xs text-fg-muted mt-0.5">{src.description}</p>
              </button>
            ))}
          </div>
        )}

        {step.id === 'imagery' && (
          <div className="space-y-1.5">
            {IMAGERY_SOURCES.map(src => (
              <button
                key={src.id}
                onClick={() => setImagerySource(src.id)}
                className={`w-full text-left p-2 rounded border transition-colors ${
                  imagerySource === src.id
                    ? 'bg-accent/15 border-accent/40 text-fg-primary'
                    : 'bg-surface-panel/50 border-edge text-fg-secondary hover:bg-surface-hover'
                }`}
              >
                <span className="text-2xs font-medium">{src.label}</span>
                <p className="text-3xs text-fg-muted mt-0.5">{src.description}</p>
              </button>
            ))}
          </div>
        )}

        {step.id === 'engine' && (
          <div className="space-y-1.5">
            {ENGINE_PRESETS.map(preset => (
              <button
                key={preset.id}
                onClick={() => handleSelectPreset(preset.id)}
                className={`w-full text-left p-2 rounded border transition-colors ${
                  selectedPreset === preset.id
                    ? 'bg-accent/15 border-accent/40 text-fg-primary'
                    : 'bg-surface-panel/50 border-edge text-fg-secondary hover:bg-surface-hover'
                }`}
              >
                <span className="text-2xs font-medium">{preset.label}</span>
                <p className="text-3xs text-fg-muted mt-0.5">{preset.description}</p>
              </button>
            ))}
          </div>
        )}

        {step.id === 'generate' && (
          <div className="space-y-3">
            {/* Summary */}
            <div className="bg-surface-panel/50 border border-edge rounded p-2 space-y-1">
              <div className="flex justify-between text-3xs">
                <span className="text-fg-muted">Engine</span>
                <span className="text-fg-primary">{ENGINE_PRESETS.find(p => p.id === selectedPreset)?.label}</span>
              </div>
              <div className="flex justify-between text-3xs">
                <span className="text-fg-muted">DEM</span>
                <span className="text-fg-primary">{DEM_SOURCES.find(d => d.id === demSource)?.label}</span>
              </div>
              <div className="flex justify-between text-3xs">
                <span className="text-fg-muted">Imagery</span>
                <span className="text-fg-primary">{IMAGERY_SOURCES.find(i => i.id === imagerySource)?.label}</span>
              </div>
              <div className="flex justify-between text-3xs">
                <span className="text-fg-muted">Tiles</span>
                <span className="text-fg-primary">{selectedTiles.size}</span>
              </div>
              <div className="flex justify-between text-3xs">
                <span className="text-fg-muted">Output</span>
                <span className="text-fg-primary truncate ml-2 max-w-[180px]">{outputPath ?? 'Project/Exports/'}</span>
              </div>
            </div>

            {/* Export button */}
            <button
              onClick={handleGenerate}
              disabled={isExporting || !selectedBounds || !outputPath}
              className="w-full flex items-center justify-center gap-2 bg-accent hover:bg-accent/90 text-fg-primary text-xs font-medium py-2 rounded transition-colors disabled:opacity-40 disabled:cursor-not-allowed"
            >
              {isExporting ? (
                <>
                  <Loader2 size={14} className="animate-spin" />
                  Exporting...
                </>
              ) : (
                <>
                  <Download size={14} />
                  Generate Terrain
                </>
              )}
            </button>

            {/* Progress */}
            {exportProgress && (
              <div className="bg-info/10 border border-info/30 rounded p-2">
                <div className="text-3xs text-info mb-1">{exportProgress.stage}</div>
                <div className="h-1 bg-surface-hover rounded-full overflow-hidden">
                  <div
                    className="h-full bg-info transition-all"
                    style={{ width: `${exportProgress.percent ?? 0}%` }}
                  />
                </div>
                <div className="text-3xs text-fg-muted mt-1">{exportProgress.message}</div>
              </div>
            )}

            {exportResult && (
              <div className="bg-ok/10 border border-ok/30 rounded p-2 flex items-center gap-2">
                <CheckCircle size={14} className="text-ok shrink-0" />
                <span className="text-3xs text-ok">Export complete!</span>
              </div>
            )}
          </div>
        )}

        {step.id === 'validate' && (
          <div className="space-y-2">
            {exportResult ? (
              <>
                <div className="bg-ok/10 border border-ok/30 rounded p-3 text-center">
                  <CheckCircle size={24} className="mx-auto text-ok mb-1" />
                  <p className="text-2xs text-ok font-medium">Terrain Generated Successfully</p>
                  <p className="text-3xs text-fg-muted mt-1">{exportResult}</p>
                </div>
                <div className="space-y-1">
                  <button
                    onClick={() => {
                      // Switch to 3D Scene workspace
                      useCoreStore.getState().activateWorkspace('3d-scene');
                    }}
                    className="w-full flex items-center justify-center gap-2 bg-surface-hover hover:bg-surface-active text-fg-primary text-xs py-2 rounded transition-colors"
                  >
                    <Gamepad size={14} />
                    Preview in 3D
                  </button>
                  <button
                    onClick={() => {
                      // Switch to GIS workspace
                      useCoreStore.getState().activateWorkspace('gis');
                    }}
                    className="w-full flex items-center justify-center gap-2 bg-surface-hover hover:bg-surface-active text-fg-primary text-xs py-2 rounded transition-colors"
                  >
                    <ChevronRight size={14} />
                    Continue to GIS
                  </button>
                </div>
              </>
            ) : (
              <div className="bg-warn/10 border border-warn/30 rounded p-2 flex items-center gap-2">
                <AlertCircle size={14} className="text-warn shrink-0" />
                <span className="text-3xs text-warn">Generate terrain first</span>
              </div>
            )}
          </div>
        )}
      </div>

      {/* Footer Navigation */}
      <div className="flex items-center justify-between px-3 py-2 border-t border-edge bg-surface-panel">
        <button
          onClick={handlePrev}
          disabled={currentStep === 0}
          className="flex items-center gap-1 text-3xs text-fg-secondary hover:text-fg-primary disabled:opacity-30 disabled:cursor-not-allowed transition-colors"
        >
          <ChevronLeft size={12} />
          Back
        </button>
        <span className="text-3xs text-fg-muted">
          {canProceed && currentStep < STEPS.length - 1 ? 'Ready' : ''}
        </span>
        <button
          onClick={handleNext}
          disabled={!canProceed || currentStep === STEPS.length - 1}
          className="flex items-center gap-1 text-3xs text-accent hover:text-accent/80 disabled:opacity-30 disabled:cursor-not-allowed transition-colors"
        >
          Next
          <ChevronRight size={12} />
        </button>
      </div>
    </div>
  );
};
