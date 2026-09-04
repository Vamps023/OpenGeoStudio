// Helper functions for encoding/decoding lane and border selection keys.
// These are stored in the global store (`selectedLaneKey` / `selectedBorderKey`).

export function laneKey(side: 'left' | 'right', index: number) {
  return `${side}:${index}`
}

export function parseLaneKey(key: string): { side: 'left' | 'right'; index: number } | null {
  const m = key.match(/^(left|right):(\d+)$/)
  if (!m) return null
  return { side: m[1] as 'left' | 'right', index: parseInt(m[2], 10) }
}

export function borderKey(side: 'left' | 'right', edge: 'inner' | 'outer', index?: number) {
  return `border:${side}:${edge}${index !== undefined ? ':' + index : ''}`
}

export function parseBorderKey(key: string): { side: 'left' | 'right'; edge: 'inner' | 'outer'; index?: number } | null {
  const m = key.match(/^border:(left|right):(inner|outer)(?::(\d+))?$/)
  if (!m) return null
  return { side: m[1] as 'left' | 'right', edge: m[2] as 'inner' | 'outer', index: m[3] ? parseInt(m[3], 10) : undefined }
}
