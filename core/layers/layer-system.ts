/**
 * Layer System — manages GIS layers in the map viewport.
 *
 * Layers are stacked data overlays on the map: DEM, imagery, roads,
 * buildings, railways, water, vegetation, custom imports, etc.
 * Each layer has visibility, opacity, z-order, and style properties.
 */

import type { EventBus } from '../interfaces';
import type { Logger } from '../interfaces';

export type LayerType =
  | 'dem'
  | 'imagery'
  | 'roads'
  | 'railways'
  | 'buildings'
  | 'water'
  | 'rivers'
  | 'lakes'
  | 'vegetation'
  | 'landuse'
  | 'traffic-signs'
  | 'traffic-signals'
  | 'utilities'
  | 'sidewalks'
  | 'road-objects'
  | 'speed-limits'
  | 'road-rules'
  | 'junctions'
  | 'lane-markings'
  | 'custom'
  | 'mask';

export interface LayerStyle {
  /** Fill color (CSS color string) */
  fillColor?: string;
  /** Stroke color */
  strokeColor?: string;
  /** Stroke width in pixels */
  strokeWidth?: number;
  /** Opacity 0..1 */
  opacity?: number;
  /** Whether to show labels */
  showLabels?: boolean;
  /** Label field */
  labelField?: string;
  /** Min zoom level to show */
  minZoom?: number;
  /** Max zoom level to show */
  maxZoom?: number;
}

export interface Layer {
  id: string;
  name: string;
  type: LayerType;
  visible: boolean;
  /** Z-order (higher = on top) */
  zIndex: number;
  /** Opacity 0..1 */
  opacity: number;
  /** Layer source (URL, file path, or data reference) */
  source?: string;
  /** Style properties */
  style: LayerStyle;
  /** Whether this layer can be edited */
  editable: boolean;
  /** Whether this layer is locked */
  locked: boolean;
  /** Module that created this layer */
  moduleId?: string;
  /** Arbitrary metadata */
  metadata?: Record<string, unknown>;
}

// ─── Events ───────────────────────────────────────────────────

export const LAYER_EVENTS = {
  ADDED: 'layer:added',
  REMOVED: 'layer:removed',
  UPDATED: 'layer:updated',
  VISIBILITY_CHANGED: 'layer:visibility-changed',
  ORDER_CHANGED: 'layer:order-changed',
  CLEARED: 'layer:cleared',
  ACTIVE_CHANGED: 'layer:active-changed',
} as const;

// ─── Implementation ───────────────────────────────────────────

let nextLayerId = 0;

export class LayerSystem {
  private layers = new Map<string, Layer>();
  private order: string[] = []; // bottom to top
  private activeId: string | null = null;

  constructor(
    private events: EventBus,
    private logger: Logger,
  ) {}

  add(layer: Omit<Layer, 'id' | 'zIndex'>): Layer {
    const id = `layer-${++nextLayerId}`;
    const full: Layer = {
      ...layer,
      id,
      zIndex: this.order.length,
    };
    this.layers.set(id, full);
    this.order.push(id);
    this.events.emit(LAYER_EVENTS.ADDED, full);
    return full;
  }

  remove(id: string): void {
    const layer = this.layers.get(id);
    if (!layer) return;
    this.layers.delete(id);
    this.order = this.order.filter(lid => lid !== id);
    if (this.activeId === id) this.activeId = null;
    // Re-index z-order
    this.reindex();
    this.events.emit(LAYER_EVENTS.REMOVED, { id });
  }

  update(id: string, updates: Partial<Layer>): void {
    const layer = this.layers.get(id);
    if (!layer) return;
    Object.assign(layer, updates);
    this.events.emit(LAYER_EVENTS.UPDATED, { id, updates });
  }

  setVisible(id: string, visible: boolean): void {
    const layer = this.layers.get(id);
    if (!layer) return;
    layer.visible = visible;
    this.events.emit(LAYER_EVENTS.VISIBILITY_CHANGED, { id, visible });
  }

  setOpacity(id: string, opacity: number): void {
    const layer = this.layers.get(id);
    if (!layer) return;
    layer.opacity = Math.max(0, Math.min(1, opacity));
    layer.style.opacity = layer.opacity;
    this.events.emit(LAYER_EVENTS.UPDATED, { id, updates: { opacity: layer.opacity } });
  }

