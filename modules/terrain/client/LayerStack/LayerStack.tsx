/**
 * Layer Stack ΓÇö manages terrain generation layers and their visibility.
 *
 * Shows DEM, satellite imagery, and mask layers with toggle controls.
 * Mask layers have additional configuration (road width, cliff threshold).
 */

import React from 'react';
import { Layers, Map, Mountain, Droplets, TreePine, Building, AlertTriangle, Eye, EyeOff } from 'lucide-react';
import { useTerrainStore } from '@renderer/core/store';
import { PanelHeader } from '@renderer/components/common/PanelHeader';
import { EmptyState } from '@renderer/components/common/EmptyState';

export const LayerStack: React.FC = () => {
  const generationPlan = useTerrainStore((s) => s.generationPlan);
  const maskSettings = useTerrainStore((s) => s.maskSettings);
  const setMaskSettings = useTerrainStore((s) => s.setMaskSettings);
  const satelliteVisible = useTerrainStore((s) => s.satelliteVisible);
  const demVisible = useTerrainStore((s) => s.demVisible);
  const setSatelliteVisible = useTerrainStore((s) => s.setSatelliteVisible);
  const setDemVisible = useTerrainStore((s) => s.setDemVisible);

  if (!generationPlan) {
    return (
      <div className="flex flex-col h-full bg-surface-panel">
        <PanelHeader icon={Layers} title="Layers" description="Terrain generation layers" />
        <EmptyState
          icon={Layers}
          title="No generation plan"
          description="Select an area on the map and generate a plan to see layers."
        />
      </div>
    );
  }

  const layers = [
    { id: 'dem', label: 'DEM (Heightmap)', icon: Mountain, active: demVisible, color: 'text-warn', locked: false },
    { id: 'imagery', label: 'Satellite Imagery', icon: Map, active: satelliteVisible, color: 'text-ok', locked: false },
    { id: 'generateRoadMask', label: 'Road Mask', icon: Eye, active: maskSettings.generateRoadMask, color: 'text-info', locked: false },
    { id: 'generateWaterMask', label: 'Water Mask', icon: Droplets, active: maskSettings.generateWaterMask, color: 'text-accent', locked: false },
    { id: 'generateVegetationMask', label: 'Vegetation Mask', icon: TreePine, active: maskSettings.generateVegetationMask, color: 'text-ok', locked: false },
    { id: 'generateBuildingMask', label: 'Building Mask', icon: Building, active: maskSettings.generateBuildingMask, color: 'text-warn', locked: false },
    { id: 'generateCliffMask', label: 'Cliff Mask', icon: AlertTriangle, active: maskSettings.generateCliffMask, color: 'text-err', locked: false },
  ];

  const toggleLayer = (id: string) => {
    switch (id) {
      case 'dem': setDemVisible(!demVisible); break;
      case 'imagery': setSatelliteVisible(!satelliteVisible); break;
      case 'generateRoadMask': setMaskSettings({ generateRoadMask: !maskSettings.generateRoadMask }); break;
      case 'generateWaterMask': setMaskSettings({ generateWaterMask: !maskSettings.generateWaterMask }); break;
      case 'generateVegetationMask': setMaskSettings({ generateVegetationMask: !maskSettings.generateVegetationMask }); break;
      case 'generateBuildingMask': setMaskSettings({ generateBuildingMask: !maskSettings.generateBuildingMask }); break;
      case 'generateCliffMask': setMaskSettings({ generateCliffMask: !maskSettings.generateCliffMask }); break;
    }
  };

  return (
    <div className="flex flex-col h-full bg-surface-panel">
      <PanelHeader
        icon={Layers}
        title="Layers"
        description="Terrain generation layers"
        actions={
          <span className="text-3xs text-fg-muted tabular-nums">
            {generationPlan.tiles.length} tiles ┬╖ ~{generationPlan.estimatedMemoryMb}MB
          </span>
        }
      />

      {/* Layer List */}
      <div className="flex-1 overflow-y-auto">
        {layers.map((layer) => {
          const VisIcon = layer.active ? Eye : EyeOff;
          return (
            <div key={layer.id}>
              <div
                className={`flex items-center gap-3 px-3 py-2.5 border-b border-edge transition-colors ${
                  layer.locked ? 'opacity-50 cursor-not-allowed' : 'hover:bg-surface-hover cursor-pointer'
                }`}
                onClick={() => { if (!layer.locked) toggleLayer(layer.id); }}
                role="button"
                tabIndex={0}
              >
                <layer.icon size={14} className={layer.color} />
                <span className="text-2xs text-fg-primary flex-1">{layer.label}</span>
                <VisIcon size={14} className={layer.active ? 'text-fg-secondary' : 'text-fg-muted'} />
              </div>

              {/* Road Width Slider */}
              {layer.id === 'generateRoadMask' && maskSettings.generateRoadMask && (
                <div className="px-5 py-2 border-b border-edge bg-surface-base/50">
                  <div className="flex items-center justify-between mb-1">
                    <span className="text-3xs text-fg-muted">Road Width</span>
                    <span className="text-3xs text-fg-secondary tabular-nums">{maskSettings.roadLineWidthPx}px</span>
                  </div>
                  <input
                    type="range" min={1} max={10} step={1}
                    value={maskSettings.roadLineWidthPx}
                    onChange={(e) => setMaskSettings({ roadLineWidthPx: Number(e.target.value) })}
                    onClick={(e) => e.stopPropagation()}
                    className="w-full h-1 bg-surface-active rounded-lg appearance-none cursor-pointer accent-accent"
                  />
                </div>
              )}

              {/* Cliff Threshold Slider */}
              {layer.id === 'generateCliffMask' && maskSettings.generateCliffMask && (
                <div className="px-5 py-2 border-b border-edge bg-surface-base/50">
                  <div className="flex items-center justify-between mb-1">
                    <span className="text-3xs text-fg-muted">Cliff Threshold</span>
                    <span className="text-3xs text-fg-secondary tabular-nums">{maskSettings.cliffThresholdDegrees}┬░</span>
                  </div>
                  <input
                    type="range" min={0} max={90} step={1}
                    value={maskSettings.cliffThresholdDegrees}
                    onChange={(e) => setMaskSettings({ cliffThresholdDegrees: Number(e.target.value) })}
                    onClick={(e) => e.stopPropagation()}
                    className="w-full h-1 bg-surface-active rounded-lg appearance-none cursor-pointer accent-accent"
                  />
                </div>
              )}
            </div>
          );
        })}
      </div>
    </div>
  );
};
