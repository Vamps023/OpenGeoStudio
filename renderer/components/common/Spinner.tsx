/**
 * Spinner — small loading indicator used in panels and buttons.
 */
import React from 'react';
import { Loader2 } from 'lucide-react';

interface SpinnerProps {
  size?: number;
  className?: string;
  label?: string;
}

export const Spinner: React.FC<SpinnerProps> = ({ size = 16, className = '', label }) => (
  <span className={`inline-flex items-center gap-2 text-fg-secondary ${className}`} role="status" aria-label={label ?? 'Loading'}>
    <Loader2 size={size} className="animate-spin text-accent" />
    {label && <span className="text-2xs">{label}</span>}
  </span>
);

export const SpinnerOverlay: React.FC<{ label?: string }> = ({ label = 'Loading…' }) => (
  <div className="absolute inset-0 flex items-center justify-center bg-surface-base/60 backdrop-blur-sm z-10">
    <Spinner size={24} label={label} />
  </div>
);
