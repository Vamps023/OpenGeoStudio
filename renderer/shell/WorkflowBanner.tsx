/**
 * WorkflowBanner — Data-driven pipeline progress indicator.
 *
 * Reads stage definitions from the workflow registry and checks
 * completion state via ProjectContext IPC. No hardcoded stages.
 *
 * Stages are derived from the workflow definition, allowing plugins
 * to register new stages without modifying this component.
 */
import React, { useState, useEffect } from 'react';
import {
  CheckCircle, Circle, ArrowRight, Mountain, Map, Box, Car, Download,
  FolderOpen, Home,
} from 'lucide-react';
import { useCoreStore } from '../core/coreStore';
import { ProjectContextIPC } from '../core/ipc';
import { workflowRegistry, type WorkflowStageDef } from '../../core/project/workflow-def';
import type { ProjectContextState } from '../../core/project/projectContext';

// Icon map — maps icon name strings to lucide components
const ICONS: Record<string, React.ComponentType<{ size?: number; className?: string }>> = {
  Mountain, Map, Box, Car, Download, FolderOpen, Home,
};

export const WorkflowBanner: React.FC = () => {
  const activeProject = useCoreStore((s) => s.activeProject);
  const activeWorkspace = useCoreStore((s) => s.activeWorkspace);
  const activateWorkspace = useCoreStore((s) => s.activateWorkspace);
  const executeCommand = useCoreStore((s) => s.executeCommand);

  const [projectState, setProjectState] = useState<ProjectContextState | null>(null);

  // Poll ProjectContext state to determine stage completion
  useEffect(() => {
    if (!activeProject) {
      setProjectState(null);
      return;
    }
    let cancelled = false;
    const fetchState = async () => {
      const state = await ProjectContextIPC.getState();
      if (!cancelled) setProjectState(state);
    };
    fetchState();
    // Re-check every 2 seconds — lightweight IPC call
    const interval = setInterval(fetchState, 2000);
    return () => { cancelled = true; clearInterval(interval); };
  }, [activeProject]);

  // Don't show banner on Home workspace or when no project is open
  if (!activeProject || activeWorkspace?.id === 'home') return null;

  const stages = workflowRegistry.getStages();

  // Determine completion state for each stage
  const stageStates = stages.map(stage => ({
    ...stage,
    complete: projectState ? stage.isComplete(projectState) : false,
  }));

  // Find the next incomplete stage (the "current" stage)
  const nextStage = stageStates.find(s => !s.complete);
  const allComplete = !nextStage;

  const handleStageClick = (stage: WorkflowStageDef) => {
    activateWorkspace(stage.workspaceId);
  };

  const handleActionClick = (e: React.MouseEvent, stage: WorkflowStageDef) => {
    e.stopPropagation();
    if (stage.actionCommand) {
      executeCommand(stage.actionCommand);
    } else {
      activateWorkspace(stage.workspaceId);
    }
  };

  return (
    <div data-testid="workflow-banner" className="flex items-center h-7 px-3 border-b border-edge bg-surface-panel shrink-0 gap-1 text-2xs">
      {/* Workflow stages — data-driven from workflow registry */}
      <div className="flex items-center gap-0.5">
        {stageStates.map((stage, idx) => {
          return (
            <React.Fragment key={stage.id}>
              <button
                onClick={() => handleStageClick(stage)}
                className={`flex items-center gap-1 px-1.5 py-0.5 rounded transition-colors ${
                  !stage.complete && stage.id === nextStage?.id
                    ? 'bg-accent/15 text-accent font-medium'
                    : stage.complete
                    ? 'text-ok hover:bg-surface-hover'
                    : 'text-fg-muted hover:bg-surface-hover'
                }`}
                title={stage.complete ? `${stage.label} ✓` : stage.actionLabel}
              >
                {stage.complete ? (
                  <CheckCircle size={11} className="text-ok" />
                ) : !stage.complete && stage.id === nextStage?.id ? (
                  <Circle size={11} className="text-accent fill-accent/30" />
                ) : (
                  <Circle size={11} className="text-fg-muted" />
                )}
                <span>{stage.label}</span>
              </button>
              {idx < stageStates.length - 1 && (
                <ArrowRight size={10} className="text-fg-muted shrink-0" />
              )}
            </React.Fragment>
          );
        })}
      </div>

      {/* Next action button — data-driven from stage definition */}
      {nextStage && (
        <button
          onClick={(e) => handleActionClick(e, nextStage)}
          className="ml-auto flex items-center gap-1.5 px-2 py-0.5 rounded bg-accent text-white font-medium hover:bg-accent-hover transition-colors"
        >
          <NextStageIcon stage={nextStage} />
          {nextStage.actionLabel}
          <ArrowRight size={10} />
        </button>
      )}

      {allComplete && (
        <div className="ml-auto flex items-center gap-1.5 text-ok font-medium">
          <CheckCircle size={12} />
          <span>Workflow Complete — Ready for Export</span>
        </div>
      )}
    </div>
  );
};

// Helper to render the icon for the next stage
const NextStageIcon: React.FC<{ stage: WorkflowStageDef }> = ({ stage }) => {
  const Icon = ICONS[stage.icon] ?? Circle;
  return <Icon size={11} />;
};
