/** "Draft length" readout shared by the TOOL panel and the Roads tab. */
export default function DraftLengthRow({ length }: { length: number }) {
  return (
    <div className="flex items-center justify-between text-xs">
      <span className="text-muted-foreground">Draft length</span>
      <b className="font-medium text-primary">{length.toFixed(1)} m</b>
    </div>
  )
}
