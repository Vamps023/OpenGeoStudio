/**
 * Toolbar — contextual toolbar that displays workspace-specific tools.
 *
 * Each tool is an icon button with a tooltip (label + shortcut).
 * Tools are sorted by their `order` field. Dividers are inserted
 * between tools with different `group` values.
 */

import React, { useMemo } from 'react';
import { useCoreStore } from '../core/coreStore';
import type { ToolbarContribution } from '../../core/module/contributions';

// ─── Icon mapping ─────────────────────────────────────────────
import {
  MousePointer, Square, ZoomIn, ZoomOut, Download, Mountain, Road, Train,
  FileCode, CheckCircle, RotateCw, Home, FileInput, FileOutput, Network,
  Maximize, Grid3x3, Grid2x2, Move, Play, Square as StopIcon, Plus, X,
  Search, Layers, Box, FolderTree, Clock, Car, Package, Workflow,
  Terminal, ListTree, Settings, ListTodo, Eye, Globe, Filter,
} from 'lucide-react';

const ICON_MAP: Record<string, React.ComponentType<{ className?: string; size?: number }>> = {
  MousePointer, Square, ZoomIn, ZoomOut, Download, Mountain, Road, Train,
  FileCode, CheckCircle, RotateCw, Home, FileInput, FileOutput, Network,
  Maximize, Grid3x3, Grid2x2, Move, Play, Stop: StopIcon, Plus, X,
  Search, Layers, Box, FolderTree, Clock, Car, Package, Workflow,
  Terminal, ListTree, Settings, ListTodo, Eye, Globe, Filter,
};

function getIcon(name?: string): React.ComponentType<{ className?: string; size?: number }> | undefined {
  if (!name) return undefined;
  return ICON_MAP[name] ?? ICON_MAP[name.charAt(0).toUpperCase() + name.slice(1)];
}

interface ToolbarProps {
  tools: ToolbarContribution[];
}

export const Toolbar: React.FC<ToolbarProps> = ({ tools }) => {
  const { executeCommand } = useCoreStore();

  // Sort by order
  const sortedTools = useMemo(() => {
    if (!tools || tools.length === 0) return [];
    return [...tools].sort((a, b) => (a.order ?? 100) - (b.order ?? 100));
  }, [tools]);

  if (sortedTools.length === 0) return null;

  return (
    <div className="flex items-center gap-0.5" role="toolbar" aria-label="Workspace tools">
      {sortedTools.map((tool) => (
          <button
            key={tool.commandId}
            onClick={() => executeCommand(tool.commandId)}
            title={tool.tooltip ?? tool.label}
            aria-label={tool.label}
            className="flex items-center gap-1.5 px-2 h-7 text-2xs font-medium
              text-fg-secondary hover:text-fg-primary hover:bg-surface-hover
              rounded transition-colors duration-150"
          >
            {(() => {
              const Icon = getIcon(tool.icon);
              return Icon ? <Icon size={14} className="shrink-0" /> : null;
            })()}
            <span className="hidden lg:inline">{tool.label}</span>
          </button>
        ))}
    </div>
  );
};
