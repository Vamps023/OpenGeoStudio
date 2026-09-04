// Right-click context menu for the Roads tab (SCANeR-style contextual
// actions on tracks, functions, intersections and exits).
import { useEffect, useRef } from 'react'

export interface ContextMenuItem {
  label: string
  shortcut?: string
  disabled?: boolean
  danger?: boolean
  separatorBefore?: boolean
  onSelect: () => void
}

export function RoadsContextMenu({
  position,
  title,
  items,
  onClose,
}: {
  position: { x: number; y: number }
  title: string
  items: ContextMenuItem[]
  onClose: () => void
}) {
  const ref = useRef<HTMLDivElement>(null)

  useEffect(() => {
    function handlePointerDown(event: MouseEvent) {
      if (ref.current && !ref.current.contains(event.target as Node)) onClose()
    }
    function handleKey(event: KeyboardEvent) {
      if (event.key === 'Escape') onClose()
    }
    window.addEventListener('pointerdown', handlePointerDown, true)
    window.addEventListener('keydown', handleKey)
    return () => {
      window.removeEventListener('pointerdown', handlePointerDown, true)
      window.removeEventListener('keydown', handleKey)
    }
  }, [onClose])

  const left = Math.min(position.x, window.innerWidth - 260)
  const top = Math.min(position.y, window.innerHeight - items.length * 28 - 40)

  return (
    <div
      ref={ref}
      className="fixed z-50 min-w-56 overflow-hidden rounded-lg border border-border bg-popover py-1 shadow-xl"
      style={{ left: Math.max(4, left), top: Math.max(4, top) }}
    >
      <div className="px-3 py-1 text-[10px] font-bold tracking-wider text-muted-foreground uppercase">
        {title}
      </div>
      {items.map((item, index) => (
        <div key={item.label}>
          {item.separatorBefore && index > 0 && <div className="my-1 h-px bg-border" />}
          <button
            type="button"
            disabled={item.disabled}
            className={
              item.disabled
                ? 'flex w-full items-center justify-between px-3 py-1.5 text-left text-xs text-muted-foreground/50'
                : item.danger
                  ? 'flex w-full items-center justify-between px-3 py-1.5 text-left text-xs text-red-400 hover:bg-accent'
                  : 'flex w-full items-center justify-between px-3 py-1.5 text-left text-xs hover:bg-accent'
            }
            onClick={() => {
              onClose()
              item.onSelect()
            }}
          >
            <span>{item.label}</span>
            {item.shortcut && <span className="ml-3 text-[10px] text-muted-foreground">{item.shortcut}</span>}
          </button>
        </div>
      ))}
    </div>
  )
}
