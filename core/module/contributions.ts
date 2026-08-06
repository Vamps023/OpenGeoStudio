/**
 * Contribution Registry — central registry for module contributions.
 *
 * Modules register their panels, providers, toolbar items, commands,
 * validators, importers, exporters, and node-graph nodes here during init().
 * The UI and core services query this registry to build the interface dynamically.
 */

import type { Logger } from '../interfaces';
import type { DEMProvider, ImageryProvider, VectorDataProvider } from '../providers';

// ─── Panel Contribution ───────────────────────────────────────

export interface PanelContribution {
  /** Unique panel ID (e.g. "layer-stack", "road-inspector") */
  id: string;
  /** Display title */
  title: string;
  /** Icon name (lucide-react icon name) */
  icon: string;
  /** Which dock this panel belongs to */
  dock: 'left' | 'right' | 'bottom' | 'center' | 'floating';
  /** Module that registered this panel */
  moduleId: string;
  /** React component path (lazy-loaded) or component reference for renderer */
  component: string;
  /** Default width/height when docked */
  defaultWidth?: number;
  defaultHeight?: number;
  /** Whether this panel is shown by default */
  defaultVisible?: boolean;
}

// ─── Toolbar Contribution ─────────────────────────────────────

export interface ToolbarContribution {
  /** Command ID to execute when clicked */
  commandId: string;
  /** Display label */
  label: string;
  /** Icon name */
  icon: string;
  /** Tooltip */
  tooltip?: string;
  /** Sort order */
  order?: number;
  /** Module that registered this tool */
  moduleId: string;
}

// ─── Validator Contribution ───────────────────────────────────

export interface ValidatorContribution {
  id: string;
  name: string;
  moduleId: string;
  /** What this validator checks */
  targetType: 'terrain' | 'road' | 'railway' | 'export' | 'opendrive' | 'project';
  /** Run validation and return issues */
  validate: (target: unknown) => Promise<ValidationIssue[]>;
}

export interface ValidationIssue {
  severity: 'error' | 'warning' | 'info';
  message: string;
  /** Path to the problematic data (e.g. "road[3].lanes[1]") */
  path?: string;
  /** Suggested fix */
  suggestion?: string;
}

// ─── Importer/Exporter Contributions ──────────────────────────

export interface ImporterContribution {
  id: string;
  name: string;
  moduleId: string;
  /** File extensions (e.g. ["shp", "geojson"]) */
  extensions: string[];
  /** Import function */
  import: (filePath: string) => Promise<unknown>;
}

export interface ExporterContribution {
  id: string;
  name: string;
  moduleId: string;
  /** File extension this exporter produces */
  extension: string;
  /** Export function */
  export: (data: unknown, outputPath: string) => Promise<void>;
}

// ─── Node Graph Contribution ──────────────────────────────────

export interface NodeGraphContribution {
  /** Node type identifier */
  type: string;
  /** Display label */
  label: string;
  /** Category for grouping in the node palette */
  category: string;
  /** Module that registered this node */
  moduleId: string;
  /** Input port definitions */
  inputs: NodePortDef[];
  /** Output port definitions */
  outputs: NodePortDef[];
  /** Execute the node's computation */
  execute: (inputs: Record<string, unknown>, context: NodeExecutionContext) => Promise<Record<string, unknown>>;
}

export interface NodePortDef {
  id: string;
  label: string;
  /** Data type (e.g. "dem", "imagery", "road-network") */
  dataType: string;
  /** Whether multiple connections can be made to this port */
  multiple?: boolean;
}

export interface NodeExecutionContext {
  logger: Logger;
  /** Report progress (0..1) */
  reportProgress: (percentage: number, message?: string) => void;
  /** Check if execution should cancel */
  isCancelled: () => boolean;
}

// ─── Registry Implementation ──────────────────────────────────

export class ContributionRegistry {
  private panels = new Map<string, PanelContribution>();
  private toolbar: ToolbarContribution[] = [];
  private validators = new Map<string, ValidatorContribution>();
  private importers = new Map<string, ImporterContribution>();
  private exporters = new Map<string, ExporterContribution>();
  private nodes = new Map<string, NodeGraphContribution>();
  private demProviders = new Map<string, DEMProvider>();
  private imageryProviders = new Map<string, ImageryProvider>();
  private vectorProviders = new Map<string, VectorDataProvider>();

  constructor(private logger: Logger) {}

