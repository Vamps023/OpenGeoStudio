/**
 * Console Panel — application log output.
 *
 * Shows log messages from the main process and renderer.
 * Subscribes to log events via the EventBus.
 * Includes level filter, search, and clear functionality.
 */

import React, { useState, useEffect, useRef, useCallback, useMemo } from 'react';
import { Trash2, ChevronDown, Search, Terminal } from 'lucide-react';
import { getIpcBridge } from '../../../core/ipc';
import { PanelHeader } from '../../../components/common/PanelHeader';
import { EmptyState } from '../../../components/common/EmptyState';

interface LogEntry {
  level: 'info' | 'warn' | 'error' | 'debug';
  message: string;
  timestamp: string;
  source?: string;
}

const levelColor: Record<string, string> = {
  info: 'text-fg-secondary',
  warn: 'text-warn',
  error: 'text-err',
  debug: 'text-fg-muted',
};

const levelPrefix: Record<string, string> = {
  info: '[INFO]',
  warn: '[WARN]',
  error: '[ERR]',
  debug: '[DBG]',
};

const LEVELS = ['info', 'warn', 'error', 'debug'] as const;

export const ConsolePanel: React.FC = () => {
  const [entries, setEntries] = useState<LogEntry[]>([
    { level: 'info', message: 'OpenGeoStudio console ready.', timestamp: new Date().toISOString(), source: 'console' },
  ]);
  const [autoScroll, setAutoScroll] = useState(true);
  const [query, setQuery] = useState('');
  const [enabledLevels, setEnabledLevels] = useState<Set<string>>(new Set(LEVELS));
  const scrollRef = useRef<HTMLDivElement>(null);

  // Listen for console log events from main process
  useEffect(() => {
    const ipc = getIpcBridge();
    if (!ipc) return;
    const handler = (_e: unknown, entry: LogEntry) => {
      setEntries(prev => [...prev.slice(-999), entry]);
    };
    const unsub = ipc.on('console:log', handler);
    return () => { unsub?.(); };
  }, []);

  // Auto-scroll to bottom
  useEffect(() => {
    if (autoScroll && scrollRef.current) {
      scrollRef.current.scrollTop = scrollRef.current.scrollHeight;
    }
  }, [entries, autoScroll]);

  const handleClear = useCallback(() => setEntries([]), []);

  const handleScroll = useCallback(() => {
    if (!scrollRef.current) return;
    const { scrollTop, scrollHeight, clientHeight } = scrollRef.current;
    const atBottom = scrollHeight - scrollTop - clientHeight < 30;
    setAutoScroll(atBottom);
  }, []);

  const toggleLevel = useCallback((level: string) => {
    setEnabledLevels(prev => {
      const next = new Set(prev);
      if (next.has(level)) next.delete(level);
      else next.add(level);
      return next;
    });
  }, []);

  const filtered = useMemo(() => {
    return entries.filter(e => {
      if (!enabledLevels.has(e.level)) return false;
      if (query.trim()) {
        const q = query.toLowerCase();
        return e.message.toLowerCase().includes(q) || (e.source?.toLowerCase().includes(q) ?? false);
      }
      return true;
    });
  }, [entries, query, enabledLevels]);

  const errorCount = entries.filter(e => e.level === 'error').length;

  return (
    <div className="flex flex-col h-full bg-surface-panel">
      <PanelHeader
        icon={Terminal}
        title="Console"
        description="Application log output"
        actions={
          <>
            {/* Level filters */}
            {LEVELS.map(level => (
              <button
                key={level}
                onClick={() => toggleLevel(level)}
                className={`px-1.5 py-0.5 text-3xs font-mono uppercase rounded transition-colors ${
                  enabledLevels.has(level)
                    ? `${levelColor[level]} bg-surface-hover`
                    : 'text-fg-muted line-through'
                }`}
                title={`Toggle ${level} logs`}
              >
                {level}
              </button>
            ))}
            <button
              onClick={() => setAutoScroll(s => !s)}
              className={`icon-btn icon-btn-sm ${autoScroll ? 'icon-btn-active' : ''}`}
              aria-label="Toggle auto-scroll"
              title="Auto-scroll"
            >
              <ChevronDown size={12} />
            </button>
            <button
              onClick={handleClear}
              className="icon-btn icon-btn-sm hover:text-err"
              aria-label="Clear console"
              title="Clear console"
            >
              <Trash2 size={12} />
            </button>
          </>
        }
        search={
          <div className="flex items-center gap-1.5 px-2 py-1 bg-surface-base border border-edge rounded">
            <Search size={11} className="text-fg-muted" />
            <input
              type="text"
              value={query}
              onChange={(e) => setQuery(e.target.value)}
              placeholder="Filter logs…"
              className="flex-1 bg-transparent text-2xs text-fg-primary placeholder-fg-muted outline-none"
              aria-label="Filter logs"
            />
            <span className="text-3xs text-fg-muted tabular-nums">{filtered.length}</span>
            {errorCount > 0 && <span className="text-3xs text-err tabular-nums">{errorCount} err</span>}
          </div>
        }
      />

      {/* Log entries */}
      <div
        ref={scrollRef}
        onScroll={handleScroll}
        className="flex-1 overflow-auto px-2 py-1 font-mono text-2xs leading-relaxed"
      >
        {filtered.length === 0 ? (
          <EmptyState
            icon={Terminal}
            title={entries.length === 0 ? 'Console cleared' : 'No matching logs'}
            description={entries.length === 0 ? 'Log output will appear here.' : 'Try changing the filter or level toggles.'}
          />
        ) : (
          filtered.map((entry, i) => (
            <div key={i} className={`flex gap-2 py-0.5 hover:bg-surface-hover/50 rounded px-1 ${levelColor[entry.level] ?? 'text-fg-secondary'}`}>
              <span className="text-fg-muted shrink-0 tabular-nums">
                {new Date(entry.timestamp).toLocaleTimeString('en-US', { hour12: false })}
              </span>
              <span className="shrink-0 font-bold">{levelPrefix[entry.level] ?? '[LOG]'}</span>
              {entry.source && <span className="text-fg-muted shrink-0">[{entry.source}]</span>}
              <span className="break-all">{entry.message}</span>
            </div>
          ))
        )}
      </div>
    </div>
  );
};

export default ConsolePanel;
