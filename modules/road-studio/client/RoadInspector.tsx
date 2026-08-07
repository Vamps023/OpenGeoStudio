/**
 * RoadInspector — Property panel for selected road.
 *
 * Shows road metadata, lane network details, adapter report,
 * and mesh statistics from the Phase 2 buildRoad() pipeline.
 *
 * Registered as a right-dock panel in the Road Studio workspace.
 */

import React, { useEffect, useState } from 'react';
import { Settings, AlertTriangle, CheckCircle2, Road, Layers, Ruler, GitBranch } from 'lucide-react';
import { useRoadStudioStore } from './store/roadStudioStore';
import { roadEngine, type RoadBuildResult } from '../shared/roadEngineClient';
import { ROAD_PROFILES } from '../shared/types';
import { PanelHeader } from '../../../renderer/components/common/PanelHeader';
import { FormSection, NumberField, TextField, SelectField, BooleanField } from '../../../renderer/components/common/FormFields';

export const RoadInspector: React.FC = () => {
  const store = useRoadStudioStore();
  const selectedRoadId = store.selection.roadId;
  const road = store.roads.find((r) => r.id === selectedRoadId);
  const refLat = store.refLat;
  const refLon = store.refLon;

  const [buildResult, setBuildResult] = useState<RoadBuildResult | null>(null);
  const [loading, setLoading] = useState(false);

  // Fetch build result when selected road changes
  useEffect(() => {
    if (!road || road.points.length < 2) {
      setBuildResult(null);
      return;
    }
    setLoading(true);
    roadEngine.buildRoad(road, refLat, refLon)
      .then((result) => setBuildResult(result))
      .catch((err) => {
        console.error('[RoadInspector] buildRoad failed:', err);
        setBuildResult(null);
      })
      .finally(() => setLoading(false));
  }, [road, refLat, refLon]);

  if (!road) {
    return (
      <div className="flex flex-col h-full bg-surface-base">
        <PanelHeader icon={Settings} title="Road Inspector" description="Select a road to inspect" />
        <div className="flex-1 flex items-center justify-center text-fg-muted text-xs">
          No road selected
        </div>
      </div>
    );
  }

  // Compute road length from control points (approximate)
  const computeRoadLength = (): number => {
    let len = 0;
    for (let i = 1; i < road.points.length; i++) {
      const dx = road.points[i].lat - road.points[i - 1].lat;
      const dy = road.points[i].lon - road.points[i - 1].lon;
      // Convert lat/lon to meters (approximate)
      const mx = dx * 111000;
      const my = dy * 111000 * Math.cos((road.points[i].lat * Math.PI) / 180);
      len += Math.sqrt(mx * mx + my * my);
    }
    return len;
  };

  const roadLength = computeRoadLength();
  const profile = road.profile;
  const profileNames = Object.keys(ROAD_PROFILES);

  return (
    <div className="flex flex-col h-full bg-surface-base overflow-hidden">
      <PanelHeader
        icon={Settings}
        title="Road Inspector"
        description="Road properties and lane engine diagnostics"
      />

      <div className="flex-1 overflow-y-auto">
        {/* ─── Basic Properties ─── */}
        <FormSection title="Road" icon={Road} defaultOpen>
          <TextField
            label="Name"
            value={road.name}
            onChange={(v) => {
              // Update road name in store
              useRoadStudioStore.setState((s) => ({
                roads: s.roads.map((r) => r.id === road.id ? { ...r, name: v } : r),
              }));
            }}
          />
          <div className="flex items-center justify-between text-xs">
            <span className="text-fg-muted">ID</span>
            <span className="font-mono text-fg-secondary text-[10px] truncate max-w-[160px]">{road.id}</span>
          </div>
          <NumberField
            label="Width"
            value={road.width}
            unit="m"
            step={0.5}
            min={2}
            max={50}
            onChange={(v) => {
              useRoadStudioStore.setState((s) => ({
                roads: s.roads.map((r) => r.id === road.id ? { ...r, width: v } : r),
              }));
            }}
          />
          <NumberField
            label="Lanes"
            value={road.laneCount}
            step={1}
            min={1}
            max={10}
            onChange={(v) => {
              useRoadStudioStore.setState((s) => ({
                roads: s.roads.map((r) => r.id === road.id ? { ...r, laneCount: Math.round(v) } : r),
              }));
            }}
          />
          <div className="flex items-center justify-between text-xs">
            <span className="text-fg-muted">Length</span>
            <span className="text-fg-secondary">{roadLength.toFixed(1)} m</span>
          </div>
          <div className="flex items-center justify-between text-xs">
            <span className="text-fg-muted">Control Points</span>
            <span className="text-fg-secondary">{road.points.length}</span>
          </div>
        </FormSection>

        {/* ─── Profile ─── */}
        <FormSection title="Profile" icon={Layers} defaultOpen>
          <SelectField
            label="Type"
            value={profile.type}
            options={profileNames}
            onChange={(v) => {
              const newProfile = ROAD_PROFILES[v] || profile;
              useRoadStudioStore.setState((s) => ({
                roads: s.roads.map((r) => r.id === road.id ? { ...r, profile: { ...newProfile } } : r),
              }));
            }}
          />
          <div className="flex items-center justify-between text-xs">
            <span className="text-fg-muted">Surface</span>
            <span className="text-fg-secondary">{profile.surfaceTexture}</span>
          </div>
          <div className="flex items-center justify-between text-xs">
            <span className="text-fg-muted">Lane Width</span>
            <span className="text-fg-secondary">{profile.laneWidth.toFixed(1)} m</span>
          </div>
          <BooleanField
            label="Sidewalk"
            value={profile.hasSidewalk}
            onChange={(v) => {
              useRoadStudioStore.setState((s) => ({
                roads: s.roads.map((r) => r.id === road.id ? { ...r, profile: { ...r.profile, hasSidewalk: v } } : r),
              }));
            }}
          />
          <BooleanField
            label="Curb"
            value={profile.hasCurb}
            onChange={(v) => {
              useRoadStudioStore.setState((s) => ({
                roads: s.roads.map((r) => r.id === road.id ? { ...r, profile: { ...r.profile, hasCurb: v } } : r),
              }));
            }}
          />
        </FormSection>

        {/* ─── Lane Engine (Phase 2) ─── */}
        <FormSection title="Lane Engine" icon={GitBranch} defaultOpen>
          {loading && (
            <div className="text-xs text-fg-muted py-2">Building...</div>
          )}
          {buildResult && !loading && (
            <>
              <div className="flex items-center justify-between text-xs">
                <span className="text-fg-muted">Lane Sections</span>
                <span className="text-fg-secondary">{buildResult.lanes.numLaneSections}</span>
              </div>
              <div className="flex items-center justify-between text-xs">
                <span className="text-fg-muted">Centerlines</span>
                <span className="text-fg-secondary">{buildResult.lanes.numCenterlines}</span>
              </div>
              <div className="flex items-center justify-between text-xs">
                <span className="text-fg-muted">Boundaries</span>
                <span className="text-fg-secondary">{buildResult.lanes.numBoundaries}</span>
              </div>
              <div className="flex items-center justify-between text-xs">
                <span className="text-fg-muted">Road Marks</span>
                <span className="text-fg-secondary">{buildResult.markings.numMarkings}</span>
              </div>
              <div className="flex items-center justify-between text-xs">
                <span className="text-fg-muted">Total Length</span>
                <span className="text-fg-secondary">{buildResult.lanes.totalLength.toFixed(1)} m</span>
              </div>
            </>
          )}
          {!buildResult && !loading && (
            <div className="text-xs text-fg-muted py-2">Build data unavailable</div>
          )}
        </FormSection>

        {/* ─── Lane Details ─── */}
        {buildResult && buildResult.lanes.centerlines.length > 0 && (
          <FormSection title="Lanes" icon={Road} defaultOpen={false}>
            {buildResult.lanes.centerlines.map((cl) => (
              <div key={cl.laneId} className="flex items-center justify-between text-xs py-1 border-b border-edge/50 last:border-0">
                <span className="font-mono text-fg-secondary">
                  {cl.laneId === 0 ? 'Center' : cl.laneId > 0 ? `Right +${cl.laneId}` : `Left ${cl.laneId}`}
                </span>
                <div className="flex items-center gap-3 text-fg-muted">
                  <span>{cl.length.toFixed(1)}m</span>
                  <span>{cl.numSamples} pts</span>
                </div>
              </div>
            ))}
          </FormSection>
        )}

        {/* ─── Mesh Statistics ─── */}
        {buildResult && (
          <FormSection title="Mesh" icon={Ruler} defaultOpen={false}>
            <div className="flex items-center justify-between text-xs">
              <span className="text-fg-muted">Vertices</span>
              <span className="text-fg-secondary">{buildResult.totalVertices}</span>
            </div>
            <div className="flex items-center justify-between text-xs">
              <span className="text-fg-muted">Triangles</span>
              <span className="text-fg-secondary">{buildResult.totalTriangles}</span>
            </div>
            <div className="flex items-center justify-between text-xs">
              <span className="text-fg-muted">Sections</span>
              <span className="text-fg-secondary">{buildResult.meshSections.length}</span>
            </div>
            {buildResult.meshSections.map((sec, i) => (
              <div key={i} className="flex items-center justify-between text-[10px] text-fg-muted py-0.5">
                <span className="font-mono">{sec.material}</span>
                <span>{sec.vertexCount}v / {sec.triangleCount}t</span>
              </div>
            ))}
          </FormSection>
        )}

        {/* ─── Adapter Report ─── */}
        {buildResult && (
          <FormSection title="Adapter" icon={AlertTriangle} defaultOpen={false}>
            <div className="flex items-center gap-2 text-xs py-1">
              {buildResult.adapter.exact ? (
                <><CheckCircle2 size={12} className="text-green-400" /><span className="text-green-400">Exact conversion</span></>
              ) : (
                <><AlertTriangle size={12} className="text-yellow-400" /><span className="text-yellow-400">Legacy fallback</span></>
              )}
            </div>
            <div className="flex items-center justify-between text-xs">
              <span className="text-fg-muted">Segments</span>
              <span className="text-fg-secondary">{buildResult.adapter.numSegments}</span>
            </div>
            <div className="flex items-center justify-between text-xs">
              <span className="text-fg-muted">Exact</span>
              <span className="text-green-400">{buildResult.adapter.exactSegments}</span>
            </div>
            <div className="flex items-center justify-between text-xs">
              <span className="text-fg-muted">Legacy</span>
              <span className="text-yellow-400">{buildResult.adapter.legacySegments}</span>
            </div>
            <div className="flex items-center justify-between text-xs">
              <span className="text-fg-muted">Unsupported</span>
              <span className="text-red-400">{buildResult.adapter.unsupportedSegments}</span>
            </div>
            {buildResult.adapter.warnings.length > 0 && (
              <div className="mt-2 space-y-1">
                {buildResult.adapter.warnings.slice(0, 5).map((w, i) => (
                  <div key={i} className="text-[10px] text-yellow-400/80 font-mono leading-tight">
                    {w}
                  </div>
                ))}
                {buildResult.adapter.warnings.length > 5 && (
                  <div className="text-[10px] text-fg-muted">+{buildResult.adapter.warnings.length - 5} more...</div>
                )}
              </div>
            )}
          </FormSection>
        )}

        {/* ─── Intersections ─── */}
        <FormSection title="Connections" icon={GitBranch} defaultOpen={false}>
          <div className="flex items-center justify-between text-xs">
            <span className="text-fg-muted">Start Node</span>
            <span className="text-fg-secondary font-mono text-[10px]">{road.startIntersectionId ?? 'none'}</span>
          </div>
          <div className="flex items-center justify-between text-xs">
            <span className="text-fg-muted">End Node</span>
            <span className="text-fg-secondary font-mono text-[10px]">{road.endIntersectionId ?? 'none'}</span>
          </div>
        </FormSection>
      </div>
    </div>
  );
};

export default RoadInspector;
