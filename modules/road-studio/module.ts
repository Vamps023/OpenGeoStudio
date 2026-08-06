/**
 * Road Studio Module — Plugin definition
 */
import type { Module } from '../../core/interfaces';

export const RoadStudioModule: Module = {
  id: 'road-studio',
  name: 'Road Studio',
  version: '1.0.0',
  description: '2D/3D road editing with Bezier pen tool and elevation control',

  async init(): Promise<void> {
    // Road Studio is entirely client-side — no server initialization needed
  },

  async dispose(): Promise<void> {},
};
