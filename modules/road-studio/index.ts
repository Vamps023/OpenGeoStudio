/**
 * Road Studio Module — public exports.
 *
 * Client components (RoadStudioWorkspace, RoadToolbar, RoadViewport) are
 * imported directly by the renderer via lazy imports in registerPanels.ts.
 * This file only exports server/shared code safe for the main process.
 */
export * from './shared/types';
