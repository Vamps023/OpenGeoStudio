/**
 * Workflow Definition — Data-driven pipeline stages.
 *
 * The workflow banner reads from this definition instead of hardcoding stages.
 * This allows plugins to register new stages without modifying the banner.
 *
 * Stages are ordered sequentially. Each stage has:
 * - id: unique identifier
 * - label: display name
 * - icon: lucide icon name
 * - workspaceId: workspace to activate when clicked
 * - isComplete: function that checks ProjectContext state
 * - actionCommand: command to execute when the action button is clicked
 * - actionLabel: label for the action button
 */

import type { ProjectContextState } from './projectContext';

export interface WorkflowStageDef {
  id: string;
  label: string;
  icon: string;
  workspaceId: string;
  actionCommand?: string;
  actionLabel: string;
  isComplete: (state: ProjectContextState) => boolean;
}

// ─── Default Workflow Stages ──────────────────────────────────────
// Project → Terrain → Export
// Each stage depends on the previous one being complete.

export const DEFAULT_WORKFLOW_STAGES: WorkflowStageDef[] = [
  {
    id: 'project',
    label: 'Project',
    icon: 'FolderOpen',
    workspaceId: 'home',
    actionLabel: 'New Project',
    isComplete: (s) => s.projectId !== null,
  },
  {
    id: 'terrain',
    label: 'Terrain',
    icon: 'Mountain',
    workspaceId: 'terrain',
    actionCommand: 'export.run',
    actionLabel: 'Generate Terrain',
    isComplete: (s) => s.terrain !== null,
  },
  {
    id: 'export',
    label: 'Export',
    icon: 'Download',
    workspaceId: 'terrain',
    actionCommand: 'export.run',
    actionLabel: 'Export',
    isComplete: () => false, // Export is a manual action
  },
];

// ─── Plugin Extension Point ───────────────────────────────────────
// Plugins can register additional stages via the workflow registry.

class WorkflowRegistry {
  private stages: WorkflowStageDef[] = [...DEFAULT_WORKFLOW_STAGES];

  register(stage: WorkflowStageDef): void {
    // Insert before the last stage (Export is always last)
    this.stages.splice(this.stages.length - 1, 0, stage);
  }

  getStages(): WorkflowStageDef[] {
    return [...this.stages];
  }

  clear(): void {
    this.stages = [...DEFAULT_WORKFLOW_STAGES];
  }
}

export const workflowRegistry = new WorkflowRegistry();
