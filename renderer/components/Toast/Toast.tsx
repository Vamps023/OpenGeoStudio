import { useEffect } from 'react';
import { useTerrainStore } from '../../core/store';
import { CheckCircle, AlertCircle, Info, AlertTriangle, X } from 'lucide-react';
import type { ToastAction } from '../../../shared/types/terrain';

export function ToastContainer(): React.JSX.Element {
  const notifications = useTerrainStore((s) => s.notifications);
  const removeNotification = useTerrainStore((s) => s.removeNotification);

  return (
    <div
      className="fixed top-12 right-4 z-[300] flex flex-col gap-2 pointer-events-none max-w-[400px]"
      role="region"
      aria-label="Notifications"
      aria-live="polite"
    >
      {notifications.map((n) => (
        <Toast
          key={n.id}
          notification={n}
          onClose={removeNotification}
        />
      ))}
    </div>
  );
};

interface ToastProps {
  notification: {
    id: string;
    type: 'success' | 'error' | 'info' | 'warning';
    title?: string;
    message: string;
    actions?: ToastAction[];
    timeout?: number;
  };
  onClose: (id: string) => void;
}

function Toast({ notification, onClose }: ToastProps): React.JSX.Element {
  // Default: errors persist until dismissed; others auto-dismiss after 5s
  const timeout = notification.timeout ?? (notification.type === 'error' ? 0 : 5000);

  useEffect(() => {
    if (timeout === 0) return;
    const timer = setTimeout(() => onClose(notification.id), timeout);
    return () => clearTimeout(timer);
  }, [notification.id, timeout, onClose]);

  const config = {
    success: { icon: CheckCircle,   bg: 'bg-ok/10',   border: 'border-ok/30',   text: 'text-ok',   bar: 'bg-ok' },
    error:   { icon: AlertCircle,   bg: 'bg-err/10',  border: 'border-err/30',  text: 'text-err',  bar: 'bg-err' },
    warning: { icon: AlertTriangle, bg: 'bg-warn/10', border: 'border-warn/30', text: 'text-warn', bar: 'bg-warn' },
    info:    { icon: Info,          bg: 'bg-info/10', border: 'border-info/30', text: 'text-info', bar: 'bg-info' },
  }[notification.type] ?? { icon: Info, bg: 'bg-info/10', border: 'border-info/30', text: 'text-info', bar: 'bg-info' };

  const Icon = config.icon;

  return (
    <div
      role="alert"
      className={`pointer-events-auto min-w-[300px] bg-surface-elevated border-l-2 ${config.border} border border-edge rounded-md shadow-overlay overflow-hidden`}
      style={{ borderLeftColor: 'currentColor' }}
    >
      <div className={`h-0.5 ${config.bar}`} />
      <div className="p-3 flex items-start gap-3">
        <div className={`w-7 h-7 rounded flex items-center justify-center shrink-0 ${config.bg} ${config.border} border`}>
          <Icon className={`w-4 h-4 ${config.text}`} />
        </div>
        <div className="flex-1 min-w-0">
          {notification.title && (
            <p className="text-2xs font-semibold text-fg-primary mb-0.5">{notification.title}</p>
          )}
          <p className="text-2xs text-fg-secondary leading-snug">{notification.message}</p>
          {notification.actions && notification.actions.length > 0 && (
            <div className="flex items-center gap-2 mt-2">
              {notification.actions.map((action, i) => (
                <button
                  key={i}
                  onClick={() => { action.onClick(); onClose(notification.id); }}
                  className={`px-2 py-0.5 text-3xs font-medium rounded transition-colors ${
                    i === 0
                      ? `${config.bg} ${config.text} hover:brightness-125`
                      : 'bg-surface-hover text-fg-secondary hover:text-fg-primary'
                  }`}
                >
                  {action.label}
                </button>
              ))}
            </div>
          )}
        </div>
        <button
          onClick={() => onClose(notification.id)}
          className="text-fg-muted hover:text-fg-primary transition-colors shrink-0"
          aria-label="Dismiss notification"
        >
          <X className="w-3.5 h-3.5" />
        </button>
      </div>
    </div>
  );
};
