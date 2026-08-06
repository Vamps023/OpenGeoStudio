/**
 * Shared form field components used by all inspector panels.
 *
 * These components provide a consistent UI for editing properties
 * across RoadInspector, RailwayInspector, ScenarioEditor, and
 * PropertyInspector.
 *
 * Extracted during standardization phase to eliminate duplication.
 */

import React, { useState } from 'react';
import { ChevronRight, ChevronDown } from 'lucide-react';

// ─── Collapsible Section ──────────────────────────────────────

interface SectionProps {
  title: string;
  icon?: React.ComponentType<{ size?: number; className?: string }>;
  defaultOpen?: boolean;
  children: React.ReactNode;
}

export const FormSection: React.FC<SectionProps> = ({ title, icon: Icon, defaultOpen = true, children }) => {
  const [open, setOpen] = useState(defaultOpen);
  return (
    <div className="border-b border-edge">
      <button
        onClick={() => setOpen(o => !o)}
        className="w-full flex items-center gap-2 px-3 py-2 text-xs font-medium text-fg-secondary hover:bg-surface-panel transition-colors"
      >
        {open ? <ChevronDown size={12} /> : <ChevronRight size={12} />}
        {Icon && <Icon size={12} className="text-fg-muted" />}
        {title}
      </button>
      {open && <div className="px-3 pb-3 space-y-2">{children}</div>}
    </div>
  );
};

// ─── Number Field ─────────────────────────────────────────────

export const NumberField: React.FC<{
  label: string;
  value: number;
  unit?: string;
  step?: number;
  min?: number;
  max?: number;
  onChange: (v: number) => void;
}> = ({ label, value, unit, step = 1, min, max, onChange }) => (
  <div className="flex items-center justify-between text-xs">
    <span className="text-fg-muted">{label}</span>
    <div className="flex items-center gap-1">
      <input
        type="number"
        value={value}
        step={step}
        min={min}
        max={max}
        aria-label={label}
        onChange={(e) => onChange(parseFloat(e.target.value) || 0)}
        className="w-20 px-1.5 py-0.5 bg-surface-panel text-fg-primary rounded border border-edge text-right"
      />
      {unit && <span className="text-3xs text-fg-muted w-8">{unit}</span>}
    </div>
  </div>
);

// ─── Text Field ───────────────────────────────────────────────

export const TextField: React.FC<{
  label: string;
  value: string;
  onChange: (v: string) => void;
  placeholder?: string;
}> = ({ label, value, onChange, placeholder }) => (
  <div className="flex items-center justify-between text-xs">
    <span className="text-fg-muted">{label}</span>
    <input
      type="text"
      value={value}
      aria-label={label}
      onChange={(e) => onChange(e.target.value)}
      placeholder={placeholder}
      className="w-40 px-1.5 py-0.5 bg-surface-panel text-fg-primary rounded border border-edge"
    />
  </div>
);

// ─── Select Field ─────────────────────────────────────────────

export const SelectField: React.FC<{
  label: string;
  value: string;
  options: { value: string; label: string }[] | string[];
  onChange: (v: string) => void;
}> = ({ label, value, options, onChange }) => {
  const normalized = options.map(o => typeof o === 'string' ? { value: o, label: o } : o);
  return (
    <div className="flex items-center justify-between text-xs">
      <span className="text-fg-muted">{label}</span>
      <select
        value={value}
        aria-label={label}
        onChange={(e) => onChange(e.target.value)}
        className="px-1.5 py-0.5 bg-surface-panel text-fg-primary rounded border border-edge"
      >
        {normalized.map(o => <option key={o.value} value={o.value}>{o.label}</option>)}
      </select>
    </div>
  );
};

// ─── Boolean/Toggle Field ─────────────────────────────────────

export const BooleanField: React.FC<{
  label: string;
  value: boolean;
  onChange: (v: boolean) => void;
}> = ({ label, value, onChange }) => (
  <div className="flex items-center justify-between text-xs">
    <span className="text-fg-muted">{label}</span>
    <button
      onClick={() => onChange(!value)}
      role="switch"
      aria-checked={value}
      aria-label={label}
      className={`w-9 h-5 rounded-full transition-colors ${value ? 'bg-accent' : 'bg-surface-hover'}`}
    >
      <div className={`w-4 h-4 bg-white rounded-full transition-transform ${value ? 'translate-x-4' : 'translate-x-0.5'}`} />
    </button>
  </div>
);
