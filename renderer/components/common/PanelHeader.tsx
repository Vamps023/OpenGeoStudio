/**
 * PanelHeader — reusable header bar for all dock panels.
 *
 * Provides: icon, title, description tooltip, action buttons slot,
 * search slot, and a consistent 36px height.
 *
 * Usage:
 *   <PanelHeader icon={Layers} title="Layer Stack" description="Manage map layers">
 *     <button className="icon-btn"><RefreshCw size={14} /></button>
 *   </PanelHeader>
 */
import React from 'react';

interface PanelHeaderProps {
  icon?: React.ComponentType<{ size?: number; className?: string }>;
  title: string;
  description?: string;
  /** Action buttons (refresh, settings, etc.) — rendered right-aligned */
  actions?: React.ReactNode;
  /** Optional search input — rendered below or inline */
  search?: React.ReactNode;
  children?: React.ReactNode;
}

export const PanelHeader: React.FC<PanelHeaderProps> = ({
  icon: Icon,
  title,
  description,
  actions,
  search,
  children,
}) => {
  return (
    <div className="flex flex-col shrink-0 border-b border-edge bg-surface-panel">
      <div className="flex items-center gap-2 h-9 px-3" role="toolbar" aria-label={title}>
        {Icon && <Icon size={14} className="text-accent shrink-0" />}
        <h2
          className="text-2xs font-semibold uppercase tracking-wider text-fg-secondary truncate"
          title={description ?? title}
        >
          {title}
        </h2>
        <div className="ml-auto flex items-center gap-0.5">
          {actions}
          {children}
        </div>
      </div>
      {search && (
        <div className="px-3 pb-2">{search}</div>
      )}
    </div>
  );
};
