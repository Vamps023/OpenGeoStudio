/**
 * EmptyState — reusable empty state for panels with no data.
 *
 * Shows an icon, title, description, and an optional action button.
 * Used when a panel has nothing to display (no selection, no layers, etc.)
 */
import React from 'react';

interface EmptyStateProps {
  icon?: React.ComponentType<{ size?: number; className?: string }>;
  title: string;
  description?: string;
  action?: React.ReactNode;
  className?: string;
}

export const EmptyState: React.FC<EmptyStateProps> = ({
  icon: Icon,
  title,
  description,
  action,
  className = '',
}) => (
  <div
    className={`flex flex-col items-center justify-center h-full p-6 text-center select-none ${className}`}
    role="status"
    aria-label={title}
  >
    {Icon && (
      <div className="mb-3 flex items-center justify-center w-12 h-12 rounded-lg bg-surface-elevated border border-edge">
        <Icon size={22} className="text-fg-muted" />
      </div>
    )}
    <div className="text-sm font-medium text-fg-secondary mb-1">{title}</div>
    {description && (
      <div className="text-2xs text-fg-muted max-w-[240px] leading-relaxed">{description}</div>
    )}
    {action && <div className="mt-4">{action}</div>}
  </div>
);