  // Panels
  registerPanel(panel: PanelContribution): void {
    if (this.panels.has(panel.id)) {
      this.logger.warn(`Panel already registered: ${panel.id}`);
      return;
    }
    this.panels.set(panel.id, panel);
  }
  getPanel(id: string): PanelContribution | undefined { return this.panels.get(id); }
  getAllPanels(): PanelContribution[] { return Array.from(this.panels.values()); }
  getPanelsByModule(moduleId: string): PanelContribution[] {
    return this.getAllPanels().filter(p => p.moduleId === moduleId);
  }
  getPanelsByDock(dock: PanelContribution['dock']): PanelContribution[] {
    return this.getAllPanels().filter(p => p.dock === dock);
  }
  unregisterPanel(id: string): void { this.panels.delete(id); }

  // Toolbar
  registerToolbar(tool: ToolbarContribution): void {
    this.toolbar.push(tool);
    this.toolbar.sort((a, b) => (a.order ?? 0) - (b.order ?? 0));
  }
  getToolbar(): ToolbarContribution[] { return this.toolbar; }
  getToolbarByModule(moduleId: string): ToolbarContribution[] {
    return this.toolbar.filter(t => t.moduleId === moduleId);
  }

  // Validators
  registerValidator(v: ValidatorContribution): void {
    this.validators.set(v.id, v);
  }
  getValidator(id: string): ValidatorContribution | undefined { return this.validators.get(id); }
  getAllValidators(): ValidatorContribution[] { return Array.from(this.validators.values()); }
  getValidatorsByTarget(targetType: ValidatorContribution['targetType']): ValidatorContribution[] {
    return this.getAllValidators().filter(v => v.targetType === targetType);
  }

  // Importers
  registerImporter(i: ImporterContribution): void {
    this.importers.set(i.id, i);
  }
  getImporter(id: string): ImporterContribution | undefined { return this.importers.get(id); }
  getAllImporters(): ImporterContribution[] { return Array.from(this.importers.values()); }
  getImporterForExtension(ext: string): ImporterContribution | undefined {
    return this.getAllImporters().find(i => i.extensions.includes(ext));
  }

  // Exporters
  registerExporter(e: ExporterContribution): void {
    this.exporters.set(e.id, e);
  }
  getExporter(id: string): ExporterContribution | undefined { return this.exporters.get(id); }
  getAllExporters(): ExporterContribution[] { return Array.from(this.exporters.values()); }
  getExporterForExtension(ext: string): ExporterContribution | undefined {
    return this.getAllExporters().find(e => e.extension === ext);
  }

  // Node Graph
  registerNode(node: NodeGraphContribution): void {
    this.nodes.set(node.type, node);
  }
  getNode(type: string): NodeGraphContribution | undefined { return this.nodes.get(type); }
  getAllNodes(): NodeGraphContribution[] { return Array.from(this.nodes.values()); }
  getNodesByCategory(category: string): NodeGraphContribution[] {
    return this.getAllNodes().filter(n => n.category === category);
  }
  getNodesByModule(moduleId: string): NodeGraphContribution[] {
    return this.getAllNodes().filter(n => n.moduleId === moduleId);
  }

  // DEM Providers
  registerDEMProvider(provider: DEMProvider): void {
    this.demProviders.set(provider.id, provider);
  }
  getDEMProvider(id: string): DEMProvider | undefined { return this.demProviders.get(id); }
  getAllDEMProviders(): DEMProvider[] { return Array.from(this.demProviders.values()); }

  // Imagery Providers
  registerImageryProvider(provider: ImageryProvider): void {
    this.imageryProviders.set(provider.id, provider);
  }
  getImageryProvider(id: string): ImageryProvider | undefined { return this.imageryProviders.get(id); }
  getAllImageryProviders(): ImageryProvider[] { return Array.from(this.imageryProviders.values()); }

  // Vector Providers
  registerVectorProvider(provider: VectorDataProvider): void {
    this.vectorProviders.set(provider.id, provider);
  }
  getVectorProvider(id: string): VectorDataProvider | undefined { return this.vectorProviders.get(id); }
  getAllVectorProviders(): VectorDataProvider[] { return Array.from(this.vectorProviders.values()); }

  /** Remove all contributions from a module (called when module is disposed) */
  unregisterModule(moduleId: string): void {
    for (const [id, p] of this.panels) {
      if (p.moduleId === moduleId) this.panels.delete(id);
    }
    this.toolbar = this.toolbar.filter(t => t.moduleId !== moduleId);
    for (const [id, v] of this.validators) {
      if (v.moduleId === moduleId) this.validators.delete(id);
    }
    for (const [id, i] of this.importers) {
      if (i.moduleId === moduleId) this.importers.delete(id);
    }
    for (const [id, e] of this.exporters) {
      if (e.moduleId === moduleId) this.exporters.delete(id);
    }
    for (const [id, n] of this.nodes) {
      if (n.moduleId === moduleId) this.nodes.delete(id);
    }
  }
}
