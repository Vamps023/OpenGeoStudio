// ─────────────────────────────────────────────────────────────────────
// PortionProfileEditor — SCANeR Lanes tab workflows:
//  · Portion editing (5.5.5.3): portions list, name, end abscissa,
//    Road Type (Bridge/Tunnel), split/delete portion
//  · Profile editing (5.5.5.4): rename profile, invert profile,
//    export profile as road style, change road style (user library)
// ─────────────────────────────────────────────────────────────────────
import { useMemo } from 'react'
import { toast } from 'sonner'
import { useStore, getLaneSection, uuid } from '../state/store'
import type { RoadData, RoadType } from '../state/store'
import { Button } from '@/components/ui/button'
import { Input } from '@/components/ui/input'
import { Label } from '@/components/ui/label'
import { Badge } from '@/components/ui/badge'
import { Separator } from '@/components/ui/separator'

interface PortionProfileEditorProps {
  road: RoadData
  length: number
}

const STYLE_LIB_KEY = 'ogs.roadStyles.v1'

interface RoadStyle {
  name: string
  left: unknown[]
  right: unknown[]
}

function loadStyles(): RoadStyle[] {
  try {
    const raw = localStorage.getItem(STYLE_LIB_KEY)
    const parsed = raw ? JSON.parse(raw) : []
    return Array.isArray(parsed) ? parsed : []
  } catch {
    return []
  }
}

function saveStyles(styles: RoadStyle[]) {
  localStorage.setItem(STYLE_LIB_KEY, JSON.stringify(styles))
}

