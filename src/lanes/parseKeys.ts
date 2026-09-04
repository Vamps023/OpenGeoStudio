// Helper utilities for parsing lane / border selection keys.

export function parseKey(key: string): { side: 'left' | 'right'; index: number } | null {
  const [side, idx] = key.split(':')
  if ((side !== 'left' && side !== 'right') || idx === undefined) return null
  const index = Number(idx)
  if (!Number.isFinite(index) || index < 0) return null
  return { side: side as 'left' | 'right', index }
}

export function parseBorderKey(key: string): { side: 'left' | 'right'; index: number; edge: 'inner' | 'outer' } | null {
  const parts = key.split(':')
  if (parts.length !== 4) return null
  const [, side, idx, edge] = parts
  if ((side !== 'left' && side !== 'right') || (edge !== 'inner' && edge !== 'outer')) return null
  const index = Number(idx)
  if (!Number.isFinite(index) || index < 0) return null
  return { side: side as 'left' | 'right', index, edge: edge as 'inner' | 'outer' }
}

export function clamp(v: number, min: number, max: number): number {
  if (Number.isNaN(v)) return min
  return Math.min(max, Math.max(min, v))
}
