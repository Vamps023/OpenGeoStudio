/**
 * DockShell — Professional desktop application layout.
 *
 * Structure:
 *   ┌──────────────────────────────────────────────────────┐
 *   │ TOP BAR: logo | workspace tabs | menu | save/open    │  32px
 *   ├──────────────────────────────────────────────────────┤
 *   │ CONTEXTUAL TOOLBAR (workspace-specific tools)         │  36px
 *   ├──┬──────────────────────────────────────────┬────────┤
 *   │L│                                            │        │
 *   │I│            CENTER VIEWPORT                 │ RIGHT  │
 *   │R│         (map / 3D / split)                 │ DOCK   │
 *   │ │                                            │        │
 *   ├──┤                                            │        │
 *   │B│                                            │        │
 *   │D│                                            │        │
 *   ├──┴──────────────────────────────────────────┴────────┤
 *   │ STATUS BAR: coords | zoom | CRS | jobs | FPS         │  24px
 *   └──────────────────────────────────────────────────────┘
 *
 * L = Left icon rail (40px) + left dock panel (resizable)
 * R = Right dock panel (resizable)
 * B = Bottom dock (resizable, collapsed by default)
 */
import React, { useState, useCallback, useEffect } from 'react';
import {
  PanelLeftClose, PanelLeftOpen, PanelRightClose, PanelRightOpen,
  PanelBottomClose, PanelBottomOpen,
} from 'lucide-react';
import { IconRail } from './IconRail';
import { ResizableDock } from './ResizableDock';
import { StatusBar } from './StatusBar';

export interface DockPanel {
  id: string;
  title: string;
  component: React.ComponentType;
  icon?: React.ComponentType<{ size?: number; className?: string }>;
}

export interface DockShellProps {
  /** Center content (map, viewer3d, split, etc.) */
  center: React.ReactNode;
  /** Left dock panels */
  leftDock: DockPanel[];
  /** Right dock panels */
  rightDock: DockPanel[];
  /** Bottom dock panels */
  bottomDock: DockPanel[];
  /** Top bar content (logo, workspace tabs, menu, save/open) */
  topBar?: React.ReactNode;
  /** Contextual toolbar content (workspace-specific tools) */
  toolbar?: React.ReactNode;
  /** Workflow banner (pipeline progress + next action) */
  workflowBanner?: React.ReactNode;
  /** Initial / saved layout sizes */
  layoutSizes?: {
    leftWidth: number;
    rightWidth: number;
    bottomHeight: number;
  };
  /** Collapsed states */
  collapsedStates?: {
    left: boolean;
    right: boolean;
    bottom: boolean;
  };
  /** Callbacks for layout changes */
  onLayoutChange?: (sizes: { leftWidth: number; rightWidth: number; bottomHeight: number }) => void;
  onCollapsedChange?: (collapsed: { left: boolean; right: boolean; bottom: boolean }) => void;
  /** Status bar data */
  statusCoords?: { lat: number; lon: number } | null;
  statusZoom?: number;
  statusFps?: number;
}

const DEFAULT_SIZES = { leftWidth: 240, rightWidth: 300, bottomHeight: 180 };
const MIN_SIZES = { leftWidth: 180, rightWidth: 220, bottomHeight: 60 };
const MAX_SIZES = { leftWidth: 420, rightWidth: 500, bottomHeight: 400 };

