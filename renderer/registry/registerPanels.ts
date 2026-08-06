/**
 * Panel Registrations — registers all renderer-side panel components.
 *
 * This file is the single source of truth for which panel components
 * exist on the renderer side. Each panel is registered with its ID
 * (matching the PanelContribution.id from the main process), a lazy-loaded
 * component, and metadata.
 */

import { registerPanel } from './panelRegistry';

// ═══════════════════════════════════════════════════════════════
// Terrain Module Panels
// ═══════════════════════════════════════════════════════════════

registerPanel(
  'map-viewport',
  'Map',
  'Map',
  'center',
  () => import('../../modules/terrain/client/MapViewport/MapViewport').then(m => ({ default: m.MapViewport })),
  { defaultVisible: true, closeable: false },
);

registerPanel(
  'layer-stack',
  'Layers',
  'Layers',
  'left',
  () => import('../../modules/terrain/client/LayerStack/LayerStack').then(m => ({ default: m.LayerStack })),
  { defaultWidth: 280, defaultVisible: true },
);

// ═══════════════════════════════════════════════════════════════
// Export Module Panels
// ═══════════════════════════════════════════════════════════════

registerPanel(
  'export-panel',
  'Export',
  'Download',
  'right',
  () => import('../../modules/export/client/ExportPanel/ExportPanel').then(m => ({ default: m.ExportPanel })),
  { defaultWidth: 340, defaultVisible: true },
);

registerPanel(
  'job-queue',
  'Jobs',
  'ListTodo',
  'bottom',
  () => import('../panels/JobQueue/JobQueue'),
  { defaultHeight: 200, defaultVisible: false },
);

// ═══════════════════════════════════════════════════════════════
// Road Studio Module Panels
// ═══════════════════════════════════════════════════════════════

registerPanel(
  'road-studio-viewport',
  'Road Studio',
  'Road',
  'center',
  () => import('../../modules/road-studio/client/RoadStudioWorkspace').then(m => ({ default: m.RoadStudioWorkspace })),
  { defaultVisible: true, closeable: false },
);

// ═══════════════════════════════════════════════════════════════
// Framework Panels
// ═══════════════════════════════════════════════════════════════

registerPanel(
  'console',
  'Console',
  'Terminal',
  'bottom',
  () => import('./panels/ConsolePanel/ConsolePanel'),
  { defaultHeight: 180, defaultVisible: true, closeable: false },
);

// ═══════════════════════════════════════════════════════════════
// Project Panels (Home workspace)
// ═══════════════════════════════════════════════════════════════

registerPanel(
  'project-explorer',
  'Project Explorer',
  'FolderTree',
  'left',
  () => import('../panels/ProjectExplorer/ProjectExplorer'),
  { defaultWidth: 280, defaultVisible: true, closeable: false },
);

registerPanel(
  'recent-projects',
  'Recent Projects',
  'Clock',
  'center',
  () => import('../panels/RecentProjects/RecentProjects'),
  { defaultVisible: true, closeable: false },
);
