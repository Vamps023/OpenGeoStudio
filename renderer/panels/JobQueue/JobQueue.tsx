/**
 * Job Queue Panel — shows active and completed jobs.
 *
 * Displays job status, progress, and allows cancellation.
 * Polls every 2 seconds for updates.
 */

import React, { useEffect, useMemo } from 'react';
import { useCoreStore } from '../../core/coreStore';
import { X, CheckCircle, AlertCircle, Loader, Clock, ListTodo } from 'lucide-react';
import type { Job } from '../../../core/jobs/job-system';
import { PanelHeader } from '../../components/common/PanelHeader';
import { EmptyState } from '../../components/common/EmptyState';

const statusIcon: Record<string, React.ReactNode> = {
  pending:   <Clock size={12} className="text-fg-muted" />,
  running:   <Loader size={12} className="text-accent animate-spin" />,
  completed: <CheckCircle size={12} className="text-ok" />,
  failed:    <AlertCircle size={12} className="text-err" />,
  cancelled: <X size={12} className="text-fg-muted" />,
};

const statusColor: Record<string, string> = {
  pending:   'text-fg-muted',
  running:   'text-accent',
  completed: 'text-ok',
  failed:    'text-err',
  cancelled: 'text-fg-muted',
};

export const JobQueue: React.FC = () => {
  const { jobs, loadJobs, cancelJob } = useCoreStore();

  useEffect(() => {
    loadJobs();
    const interval = setInterval(() => loadJobs(), 5000);
    return () => clearInterval(interval);
  }, [loadJobs]);

  const sorted = useMemo(() => {
    return [...jobs].sort((a, b) => {
      const order: Record<string, number> = { running: 0, pending: 1, completed: 2, failed: 3, cancelled: 4 };
      return (order[a.status] ?? 5) - (order[b.status] ?? 5);
    });
  }, [jobs]);

  const activeCount = jobs.filter(j => j.status === 'running' || j.status === 'pending').length;

  return (
    <div className="flex flex-col h-full bg-surface-panel">
      <PanelHeader
        icon={ListTodo}
        title="Jobs"
        description="Active and completed export/import jobs"
        actions={
          <span className="text-3xs text-fg-muted tabular-nums">
            {jobs.length} total{activeCount > 0 && <span className="text-accent ml-1">{activeCount} active</span>}
          </span>
        }
      />
      <div className="flex-1 overflow-auto">
        {sorted.length === 0 ? (
          <EmptyState
            icon={ListTodo}
            title="No jobs"
            description="Export and data processing jobs will appear here with live progress."
          />
        ) : (
          sorted.map(job => (
            <JobRow key={job.id} job={job} onCancel={() => cancelJob(job.id)} />
          ))
        )}
      </div>
    </div>
  );
};

const JobRow: React.FC<{ job: Job; onCancel: () => void }> = ({ job, onCancel }) => (
  <div className="px-3 py-2 border-b border-edge hover:bg-surface-hover transition-colors">
    <div className="flex items-center gap-2">
      {statusIcon[job.status]}
      <span className="text-2xs text-fg-primary flex-1 truncate">{job.title}</span>
      <span className={`text-3xs font-medium uppercase ${statusColor[job.status]}`}>{job.status}</span>
      {(job.status === 'running' || job.status === 'pending') && (
        <button
          onClick={onCancel}
          className="icon-btn icon-btn-sm hover:text-err"
          aria-label="Cancel job"
          title="Cancel job"
        >
          <X size={11} />
        </button>
      )}
    </div>
    {job.status === 'running' && (
      <div className="mt-1.5">
        <div className="h-1 bg-surface-base rounded-full overflow-hidden border border-edge">
          <div
            className="h-full bg-accent rounded-full transition-all duration-300"
            style={{ width: `${job.progress.percentage * 100}%` }}
          />
        </div>
        <div className="flex items-center justify-between mt-1 text-3xs text-fg-muted">
          <span className="truncate">{job.progress.stage}</span>
          <span className="tabular-nums shrink-0">{Math.round(job.progress.percentage * 100)}%</span>
        </div>
        {job.progress.message && (
          <div className="text-3xs text-fg-muted mt-0.5 truncate">{job.progress.message}</div>
        )}
      </div>
    )}
    {job.result?.error && (
      <div className="mt-1 text-3xs text-err truncate">{job.result.error}</div>
    )}
  </div>
);

export default JobQueue;
