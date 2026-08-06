/**
 * WorkspaceTabs — horizontal tab bar for 1-click workspace switching.
 *
 * Replaces the old dropdown workspace switcher (which required 2 clicks).
 * Active tab has an accent underline. Hover shows a tooltip with the description.
 * Right-click offers "Reset layout".
 */
import React, { useState, useCallback } from 'react';
import { RotateCw } from 'lucide-react';
import { ContextMenu, type ContextMenuItem } from '../components/common/ContextMenu';

/**
 * A workspace item for the tab bar. The `icon` is an optional React component
 * (resolved by the caller from the workspace's icon name string).
 */
interface WorkspaceTabItem {
  id: string;
  name: string;
  description: string;
  icon?: React.ComponentType<{ size?: number; className?: string }>;
}

interface WorkspaceTabsProps {
  workspaces: WorkspaceTabItem[];
  activeId?: string;
  onSelect: (id: string) => void;
  onResetLayout?: (id: string) => void;
}

export const WorkspaceTabs: React.FC<WorkspaceTabsProps> = ({
  workspaces,
  activeId,
  onSelect,
  onResetLayout,
}) => {
  const [menu, setMenu] = useState<{ x: number; y: number; id: string } | null>(null);

  const onContextMenu = useCallback((e: React.MouseEvent, id: string) => {
    e.preventDefault();
    setMenu({ x: e.clientX, y: e.clientY, id });
  }, []);

  const menuItems: ContextMenuItem[] = onResetLayout
    ? [
        {
          label: 'Reset layout to default',
          icon: RotateCw,
          onClick: () => { if (menu) onResetLayout(menu.id); },
        },
      ]
    : [];

  return (
    <nav
      className="flex items-end h-8 gap-0.5 px-1"
      role="tablist"
      aria-label="Workspaces"
    >
      {workspaces.map((ws) => {
        const active = ws.id === activeId;
        return (
          <button
            key={ws.id}
            role="tab"
            aria-selected={active}
            aria-label={ws.name}
            title={ws.description}
            onClick={() => onSelect(ws.id)}
            onContextMenu={(e) => onContextMenu(e, ws.id)}
            className={`relative flex items-center gap-1.5 px-3 h-7 rounded-t text-2xs font-medium
              transition-colors duration-150 whitespace-nowrap
              ${active
                ? 'text-fg-primary bg-surface-panel border-t border-l border-r border-edge border-b-transparent -mb-px'
                : 'text-fg-secondary hover:text-fg-primary hover:bg-surface-hover border border-transparent'
              }`}
          >
            {ws.icon && <ws.icon size={13} className={active ? 'text-accent' : ''} />}
            <span>{ws.name}</span>
            {active && (
              <span className="absolute bottom-0 left-0 right-0 h-0.5 bg-accent" />
            )}
          </button>
        );
      })}
      {menu && menuItems.length > 0 && (
        <ContextMenu
          x={menu.x}
          y={menu.y}
          items={menuItems}
          onClose={() => setMenu(null)}
        />
      )}
    </nav>
  );
};
