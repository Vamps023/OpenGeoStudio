/**
 * ContextMenu — reusable right-click context menu.
 *
 * Renders a fixed-position menu at the given coordinates.
 * Closes on click outside, Escape, or scroll.
 *
 * Usage:
 *   const [menu, setMenu] = useState<{x:number;y:number}|null>(null);
 *   <div onContextMenu={(e) => { e.preventDefault(); setMenu({x:e.clientX,y:e.clientY}); }}>
 *   {menu && <ContextMenu x={menu.x} y={menu.y} items={[...]} onClose={() => setMenu(null)} />}
 */
import React, { useEffect, useLayoutEffect, useRef, useState } from 'react';

export interface ContextMenuItem {
  id?: string;
  label?: string;
  icon?: React.ComponentType<{ size?: number; className?: string }>;
  shortcut?: string;
  onClick?: () => void;
  divider?: boolean;
  disabled?: boolean;
  danger?: boolean;
}

interface ContextMenuProps {
  x: number;
  y: number;
  items: ContextMenuItem[];
  onClose: () => void;
}

export const ContextMenu: React.FC<ContextMenuProps> = ({ x, y, items, onClose }) => {
  const ref = useRef<HTMLDivElement>(null);
  const [pos, setPos] = useState({ x, y });

  // Clamp to viewport so the menu never overflows
  useLayoutEffect(() => {
    const el = ref.current;
    if (!el) return;
    const rect = el.getBoundingClientRect();
    let nx = x;
    let ny = y;
    if (x + rect.width > window.innerWidth) nx = window.innerWidth - rect.width - 8;
    if (y + rect.height > window.innerHeight) ny = window.innerHeight - rect.height - 8;
    setPos({ x: Math.max(8, nx), y: Math.max(8, ny) });
  }, [x, y]);

  // Close on outside click, Escape, scroll, or blur
  useEffect(() => {
    const onDown = (e: MouseEvent) => {
      if (ref.current && !ref.current.contains(e.target as Node)) onClose();
    };
    const onKey = (e: KeyboardEvent) => { if (e.key === 'Escape') onClose(); };
    const onScroll = () => onClose();
    window.addEventListener('mousedown', onDown);
    window.addEventListener('keydown', onKey);
    window.addEventListener('scroll', onScroll, true);
    return () => {
      window.removeEventListener('mousedown', onDown);
      window.removeEventListener('keydown', onKey);
      window.removeEventListener('scroll', onScroll, true);
    };
  }, [onClose]);

  return (
    <div
      ref={ref}
      role="menu"
      className="fixed z-[500] min-w-[200px] py-1 rounded-md border border-edge
                 bg-surface-elevated shadow-overlay animate-[fadeIn_0.1s_ease-out]"
      style={{ left: pos.x, top: pos.y }}
    >
      {items.map((item, i) =>
        item.divider ? (
          <div key={i} className="h-px bg-edge my-1 mx-2" role="separator" />
        ) : (
          <button
            key={item.id ?? i}
            role="menuitem"
            disabled={item.disabled}
            onClick={() => {
              item.onClick?.();
              onClose();
            }}
            className={`w-full flex items-center justify-between gap-3 px-3 py-1.5 text-2xs
              transition-colors text-left
              ${item.disabled
                ? 'text-fg-muted cursor-default'
                : item.danger
                  ? 'text-err hover:bg-err/10'
                  : 'text-fg-primary hover:bg-surface-hover'
              }`}
          >
            <span className="flex items-center gap-2 truncate">
              {item.icon && <item.icon size={13} className="shrink-0" />}
              {item.label}
            </span>
            {item.shortcut && (
              <kbd className="text-fg-muted font-mono text-3xs">{item.shortcut}</kbd>
            )}
          </button>
        )
      )}
    </div>
  );
};
