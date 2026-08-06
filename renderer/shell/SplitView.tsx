/**
 * SplitView — renders two panels side by side with a draggable divider.
 *
 * Used by workspaces with `center: 'split'` to show map + 3D simultaneously.
 * The divider can be dragged to resize (30%-70% range).
 * Double-click the divider to reset to 50%.
 * Keyboard: Ctrl+\ toggles between split and single (left only) view.
 */

import React, { useState, useCallback, useEffect, useRef } from 'react';

interface SplitViewProps {
  left: React.ReactNode;
  right: React.ReactNode;
  /** Initial split percentage for the left panel (default 50) */
  initialSplit?: number;
  /** Minimum percentage for each side (default 30) */
  minPct?: number;
}

export const SplitView: React.FC<SplitViewProps> = ({
  left,
  right,
  initialSplit = 50,
  minPct = 30,
}) => {
  const [splitPct, setSplitPct] = useState(initialSplit);
  const [singleView, setSingleView] = useState(false);
  const dragging = useRef(false);
  const containerRef = useRef<HTMLDivElement>(null);

  const onMouseDown = useCallback((e: React.MouseEvent) => {
    e.preventDefault();
    dragging.current = true;
    document.body.style.cursor = 'col-resize';
    document.body.style.userSelect = 'none';
  }, []);

  useEffect(() => {
    const onMouseMove = (e: MouseEvent) => {
      if (!dragging.current || !containerRef.current) return;
      const rect = containerRef.current.getBoundingClientRect();
      const pct = ((e.clientX - rect.left) / rect.width) * 100;
      setSplitPct(Math.max(minPct, Math.min(100 - minPct, pct)));
    };
    const onMouseUp = () => {
      if (dragging.current) {
        dragging.current = false;
        document.body.style.cursor = '';
        document.body.style.userSelect = '';
      }
    };
    window.addEventListener('mousemove', onMouseMove);
    window.addEventListener('mouseup', onMouseUp);
    return () => {
      window.removeEventListener('mousemove', onMouseMove);
      window.removeEventListener('mouseup', onMouseUp);
    };
  }, [minPct]);

  // Ctrl+\ to toggle split/single
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if ((e.ctrlKey || e.metaKey) && e.key === '\\') {
        e.preventDefault();
        setSingleView(s => !s);
      }
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, []);

  if (singleView) {
    return (
      <div ref={containerRef} className="w-full h-full relative">
        {left}
        {/* Button to restore split */}
        <button
          onClick={() => setSingleView(false)}
          className="absolute top-2 right-2 z-10 px-2 py-1 text-3xs bg-surface-elevated/80
            border border-edge rounded text-fg-secondary hover:text-fg-primary backdrop-blur-sm"
          title="Show split view (Ctrl+\)"
        >
          Split View
        </button>
      </div>
    );
  }

  return (
    <div ref={containerRef} className="flex w-full h-full overflow-hidden">
      {/* Left panel */}
      <div style={{ width: `${splitPct}%` }} className="h-full overflow-hidden relative">
        {left}
      </div>

      {/* Divider */}
      <div
        className="w-1 bg-edge hover:bg-accent/50 cursor-col-resize shrink-0 relative group transition-colors"
        onMouseDown={onMouseDown}
        onDoubleClick={() => setSplitPct(50)}
        role="separator"
        aria-orientation="vertical"
        aria-label="Resize split view"
      >
        {/* Visual handle */}
        <div className="absolute top-1/2 left-1/2 -translate-x-1/2 -translate-y-1/2 w-0.5 h-8 bg-edge-strong group-hover:bg-accent rounded-full transition-colors" />
      </div>

      {/* Right panel */}
      <div style={{ width: `${100 - splitPct}%` }} className="h-full overflow-hidden relative">
        {right}
        {/* Button to collapse to single */}
        <button
          onClick={() => setSingleView(true)}
          className="absolute top-2 right-2 z-10 px-2 py-1 text-3xs bg-surface-elevated/80
            border border-edge rounded text-fg-secondary hover:text-fg-primary backdrop-blur-sm"
          title="Single view (Ctrl+\)"
        >
          Single
        </button>
      </div>
    </div>
  );
};
