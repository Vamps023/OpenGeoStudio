/**
 * RoadStudioWorkspace — Container for the road editing workspace.
 *
 * Layout:
 *   ┌─────────────────────────────────────┐
 *   │ RoadToolbar (tools, elevation, etc)  │
 *   ├─────────────────────────────────────┤
 *   │                                     │
 *   │     RoadViewport (Babylon.js)       │
 *   │     2D top-down / 3D rotate         │
 *   │                                     │
 *   ├─────────────────────────────────────┤
 *   │ Status bar (road count, hint)       │
 *   └─────────────────────────────────────┘
 */

import React, { useEffect } from 'react';
import { RoadViewport } from './RoadViewport';
import { RoadToolbar } from './RoadToolbar';
import { useRoadStudioStore } from './store/roadStudioStore';

export const RoadStudioWorkspace: React.FC = () => {
  const roads = useRoadStudioStore((s) => s.roads);
  const tool = useRoadStudioStore((s) => s.tool);
  const drawingRoadId = useRoadStudioStore((s) => s.drawingRoadId);

  // Keyboard shortcuts for tool switching
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      // Don't intercept if typing in an input
      const target = e.target as HTMLElement;
      if (target.tagName === 'INPUT' || target.tagName === 'TEXTAREA') return;

      if (e.key === 'v' || e.key === 'V') {
        useRoadStudioStore.getState().setTool('select');
      } else if (e.key === 'l' || e.key === 'L') {
        useRoadStudioStore.getState().setTool('line');
      } else if (e.key === 'p' || e.key === 'P') {
        useRoadStudioStore.getState().setTool('pen');
      } else if (e.key === '2') {
        useRoadStudioStore.getState().setViewMode('top');
      } else if (e.key === '3') {
        useRoadStudioStore.getState().setViewMode('perspective');
      } else if (e.key === 'm' || e.key === 'M') {
        const s = useRoadStudioStore.getState();
        s.setShowMapOverlay(!s.showMapOverlay);
      }
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, []);

  const totalPoints = roads.reduce((sum, r) => sum + r.points.length, 0);

  return (
    <div className="flex flex-col h-full bg-surface-base">
      <RoadToolbar />

      {/* Viewport */}
      <div className="flex-1 relative overflow-hidden">
        <RoadViewport
          className="w-full h-full block"
        />

        {/* Overlay hint — bottom left */}
        <div className="absolute bottom-3 left-3 flex items-center gap-3 text-2xs text-fg-muted bg-surface-base/80 backdrop-blur-sm rounded-md px-3 py-1.5 border border-edge">
          <span>
            <kbd className="text-fg-secondary">2</kbd> Top
          </span>
          <span className="text-edge">|</span>
          <span>
            <kbd className="text-fg-secondary">3</kbd> 3D
          </span>
          <span className="text-edge">|</span>
          <span>
            <kbd className="text-fg-secondary">M</kbd> Map
          </span>
          <span className="text-edge">|</span>
          <span>
            <kbd className="text-fg-secondary">Right-drag</kbd> pan
          </span>
          <span className="text-edge">|</span>
          <span>
            <kbd className="text-fg-secondary">Scroll</kbd> zoom
          </span>
          {drawingRoadId && (
            <>
              <span className="text-edge">|</span>
              <span className="text-accent">
                <kbd>Esc</kbd> finish road
              </span>
            </>
          )}
        </div>

        {/* Tool indicator — top right */}
        <div className="absolute top-3 right-3 flex items-center gap-2 text-xs text-fg-secondary bg-surface-base/80 backdrop-blur-sm rounded-md px-3 py-1.5 border border-edge">
          <span className="capitalize font-medium text-fg-primary">{tool}</span>
          <span className="text-fg-muted">tool</span>
        </div>

        {/* Empty state */}
        {roads.length === 0 && (
          <div className="absolute inset-0 flex items-center justify-center pointer-events-none">
            <div className="text-center space-y-2">
              <div className="text-sm text-fg-muted">
                Select the <span className="text-accent font-medium">Line</span> or{' '}
                <span className="text-accent font-medium">Pen</span> tool and click on the grid to start drawing a road
              </div>
              <div className="text-2xs text-fg-muted">
                Use middle mouse button to rotate into 3D view
              </div>
            </div>
          </div>
        )}
      </div>

      {/* Status bar */}
      <div className="flex items-center justify-between h-6 px-3 text-2xs text-fg-muted bg-surface-elevated border-t border-edge shrink-0">
        <span>
          {roads.length} road{roads.length !== 1 ? 's' : ''} · {totalPoints} control point{totalPoints !== 1 ? 's' : ''}
        </span>
        <span>OpenGeoStudio Road Studio</span>
      </div>
    </div>
  );
};
