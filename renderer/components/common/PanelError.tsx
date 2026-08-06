/**
 * PanelError — reusable error state for panels that failed to load data.
 *
 * Shows an error icon, message, and a retry button.
 */
import React from 'react';
import { AlertCircle, RotateCw } from 'lucide-react';

interface PanelErrorProps {
  message?: string;
  onRetry?: () => void;
  className?: string;
}

export const PanelError: React.FC<PanelErrorProps> = ({
  message = 'Something went wrong',
  onRetry,
  className = '',
}) => (
  <div
    className={`flex flex-col items-center justify-center h-full p-6 text-center select-none ${className}`}
    role="alert"
  >
    <div className="mb-3 flex items-center justify-center w-12 h-12 rounded-lg bg-err/10 border border-err/30">
      <AlertCircle size={22} className="text-err" />
    </div>
    <div className="text-sm font-medium text-err mb-1">Error</div>
    <div className="text-2xs text-fg-muted max-w-[240px] leading-relaxed mb-3">{message}</div>
    {onRetry && (
      <button
        onClick={onRetry}
        className="inline-flex items-center gap-1.5 px-3 py-1.5 text-2xs font-medium
                   rounded bg-surface-elevated border border-edge hover:bg-surface-hover
                   text-fg-primary transition-colors"
      >
        <RotateCw size={12} />
        Retry
      </button>
    )}
  </div>
);
