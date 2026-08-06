/**
 * ResizableDock — a dock panel wrapper with drag-to-resize handle.
 *
 * Supports left (horizontal resize), right (horizontal), and bottom (vertical).
 * Double-click the handle to reset to default size.
 * Min/max constraints prevent collapsing too small or growing too large.
 */
import React, { useCallback, useEffect, useRef } from 'react';

type DockSide = 'left' | 'right' | 'bottom';

interface ResizableDockProps {
  side: DockSide;
  size: number;
  defaultSize: number;
  min: number;
  max: number;
  collapsed: boolean;
  onResize: (size: number) => void;
  children: React.ReactNode;
}

export const ResizableDock: React.FC<ResizableDockProps> = ({
  side,
  size,
  defaultSize,
  min,
  max,
  collapsed,
  onResize,
  children,
}) => {
  const dragging = useRef(false);
  const startPos = useRef(0);
  const startSize = useRef(0);

  const onMouseDown = useCallback(
    (e: React.MouseEvent) => {
      e.preventDefault();
      dragging.current = true;
      startPos.current = side === 'bottom' ? e.clientY : e.clientX;
      startSize.current = size;
      document.body.style.cursor = side === 'bottom' ? 'row-resize' : 'col-resize';
      document.body.style.userSelect = 'none';
    },
    [side, size]
  );

  useEffect(() => {
    const onMouseMove = (e: MouseEvent) => {
      if (!dragging.current) return;
      const pos = side === 'bottom' ? e.clientY : e.clientX;
      const delta = pos - startPos.current;
      // Left dock: dragging right increases size. Right/bottom: dragging left/up increases.
      const raw = side === 'left'
        ? startSize.current + delta
        : startSize.current - delta;
      const clamped = Math.max(min, Math.min(max, raw));
      onResize(clamped);
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
  }, [side, min, max, onResize]);

  const handleDoubleClick = useCallback(() => onResize(defaultSize), [defaultSize, onResize]);

  // The resize handle is a thin bar on the inner edge of the dock
  const handleClass =
    side === 'bottom'
      ? 'absolute top-0 left-0 right-0 h-1 cursor-row-resize hover:bg-accent/40 transition-colors'
      : side === 'left'
        ? 'absolute top-0 right-0 bottom-0 w-1 cursor-col-resize hover:bg-accent/40 transition-colors'
        : 'absolute top-0 left-0 bottom-0 w-1 cursor-col-resize hover:bg-accent/40 transition-colors';

  const dimension =
    collapsed
      ? side === 'bottom' ? { height: '32px' } : { width: '40px' }
      : side === 'bottom' ? { height: `${size}px` } : { width: `${size}px` };

  return (
    <div
      className={`relative flex flex-col bg-surface-panel border-edge shrink-0 overflow-hidden ${
        side === 'bottom' ? 'border-t' : side === 'left' ? 'border-r' : 'border-l'
      }`}
      style={dimension}
    >
      {/* Resize handle (hidden when collapsed) */}
      {!collapsed && (
        <div
          className={handleClass}
          onMouseDown={onMouseDown}
          onDoubleClick={handleDoubleClick}
          role="separator"
          aria-orientation={side === 'bottom' ? 'horizontal' : 'vertical'}
          aria-label={`Resize ${side} dock`}
        />
      )}
      {children}
    </div>
  );
};
