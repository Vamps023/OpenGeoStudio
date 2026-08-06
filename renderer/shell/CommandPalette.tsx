/**
 * Command Palette — Ctrl+Shift+P search interface for all registered commands.
 *
 * Shows a searchable list of all commands from the CommandRegistry.
 * User can filter by name, category, or shortcut.
 */

import React, { useState, useMemo, useEffect, useRef } from 'react';
import { Search, CornerDownLeft } from 'lucide-react';
import { useCoreStore } from '../core/coreStore';
import { useFocusTrap } from '../hooks/useFocusTrap';

interface CommandPaletteProps {
  onClose: () => void;
}

export const CommandPalette: React.FC<CommandPaletteProps> = ({ onClose }) => {
  const [query, setQuery] = useState('');
  const [selectedIndex, setSelectedIndex] = useState(0);
  const { commands, executeCommand } = useCoreStore();
  const inputRef = useRef<HTMLInputElement>(null);
  const listRef = useRef<HTMLDivElement>(null);
  const containerRef = useFocusTrap<HTMLDivElement>(true);

  useEffect(() => {
    inputRef.current?.focus();
  }, []);

  // Filter commands by query
  const filtered = useMemo(() => {
    const cmds = commands ?? [];
    if (!query.trim()) return cmds;
    const q = query.toLowerCase();
    return cmds.filter(c =>
      c.id.toLowerCase().includes(q) ||
      c.label?.toLowerCase().includes(q) ||
      c.category?.toLowerCase().includes(q) ||
      c.shortcut?.toLowerCase().includes(q)
    );
  }, [commands, query]);

  // Group by category
  const grouped = useMemo(() => {
    const groups = new Map<string, typeof filtered>();
    for (const cmd of filtered) {
      const cat = cmd.category ?? 'General';
      if (!groups.has(cat)) groups.set(cat, []);
      groups.get(cat)!.push(cmd);
    }
    return Array.from(groups.entries());
  }, [filtered]);

  const flatList = useMemo(() => grouped.flatMap(([, cmds]) => cmds), [grouped]);

  useEffect(() => {
    setSelectedIndex(0);
  }, [query]);

  const execute = (index: number) => {
    const cmd = flatList[index];
    if (cmd) {
      executeCommand(cmd.id);
      onClose();
    }
  };

  const handleKeyDown = (e: React.KeyboardEvent) => {
    if (e.key === 'ArrowDown') {
      e.preventDefault();
      setSelectedIndex(i => Math.min(i + 1, flatList.length - 1));
    } else if (e.key === 'ArrowUp') {
      e.preventDefault();
      setSelectedIndex(i => Math.max(i - 1, 0));
    } else if (e.key === 'Enter') {
      e.preventDefault();
      execute(selectedIndex);
    } else if (e.key === 'Escape') {
      e.preventDefault();
      onClose();
    }
  };

  // Scroll selected item into view
  useEffect(() => {
    const el = listRef.current?.querySelector(`[data-idx="${selectedIndex}"]`);
    el?.scrollIntoView({ block: 'nearest' });
  }, [selectedIndex]);

  let runningIndex = 0;

  return (
    <div
      className="fixed inset-0 z-[100] flex items-start justify-center pt-[15vh] bg-black/50 backdrop-blur-sm"
      onClick={onClose}
      role="dialog"
      aria-modal="true"
      aria-label="Command palette"
    >
      <div
        ref={containerRef}
        className="w-[600px] max-h-[60vh] bg-surface-elevated border border-edge rounded-lg shadow-overlay overflow-hidden flex flex-col"
        onClick={(e) => e.stopPropagation()}
      >
        {/* Search input */}
        <div className="flex items-center gap-3 px-4 py-3 border-b border-edge">
          <Search className="w-4 h-4 text-fg-muted" />
          <input
            ref={inputRef}
            type="text"
            value={query}
            onChange={(e) => setQuery(e.target.value)}
            onKeyDown={handleKeyDown}
            placeholder="Search commands…"
            className="flex-1 bg-transparent text-sm text-fg-primary placeholder-fg-muted outline-none"
            aria-label="Search commands"
          />
          <kbd className="text-3xs text-fg-muted bg-surface-hover px-1.5 py-0.5 rounded font-mono">ESC</kbd>
        </div>

        {/* Command list */}
        <div ref={listRef} className="flex-1 overflow-auto py-1">
          {flatList.length === 0 && (
            <div className="px-4 py-8 text-center text-sm text-fg-muted">No commands found</div>
          )}
          {grouped.map(([category, cmds]) => (
            <div key={category}>
              <div className="px-3 py-1 text-3xs font-semibold text-fg-muted uppercase tracking-wider bg-surface-panel sticky top-0">
                {category}
              </div>
              {cmds.map((cmd) => {
                const idx = runningIndex++;
                const isSelected = idx === selectedIndex;
                return (
                  <button
                    key={cmd.id}
                    data-idx={idx}
                    onClick={() => execute(idx)}
                    onMouseEnter={() => setSelectedIndex(idx)}
                    className={`w-full flex items-center gap-3 px-4 py-2 text-left text-sm transition-colors ${
                      isSelected ? 'bg-accent/10 text-accent' : 'text-fg-secondary hover:bg-surface-hover'
                    }`}
                  >
                    <span className="flex-1">{cmd.label ?? cmd.id}</span>
                    {cmd.shortcut && (
                      <kbd className="text-3xs text-fg-muted bg-surface-hover px-1.5 py-0.5 rounded font-mono">{cmd.shortcut}</kbd>
                    )}
                    {isSelected && <CornerDownLeft className="w-3 h-3 text-accent" />}
                  </button>
                );
              })}
            </div>
          ))}
        </div>

        {/* Footer */}
        <div className="px-4 py-2 border-t border-edge flex items-center justify-between text-3xs text-fg-muted">
          <span>{flatList.length} commands</span>
          <div className="flex items-center gap-3">
            <span><kbd className="bg-surface-hover px-1 rounded font-mono">↑↓</kbd> navigate</span>
            <span><kbd className="bg-surface-hover px-1 rounded font-mono">↵</kbd> execute</span>
          </div>
        </div>
      </div>
    </div>
  );
};
