import { useEffect } from 'react'
import type { Frame } from '../engine/xyFunctions'
import type { EditorSelection, Tool } from '../state/store'

export interface KeyboardShortcutHandlers {
  tool: Tool
  selection: EditorSelection
  dragSnapRef: { current: { roadId: string; contact: 'start' | 'end'; frame: Frame } | null }
  onFinishPointDraft: () => void
  onEscape: () => void
  onDeleteSelection: () => void
  onLinkIntersectionToTracks: () => void
  onInvertAuthorizations: () => void
  onUndo: () => void
  onRedo: () => void
}

/**
 * Global editor shortcuts (Enter finish draft, Escape cancel, Delete
 * selection, Ctrl+L link, Ctrl+Shift+I invert authorisations). Resubscribes
 * every render so handlers always see fresh state.
 */
export function useKeyboardShortcuts(handlers: KeyboardShortcutHandlers) {
  useEffect(() => {
    function handleKeyDown(event: KeyboardEvent) {
      const target = event.target as HTMLElement
      if (target instanceof HTMLInputElement || target instanceof HTMLSelectElement || target instanceof HTMLTextAreaElement) return
      const { tool, selection } = handlers
      if (event.key === 'Enter' && (tool === 'draw-polyline' || tool === 'draw-spline')) {
        handlers.onFinishPointDraft()
      }
      if (event.key === 'Escape') {
        handlers.onEscape()
      }
      if ((event.key === 'Delete' || event.key === 'Backspace') && tool === 'select') {
        handlers.onDeleteSelection()
      }
      if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'l') {
        event.preventDefault()
        handlers.onLinkIntersectionToTracks()
      }
      if ((event.ctrlKey || event.metaKey) && event.shiftKey && event.key.toLowerCase() === 'i') {
        event.preventDefault()
        handlers.onInvertAuthorizations()
      }
      if ((event.ctrlKey || event.metaKey) && !event.shiftKey && event.key.toLowerCase() === 'z') {
        event.preventDefault()
        handlers.onUndo()
      }
      if ((event.ctrlKey || event.metaKey) && (event.key.toLowerCase() === 'y' || (event.shiftKey && event.key.toLowerCase() === 'z'))) {
        event.preventDefault()
        handlers.onRedo()
      }
    }
    window.addEventListener('keydown', handleKeyDown)
    return () => window.removeEventListener('keydown', handleKeyDown)
  })
}