  /** Move a layer up in the stack */
  moveUp(id: string): void {
    const idx = this.order.indexOf(id);
    if (idx < 0 || idx >= this.order.length - 1) return;
    this.order.splice(idx, 1);
    this.order.splice(idx + 1, 0, id);
    this.reindex();
    this.events.emit(LAYER_EVENTS.ORDER_CHANGED, this.getAll());
  }

  /** Move a layer down in the stack */
  moveDown(id: string): void {
    const idx = this.order.indexOf(id);
    if (idx <= 0) return;
    this.order.splice(idx, 1);
    this.order.splice(idx - 1, 0, id);
    this.reindex();
    this.events.emit(LAYER_EVENTS.ORDER_CHANGED, this.getAll());
  }

  /** Set the active layer (for editing) */
  setActive(id: string | null): void {
    this.activeId = id;
    this.events.emit(LAYER_EVENTS.ACTIVE_CHANGED, id);
  }

  getActive(): Layer | undefined {
    return this.activeId ? this.layers.get(this.activeId) : undefined;
  }

  get(id: string): Layer | undefined { return this.layers.get(id); }

  getAll(): Layer[] {
    return this.order.map(id => this.layers.get(id)).filter(Boolean) as Layer[];
  }

  getVisible(): Layer[] {
    return this.getAll().filter(l => l.visible);
  }

  getByType(type: LayerType): Layer[] {
    return this.getAll().filter(l => l.type === type);
  }

  clear(): void {
    this.layers.clear();
    this.order = [];
    this.activeId = null;
    this.events.emit(LAYER_EVENTS.CLEARED, {});
  }

  private reindex(): void {
    for (let i = 0; i < this.order.length; i++) {
      const layer = this.layers.get(this.order[i]);
      if (layer) layer.zIndex = i;
    }
  }
}

// ─── Default layers ───────────────────────────────────────────

export function createDefaultLayers(layerSystem: LayerSystem): void {
  layerSystem.add({
    name: 'Satellite Imagery',
    type: 'imagery',
    visible: true,
    opacity: 1.0,
    source: 'arcgis',
    style: { opacity: 1.0 },
    editable: false,
    locked: false,
    moduleId: 'gis',
  });

  layerSystem.add({
    name: 'DEM',
    type: 'dem',
    visible: false,
    opacity: 0.7,
    source: 'aws-terrarium',
    style: { opacity: 0.7, fillColor: '#4a7c3f' },
    editable: false,
    locked: false,
    moduleId: 'terrain',
  });

  layerSystem.add({
    name: 'Roads',
    type: 'roads',
    visible: true,
    opacity: 1.0,
    style: { strokeColor: '#c4a96b', strokeWidth: 2, opacity: 1.0 },
    editable: true,
    locked: false,
    moduleId: 'gis',
  });

  layerSystem.add({
    name: 'Buildings',
    type: 'buildings',
    visible: true,
    opacity: 0.8,
    style: { fillColor: '#8b7355', strokeColor: '#5a4a3a', strokeWidth: 1, opacity: 0.8 },
    editable: true,
    locked: false,
    moduleId: 'gis',
  });

  layerSystem.add({
    name: 'Railways',
    type: 'railways',
    visible: true,
    opacity: 1.0,
    style: { strokeColor: '#7ab86f', strokeWidth: 2, opacity: 1.0 },
    editable: true,
    locked: false,
    moduleId: 'gis',
  });

  layerSystem.add({
    name: 'Water',
    type: 'water',
    visible: true,
    opacity: 0.7,
    style: { fillColor: '#3b82f6', strokeColor: '#1e40af', strokeWidth: 1, opacity: 0.7 },
    editable: false,
    locked: false,
    moduleId: 'gis',
  });

  layerSystem.add({
    name: 'Vegetation',
    type: 'vegetation',
    visible: false,
    opacity: 0.6,
    style: { fillColor: '#22c55e', opacity: 0.6 },
    editable: false,
    locked: false,
    moduleId: 'gis',
  });
}
