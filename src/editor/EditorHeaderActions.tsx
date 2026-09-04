import type { RefObject } from 'react'
import { FileUp, Map as MapIcon } from 'lucide-react'
import { Button } from '@/components/ui/button'

export interface EditorHeaderActionsProps {
  mode: '2d' | '3d'
  onModeChange: (mode: '2d' | '3d') => void
  showMap: boolean
  onToggleMap: () => void
  importInputRef: RefObject<HTMLInputElement | null>
  onImportClick: () => void
  onImportFile: (event: import('react').ChangeEvent<HTMLInputElement>) => void
}

/** Header controls: 2D/3D toggle, OpenDRIVE import, map background toggle. */
export default function EditorHeaderActions({
  mode,
  onModeChange,
  showMap,
  onToggleMap,
  importInputRef,
  onImportClick,
  onImportFile,
}: EditorHeaderActionsProps) {
  return (
    <>
      <div className="flex items-center rounded-md border border-border bg-background p-0.5">
        <Button size="sm" variant={mode === '2d' ? 'default' : 'ghost'} className="h-7 px-3" onClick={() => onModeChange('2d')}>
          2D
        </Button>
        <Button size="sm" variant={mode === '3d' ? 'default' : 'ghost'} className="h-7 px-3" onClick={() => onModeChange('3d')}>
          3D
        </Button>
      </div>
      <input ref={importInputRef} type="file" accept=".xodr,.xml" className="hidden" onChange={onImportFile} />
      <Button
        size="sm"
        variant="ghost"
        className="h-7 gap-1.5 px-3"
        onClick={onImportClick}
        title="Import OpenDRIVE (.xodr) — roads and junctions with explicit ways"
      >
        <FileUp className="size-3.5" />
        Import
      </Button>
      {mode === '2d' && (
        <Button size="sm" variant={showMap ? 'default' : 'ghost'} className="h-7 gap-1.5 px-3" onClick={onToggleMap} title="Toggle satellite map background">
          <MapIcon className="size-3.5" />
          Map
        </Button>
      )}
    </>
  )
}
