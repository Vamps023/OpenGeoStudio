import type { LaneSectionDef } from './types'

export interface LaneStrip {
  inner: number
  outer: number
}

export interface LaneLayout {
  left: LaneStrip[]
  right: LaneStrip[]
  totalLeft: number
  totalRight: number
}

export function laneLayout(section: LaneSectionDef): LaneLayout {
  const build = (widths: number[], sign: 1 | -1): LaneStrip[] => {
    const strips: LaneStrip[] = []
    let acc = 0
    for (const w of widths) {
      strips.push({ inner: acc * sign, outer: (acc + w) * sign })
      acc += w
    }
    return strips
  }
  return {
    left: build(section.left, 1),
    right: build(section.right, -1),
    totalLeft: section.left.reduce((a, w) => a + w, 0),
    totalRight: section.right.reduce((a, w) => a + w, 0),
  }
}
