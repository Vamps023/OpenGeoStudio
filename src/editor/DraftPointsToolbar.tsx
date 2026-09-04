import { Button } from '@/components/ui/button'

/** Floating toolbar while drawing a multi-point draft (polyline / spline). */
export default function DraftPointsToolbar({
  count,
  onFinish,
  onCancel,
}: {
  count: number
  onFinish: () => void
  onCancel: () => void
}) {
  return (
    <div className="absolute top-3 left-1/2 z-10 flex -translate-x-1/2 items-center gap-2 rounded-lg border border-border bg-card/90 px-2 py-1.5 shadow-lg backdrop-blur">
      <span className="pl-1 text-xs text-muted-foreground">
        {count} point{count === 1 ? '' : 's'}
      </span>
      <Button size="sm" className="h-7" disabled={count < 2} onClick={onFinish}>
        Finish
      </Button>
      <Button size="sm" variant="ghost" className="h-7" onClick={onCancel}>
        Cancel
      </Button>
    </div>
  )
}
