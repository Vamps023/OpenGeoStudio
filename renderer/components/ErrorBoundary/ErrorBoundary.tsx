/**
 * Error Boundary — catches render errors in child components.
 *
 * Without this, any throw during render unmounts the entire React tree
 * and shows a blank screen. This boundary shows a recoverable error
 * message instead.
 */

import React from 'react';

interface ErrorBoundaryProps {
  children: React.ReactNode;
  /** Panel ID for context in the error message */
  panelId?: string;
}

interface ErrorBoundaryState {
  hasError: boolean;
  error: Error | null;
}

export class ErrorBoundary extends React.Component<ErrorBoundaryProps, ErrorBoundaryState> {
  constructor(props: ErrorBoundaryProps) {
    super(props);
    this.state = { hasError: false, error: null };
  }

  static getDerivedStateFromError(error: Error): ErrorBoundaryState {
    return { hasError: true, error };
  }

  componentDidCatch(error: Error, errorInfo: React.ErrorInfo): void {
    console.error(`[ErrorBoundary]${this.props.panelId ? ` Panel "${this.props.panelId}":` : ''}`, error, errorInfo);
  }

  render(): React.ReactNode {
    if (this.state.hasError) {
      return (
        <div className="flex flex-col items-center justify-center h-full p-6 text-center select-none">
          <div className="mb-3 flex items-center justify-center w-12 h-12 rounded-lg bg-err/10 border border-err/30">
            <span className="text-err text-sm font-bold">!</span>
          </div>
          <div className="text-err text-sm font-medium mb-1">Component Error</div>
          <div className="text-fg-muted text-2xs mb-1">
            {this.props.panelId ? `Panel: ${this.props.panelId}` : 'A component failed to render'}
          </div>
          <div className="text-fg-muted text-3xs max-w-md break-all mb-3 font-mono">
            {this.state.error?.message ?? 'Unknown error'}
          </div>
          <button
            onClick={() => this.setState({ hasError: false, error: null })}
            className="inline-flex items-center gap-1.5 px-3 py-1.5 text-2xs font-medium
              rounded bg-surface-elevated border border-edge hover:bg-surface-hover
              text-fg-primary transition-colors"
          >
            Retry
          </button>
        </div>
      );
    }
    return this.props.children;
  }
}

export default ErrorBoundary;