export default function PortionProfileEditor({ road, length }: PortionProfileEditorProps) {
  const updateRoad = useStore((s) => s.updateRoad)

  // Portions (default: one uniform portion over the whole track)
  const portions = useMemo(() => {
    if (road.portions && road.portions.length > 0) return road.portions
    return [{ id: 'p0', name: 'Portion 1', sEnd: length, roadType: 'none' as RoadType }]
  }, [road.portions, length])

  function writePortions(next: typeof portions) {
    updateRoad(road.id, { portions: next })
  }

  function setPortion(index: number, patch: Partial<(typeof portions)[number]>) {
    writePortions(portions.map((p, i) => (i === index ? { ...p, ...patch } : p)))
  }

  function splitPortion(index: number) {
    // 'Split portion': insert a juxtaposition at the portion midpoint
    const p = portions[index]
    const prevEnd = index > 0 ? portions[index - 1].sEnd : 0
    const mid = Math.max(prevEnd + 1, Math.min(p.sEnd - 1, (prevEnd + p.sEnd) / 2))
    if (mid <= prevEnd || mid >= p.sEnd) {
      toast.error('Portion too short to split.')
      return
    }
    const next = [...portions]
    next[index] = { ...p, sEnd: mid, name: `${p.name} A` }
    next.splice(index + 1, 0, { id: uuid(), name: `${p.name} B`, sEnd: p.sEnd, roadType: p.roadType })
    writePortions(next)
    toast.success('Portion split (profile juxtaposition inserted)')
  }

  function deletePortion(index: number) {
    if (portions.length <= 1) {
      toast.error('Cannot delete the only portion of a track.')
      return
    }
    if (index === 0) {
      toast.error('Cannot delete the first portion from its start profile.')
      return
    }
    writePortions(portions.filter((_, i) => i !== index))
    toast.success('Portion deleted')
  }

  // Profile (road style) workflows
  const profileName = road.profileName ?? 'Default'

  function renameProfile(name: string) {
    updateRoad(road.id, { profileName: name })
  }

  function invertProfile() {
    const section = getLaneSection(road)
    updateRoad(road.id, {
      laneSection: { left: section.right, right: section.left },
      lanesLeft: section.right.length,
      lanesRight: section.left.length,
    })
    toast.success('Profile inverted')
  }

  function exportProfileAsRoadStyle() {
    const section = getLaneSection(road)
    const style: RoadStyle = { name: profileName, left: section.left, right: section.right }
    const styles = loadStyles().filter((s) => s.name !== profileName)
    styles.push(style)
    saveStyles(styles)
    // download a copy (RoadStyle file)
    try {
      const blob = new Blob([JSON.stringify(style, null, 2)], { type: 'application/json' })
      const url = URL.createObjectURL(blob)
      const a = document.createElement('a')
      a.href = url
      a.download = `${profileName}.roadStyle.json`
      a.click()
      URL.revokeObjectURL(url)
    } catch {
      // download blocked — the style is still in the library
    }
    toast.success(`Profile exported as road style “${profileName}”`)
  }

  function changeRoadStyle(name: string) {
    const style = loadStyles().find((s) => s.name === name)
    if (!style) return
    updateRoad(road.id, {
      laneSection: { left: style.left as never, right: style.right as never },
      lanesLeft: (style.left as { length: number }).length,
      lanesRight: (style.right as { length: number }).length,
      profileName: name,
    })
    toast.success(`Road style changed to “${name}” on all portions`)
  }

  const styles = loadStyles()

  return (
    <div className="grid gap-3 p-3 text-xs">
      <Separator />
      <div className="flex items-center justify-between">
        <h3 className="text-[11px] font-bold tracking-wider text-muted-foreground uppercase">Road Portion</h3>
        <Badge variant="muted">{portions.length}</Badge>
      </div>
      <p className="text-[11px] leading-relaxed text-muted-foreground">
        Portions split the track along its abscissa. A Bridge portion is ignored by terrain generation; a Tunnel tells traffic to switch on headlights.
      </p>
      <div className="grid gap-2">
        {portions.map((portion, index) => (
          <div key={portion.id} className="grid gap-1.5 rounded-md border border-border bg-muted/40 px-2 py-2">
            <div className="grid grid-cols-[1fr_4.5rem] items-center gap-1.5">
              <Input
                className="h-6 text-xs"
                value={portion.name}
                onChange={(e) => setPortion(index, { name: e.target.value })}
              />
              <Input
                type="number"
                className="h-6 text-xs"
                title="End abscissa"
                min={0}
                step={1}
                value={Math.round(portion.sEnd)}
                disabled={index === portions.length - 1 && portions.length > 1 ? false : index === 0 && portions.length === 1}
                onChange={(e) => {
                  const prevEnd = index > 0 ? portions[index - 1].sEnd : 0
                  const v = Math.max(prevEnd + 1, Math.min(length, Number.parseFloat(e.target.value) || 0))
                  setPortion(index, { sEnd: v })
                }}
              />
            </div>
            <div className="grid grid-cols-[5.5rem_1fr] items-center gap-1.5">
              <span className="text-[11px] text-muted-foreground">Road type</span>
              <select
                className="h-6 rounded-md border border-border bg-background px-1 text-xs"
                value={portion.roadType}
                onChange={(e) => setPortion(index, { roadType: e.target.value as RoadType })}
              >
                <option value="none">Nothing</option>
                <option value="bridge">Bridge</option>
                <option value="tunnel">Tunnel</option>
              </select>
            </div>
            <div className="flex items-center justify-between text-[10px] text-muted-foreground">
              <span>{index === 0 ? 0 : Math.round(portions[index - 1].sEnd)} – {Math.round(portion.sEnd)} m</span>
              <span className="flex gap-1">
                <Button size="sm" variant="ghost" className="h-5 px-1.5 text-[10px]" onClick={() => splitPortion(index)}>
                  Split
                </Button>
                <Button size="sm" variant="ghost" className="h-5 px-1.5 text-[10px] text-red-400" onClick={() => deletePortion(index)}>
                  Delete
                </Button>
              </span>
            </div>
          </div>
        ))}
      </div>

      <Separator />

      <h3 className="text-[11px] font-bold tracking-wider text-muted-foreground uppercase">Profile / Road style</h3>
      <div className="grid gap-1.5">
        <Label className="text-[11px] text-muted-foreground">Profile name (press Enter)</Label>
        <Input
          className="h-7 text-xs"
          defaultValue={profileName}
          key={profileName}
          onKeyDown={(e) => {
            if (e.key === 'Enter') {
              renameProfile((e.target as HTMLInputElement).value)
              toast.success('Profile renamed')
            }
          }}
        />
      </div>
      <div className="grid grid-cols-2 gap-1.5">
        <Button size="sm" variant="outline" className="h-7 text-xs" onClick={invertProfile}>
          Invert Profile
        </Button>
        <Button size="sm" variant="outline" className="h-7 text-xs" onClick={exportProfileAsRoadStyle}>
          Export as Style
        </Button>
      </div>
      <div className="grid gap-1.5">
        <Label className="text-[11px] text-muted-foreground">Change road style (user library)</Label>
        <select
          className="h-7 rounded-md border border-border bg-background px-2 text-xs"
          value=""
          onChange={(e) => {
            if (e.target.value) changeRoadStyle(e.target.value)
          }}
        >
          <option value="">— select a saved road style —</option>
          {styles.map((style) => (
            <option key={style.name} value={style.name}>
              {style.name}
            </option>
          ))}
        </select>
      </div>
    </div>
  )
}