export const DockShell: React.FC<DockShellProps> = ({
  center,
  leftDock,
  rightDock,
  bottomDock,
  topBar,
  toolbar,
  workflowBanner,
  layoutSizes,
  collapsedStates,
  onLayoutChange,
  onCollapsedChange,
  statusCoords,
  statusZoom,
  statusFps,
}) => {
  const sizes = { ...DEFAULT_SIZES, ...layoutSizes };
  const [leftCollapsed, setLeftCollapsed] = useState(collapsedStates?.left ?? false);
  const [rightCollapsed, setRightCollapsed] = useState(collapsedStates?.right ?? false);
  const [bottomCollapsed, setBottomCollapsed] = useState(collapsedStates?.bottom ?? true);
  const [activeLeft, setActiveLeft] = useState(0);
  const [activeRight, setActiveRight] = useState(0);
  const [activeBottom, setActiveBottom] = useState(0);

  const [leftWidth, setLeftWidth] = useState(sizes.leftWidth);
  const [rightWidth, setRightWidth] = useState(sizes.rightWidth);
  const [bottomHeight, setBottomHeight] = useState(sizes.bottomHeight);

  // Sync from props when workspace changes
  useEffect(() => {
    setLeftWidth(sizes.leftWidth);
    setRightWidth(sizes.rightWidth);
    setBottomHeight(sizes.bottomHeight);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [layoutSizes?.leftWidth, layoutSizes?.rightWidth, layoutSizes?.bottomHeight]);

  useEffect(() => {
    if (collapsedStates) {
      setLeftCollapsed(collapsedStates.left);
      setRightCollapsed(collapsedStates.right);
      setBottomCollapsed(collapsedStates.bottom);
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps -- use individual properties to avoid object identity churn
  }, [collapsedStates?.left, collapsedStates?.right, collapsedStates?.bottom]);

  // Notify parent of size changes
  const handleLeftResize = useCallback((w: number) => {
    setLeftWidth(w);
    onLayoutChange?.({ leftWidth: w, rightWidth, bottomHeight });
  }, [rightWidth, bottomHeight, onLayoutChange]);

  const handleRightResize = useCallback((w: number) => {
    setRightWidth(w);
    onLayoutChange?.({ leftWidth, rightWidth: w, bottomHeight });
  }, [leftWidth, bottomHeight, onLayoutChange]);

  const handleBottomResize = useCallback((h: number) => {
    setBottomHeight(h);
    onLayoutChange?.({ leftWidth, rightWidth, bottomHeight: h });
  }, [leftWidth, rightWidth, onLayoutChange]);

  const toggleLeft = useCallback(() => {
    const c = !leftCollapsed;
    setLeftCollapsed(c);
    onCollapsedChange?.({ left: c, right: rightCollapsed, bottom: bottomCollapsed });
  }, [leftCollapsed, rightCollapsed, bottomCollapsed, onCollapsedChange]);

  const toggleRight = useCallback(() => {
    const c = !rightCollapsed;
    setRightCollapsed(c);
    onCollapsedChange?.({ left: leftCollapsed, right: c, bottom: bottomCollapsed });
  }, [leftCollapsed, rightCollapsed, bottomCollapsed, onCollapsedChange]);

  const toggleBottom = useCallback(() => {
    const c = !bottomCollapsed;
    setBottomCollapsed(c);
    onCollapsedChange?.({ left: leftCollapsed, right: rightCollapsed, bottom: c });
  }, [leftCollapsed, rightCollapsed, bottomCollapsed, onCollapsedChange]);

  const leftActivePanel = leftDock[activeLeft];
  const rightActivePanel = rightDock[activeRight];
  const bottomActivePanel = bottomDock[activeBottom];

  return (
    <div className="flex flex-col h-screen w-screen overflow-hidden bg-surface-base text-fg-primary">
      {/* ─── Top Bar (32px) ─────────────────────────────────── */}
      {topBar && (
        <div className="flex items-center h-8 px-2 border-b border-edge bg-surface-panel shrink-0 gap-1">
          {topBar}
        </div>
      )}

      {/* ─── Contextual Toolbar (36px) ──────────────────────── */}
      {toolbar && (
        <div className="flex items-center h-9 px-2 border-b border-edge bg-surface-elevated shrink-0 gap-1">
          {toolbar}
        </div>
      )}

      {/* ─── Workflow Banner (28px) ─────────────────────────── */}
      {workflowBanner}

      {/* ─── Main Area ──────────────────────────────────────── */}
      <div className="flex flex-1 overflow-hidden">
        {/* Left: Icon Rail + Dock Panel */}
        {leftDock.length > 0 && (
          <>
            <IconRail
              items={leftDock}
              activeIndex={activeLeft}
              onSelect={setActiveLeft}
              orientation="vertical"
            />
            <ResizableDock
              side="left"
              size={leftWidth}
              defaultSize={DEFAULT_SIZES.leftWidth}
              min={MIN_SIZES.leftWidth}
              max={MAX_SIZES.leftWidth}
              collapsed={leftCollapsed}
              onResize={handleLeftResize}
            >
              {/* Dock header: title + collapse button */}
              <div className="flex items-center h-8 px-2 border-b border-edge shrink-0">
                {!leftCollapsed && (
                  <span className="text-2xs font-semibold uppercase tracking-wider text-fg-secondary truncate">
                    {leftActivePanel?.title ?? ''}
                  </span>
                )}
                <button
                  onClick={toggleLeft}
                  className="icon-btn icon-btn-sm ml-auto"
                  aria-label={leftCollapsed ? 'Expand left panel' : 'Collapse left panel'}
                >
                  {leftCollapsed ? <PanelLeftOpen size={14} /> : <PanelLeftClose size={14} />}
                </button>
              </div>
              {!leftCollapsed && leftActivePanel && (
                <div className="flex-1 overflow-auto bg-surface-panel">
                  {React.createElement(leftActivePanel.component)}
                </div>
              )}
            </ResizableDock>
          </>
        )}

        {/* Center + Bottom */}
        <div className="flex flex-col flex-1 overflow-hidden">
          <div id="main-content" className="flex-1 overflow-hidden relative bg-surface-base" role="main">
            {center}
          </div>

          {/* Bottom Dock */}
          {bottomDock.length > 0 && (
            <ResizableDock
              side="bottom"
              size={bottomHeight}
              defaultSize={DEFAULT_SIZES.bottomHeight}
              min={MIN_SIZES.bottomHeight}
              max={MAX_SIZES.bottomHeight}
              collapsed={bottomCollapsed}
              onResize={handleBottomResize}
            >
              {/* Bottom dock header: tabs + collapse */}
              <div className="flex items-center h-8 px-1 border-b border-edge shrink-0">
                {!bottomCollapsed && (
                  <div className="flex items-end gap-0.5 h-full">
                    {bottomDock.map((p, i) => (
                      <button
                        key={p.id}
                        onClick={() => setActiveBottom(i)}
                        className={`relative flex items-center gap-1.5 px-2.5 h-7 text-2xs font-medium
                          transition-colors rounded-t whitespace-nowrap
                          ${i === activeBottom
                            ? 'text-fg-primary bg-surface-panel border-t border-l border-r border-edge border-b-transparent -mb-px'
                            : 'text-fg-secondary hover:text-fg-primary hover:bg-surface-hover'
                          }`}
                      >
                        {p.icon && <p.icon size={12} className={i === activeBottom ? 'text-accent' : ''} />}
                        {p.title}
                        {i === activeBottom && <span className="absolute bottom-0 left-0 right-0 h-0.5 bg-accent" />}
                      </button>
                    ))}
                  </div>
                )}
                <button
                  onClick={toggleBottom}
                  className="icon-btn icon-btn-sm ml-auto"
                  aria-label={bottomCollapsed ? 'Expand bottom panel' : 'Collapse bottom panel'}
                >
                  {bottomCollapsed ? <PanelBottomOpen size={14} /> : <PanelBottomClose size={14} />}
                </button>
              </div>
              {!bottomCollapsed && bottomActivePanel && (
                <div className="flex-1 overflow-auto bg-surface-panel">
                  {React.createElement(bottomActivePanel.component)}
                </div>
              )}
            </ResizableDock>
          )}
        </div>

        {/* Right Dock */}
        {rightDock.length > 0 && (
          <ResizableDock
            side="right"
            size={rightWidth}
            defaultSize={DEFAULT_SIZES.rightWidth}
            min={MIN_SIZES.rightWidth}
            max={MAX_SIZES.rightWidth}
            collapsed={rightCollapsed}
            onResize={handleRightResize}
          >
            {/* Right dock header: collapse + title */}
            <div className="flex items-center h-8 px-2 border-b border-edge shrink-0">
              <button
                onClick={toggleRight}
                className="icon-btn icon-btn-sm mr-auto"
                aria-label={rightCollapsed ? 'Expand right panel' : 'Collapse right panel'}
              >
                {rightCollapsed ? <PanelRightOpen size={14} /> : <PanelRightClose size={14} />}
              </button>
              {!rightCollapsed && (
                <span className="text-2xs font-semibold uppercase tracking-wider text-fg-secondary truncate">
                  {rightActivePanel?.title ?? ''}
                </span>
              )}
            </div>
            {!rightCollapsed && rightActivePanel && (
              <div className="flex-1 overflow-auto bg-surface-panel">
                {React.createElement(rightActivePanel.component)}
              </div>
            )}
            {/* Right icon rail (at the bottom of right dock when collapsed) */}
            {!rightCollapsed && rightDock.length > 1 && (
              <div className="flex border-t border-edge shrink-0 h-8">
                {rightDock.map((p, i) => (
                  <button
                    key={p.id}
                    onClick={() => setActiveRight(i)}
                    className={`flex-1 flex items-center justify-center transition-colors
                      ${i === activeRight ? 'text-accent bg-accent/10' : 'text-fg-muted hover:text-fg-secondary hover:bg-surface-hover'}`}
                    title={p.title}
                  >
                    {p.icon ? <p.icon size={14} /> : <span className="text-2xs">{p.title.charAt(0)}</span>}
                  </button>
                ))}
              </div>
            )}
          </ResizableDock>
        )}
      </div>

      {/* ─── Status Bar (24px) ─────────────────────────────── */}
      <StatusBar coords={statusCoords} zoom={statusZoom} fps={statusFps} />
    </div>
  );
};
