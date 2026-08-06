/**
 * IconRail — vertical 40px icon strip for panel switching.
 *
 * Shows one icon per panel in a dock. Clicking an icon selects that panel.
 * The active icon has a left accent border. Inactive icons are dimmed.
 *
 * This replaces the bottom text-tab bar from the old DockShell.
 */
import React from 'react';

interface IconRailItem {
  id: string;
  title: string;
  icon?: React.ComponentType<{ size?: number; className?: string }>;
}

interface IconRailProps {
  items: IconRailItem[];
  activeIndex: number;
  onSelect: (index: number) => void;
  /** Vertical or horizontal rail (left/right = vertical, bottom = horizontal) */
  orientation?: 'vertical' | 'horizontal';
}

export const IconRail: React.FC<IconRailProps> = ({
  items,
  activeIndex,
  onSelect,
  orientation = 'vertical',
}) => {
  if (items.length === 0) return null;

  const isVertical = orientation === 'vertical';

  return (
    <div
      className={`flex shrink-0 bg-surface-base border-edge ${
        isVertical ? 'flex-col w-10 border-r' : 'flex-row h-9 border-t items-center px-1 gap-0.5'
      }`}
      role="tablist"
      aria-orientation={isVertical ? 'vertical' : 'horizontal'}
    >
      {items.map((item, i) => {
        const Icon = item.icon;
        const active = i === activeIndex;
        return (
          <button
            key={item.id}
            role="tab"
            aria-selected={active}
            aria-label={item.title}
            title={item.title}
            onClick={() => onSelect(i)}
            className={`group relative flex items-center justify-center transition-colors ${
              isVertical ? 'w-10 h-10' : 'w-8 h-7 rounded'
            } ${
              active
                ? 'text-accent bg-accent/10'
                : 'text-fg-muted hover:text-fg-secondary hover:bg-surface-hover'
            }`}
          >
            {/* Active indicator bar */}
            {active && isVertical && (
              <span className="absolute left-0 top-1.5 bottom-1.5 w-0.5 rounded-full bg-accent" />
            )}
            {active && !isVertical && (
              <span className="absolute bottom-0.5 left-1.5 right-1.5 h-0.5 rounded-full bg-accent" />
            )}
            {Icon ? <Icon size={16} /> : <span className="text-2xs font-bold">{item.title.charAt(0)}</span>}
          </button>
        );
      })}
    </div>
  );
};
