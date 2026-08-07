/**
 * GeoTerrain Studio — Typed IPC Channel Definitions
 * Single source of truth for all IPC channel names between main and renderer.
 */

// ─── Native / Export ────────────────────────────────────────
export const NATIVE_GET_VERSION = 'native:getVersion' as const;
export const NATIVE_GET_MEMORY_USAGE = 'native:getMemoryUsage' as const;
export const NATIVE_PLAN_GENERATION = 'native:planGeneration' as const;
export const NATIVE_START_GENERATION = 'native:startGeneration' as const;
export const NATIVE_CANCEL_GENERATION = 'native:cancelGeneration' as const;
export const NATIVE_GET_PROGRESS = 'native:getProgress' as const;
export const NATIVE_EXPORT_PACKAGE = 'native:exportPackage' as const;
export const NATIVE_CANCEL_EXPORT = 'native:cancelExport' as const;
export const NATIVE_PROGRESS_UPDATE = 'native:progressUpdate' as const;

// ─── OpenDRIVE ──────────────────────────────────────────────
export const OPENDRIVE_EXPORT = 'opendrive:export' as const;
export const OPENDRIVE_PREVIEW = 'opendrive:preview' as const;
export const OPENDRIVE_READ = 'opendrive:read' as const;
export const OPENDRIVE_VALIDATE = 'opendrive:validate' as const;

// ─── Road Engine (C++ native addon) ─────────────────────────
export const ROAD_GET_VERSION = 'road:getVersion' as const;
export const ROAD_GENERATE_INTERSECTION = 'road:generateIntersection' as const;
export const ROAD_COMPUTE_CIRCLE_ARC = 'road:computeCircleArc' as const;
export const ROAD_SAMPLE_CENTERLINE = 'road:sampleCenterline' as const;
export const ROAD_GEO_TO_LOCAL = 'road:geoToLocal' as const;
export const ROAD_LOCAL_TO_GEO = 'road:localToGeo' as const;
export const ROAD_COMPUTE_CLOTHOID = 'road:computeClothoid' as const;
export const ROAD_GENERATE_ROAD_MESH = 'road:generateRoadMesh' as const;
export const ROAD_GENERATE_INTERSECTION_MESH = 'road:generateIntersectionMesh' as const;
export const ROAD_EXPORT_OPENDRIVE = 'road:exportOpenDrive' as const;

// ─── Road Creation Tools (SCANeR-style) ─────────────────────
export const ROAD_CREATE_SEGMENT = 'road:createSegment' as const;
export const ROAD_CREATE_CIRCLE_ARC = 'road:createCircleArc' as const;
export const ROAD_CREATE_CLOTHOID_ARC = 'road:createClothoidArc' as const;
export const ROAD_CREATE_POLYLINE = 'road:createPolyline' as const;
export const ROAD_CREATE_BEZIER = 'road:createBezier' as const;
export const ROAD_CREATE_CLOTHOID_SPLINE = 'road:createClothoidSpline' as const;

// Phase 1.9 — RoadV2 bridge integration
export const ROAD_SAMPLE_CENTERLINE_V2 = 'road:sampleCenterlineV2' as const;
export const ROAD_GET_ADAPTER_REPORT = 'road:getAdapterReport' as const;
export const ROAD_CONVERT_FROM_V2 = 'road:convertFromV2' as const;

// Phase 2.8 — Full lane engine pipeline
export const ROAD_BUILD_ROAD = 'road:buildRoad' as const;

// ─── Dialog ───────────────────────────────────────────────────
export const DIALOG_SELECT_FOLDER = 'dialog:selectFolder' as const;
export const DIALOG_SELECT_PACKAGE = 'dialog:selectPackage' as const;
export const DIALOG_SAVE_PROJECT = 'dialog:saveProject' as const;
export const DIALOG_LOAD_PROJECT = 'dialog:loadProject' as const;
export const DIALOG_NEW_PROJECT = 'dialog:newProject' as const;
export const DIALOG_IMPORT_FILE = 'dialog:importFile' as const;
export const DIALOG_GET_DEFAULT_PROJECTS_DIR = 'dialog:getDefaultProjectsDir' as const;

// ─── Settings ─────────────────────────────────────────────────
export const SETTINGS_GET_API_KEYS = 'settings:getApiKeys' as const;
export const SETTINGS_SET_API_KEYS = 'settings:setApiKeys' as const;

// ─── File System ────────────────────────────────────────────
export const FS_SAVE_PROJECT = 'fs:saveProject' as const;
export const FS_LOAD_PROJECT = 'fs:loadProject' as const;
export const FS_READ_MANIFEST = 'fs:readManifest' as const;
export const FS_WRITE_MANIFEST = 'fs:writeManifest' as const;
export const FS_READ_FILE_BINARY = 'fs:readFileBinary' as const;
export const FS_WRITE_FILE_BINARY = 'fs:writeFileBinary' as const;

// ─── Core: Job System ────────────────────────────────────────
export const JOB_SUBMIT = 'job:submit' as const;
export const JOB_CANCEL = 'job:cancel' as const;
export const JOB_GET = 'job:get' as const;
export const JOB_GET_ALL = 'job:getAll' as const;
export const JOB_PROGRESS_UPDATE = 'job:progressUpdate' as const;

// ─── Core: Notifications ─────────────────────────────────────
export const NOTIFICATION_SHOW = 'notification:show' as const;
export const NOTIFICATION_DISMISS = 'notification:dismiss' as const;
export const NOTIFICATION_GET_ALL = 'notification:getAll' as const;
export const NOTIFICATION_UPDATE = 'notification:update' as const;

// ─── Core: Commands ──────────────────────────────────────────
export const COMMAND_EXECUTE = 'command:execute' as const;
export const COMMAND_GET_ALL = 'command:getAll' as const;
export const COMMAND_GET_BY_CATEGORY = 'command:getByCategory' as const;

// ─── Core: Selection ─────────────────────────────────────────
export const SELECTION_GET = 'selection:get' as const;
export const SELECTION_SELECT = 'selection:select' as const;
export const SELECTION_DESELECT = 'selection:deselect' as const;
export const SELECTION_DESELECT_ALL = 'selection:deselectAll' as const;
export const SELECTION_CHANGED = 'selection:changed' as const;

// ─── Core: Workspace ─────────────────────────────────────────
export const WORKSPACE_GET_ALL = 'workspace:getAll' as const;
export const WORKSPACE_ACTIVATE = 'workspace:activate' as const;
export const WORKSPACE_GET_ACTIVE = 'workspace:getActive' as const;
export const WORKSPACE_ACTIVATED = 'workspace:activated' as const;

// ─── Core: Project ───────────────────────────────────────────
export const PROJECT_CREATE = 'project:create' as const;
export const PROJECT_CREATE_WITH_FOLDER = 'project:createWithFolder' as const;
export const PROJECT_OPEN = 'project:open' as const;
export const PROJECT_SAVE = 'project:save' as const;
export const PROJECT_SAVE_AS = 'project:saveAs' as const;
export const PROJECT_CLOSE = 'project:close' as const;
export const PROJECT_MARK_DIRTY = 'project:markDirty' as const;
export const PROJECT_GET_ACTIVE = 'project:getActive' as const;
export const PROJECT_CHANGED = 'project:changed' as const;
export const PROJECT_GET_RECENT = 'project:getRecent' as const;
export const PROJECT_CLEAR_RECENT = 'project:clearRecent' as const;
export const PROJECT_GET_SUBFOLDER = 'project:getSubfolder' as const;
export const PROJECT_GET_EXPORT_PATH = 'project:getExportPath' as const;
export const PROJECT_AUTOSAVE = 'project:autosave' as const;
export const PROJECT_SET_RECENT_FILE = 'project:setRecentFile' as const;

// ProjectContext — renderer → main state sync (single source of truth)
export const PROJECT_CONTEXT_SYNC_TERRAIN = 'projectContext:syncTerrain' as const;
export const PROJECT_CONTEXT_SYNC_GIS = 'projectContext:syncGIS' as const;
export const PROJECT_CONTEXT_SYNC_SCENE = 'projectContext:syncScene' as const;
export const PROJECT_CONTEXT_SYNC_VIEWPORT = 'projectContext:syncViewport' as const;
export const PROJECT_CONTEXT_SYNC_ASSETS = 'projectContext:syncAssets' as const;
export const PROJECT_CONTEXT_SYNC_LAYER_VISIBILITY = 'projectContext:syncLayerVisibility' as const;
export const PROJECT_CONTEXT_GET_STATE = 'projectContext:getState' as const;
export const PROJECT_CONTEXT_GET_STAGE = 'projectContext:getStage' as const;
export const PROJECT_CONTEXT_RESTORED = 'projectContext:restored' as const;

// ─── Core: Contributions ─────────────────────────────────────
export const CONTRIBUTION_GET_PANELS = 'contribution:getPanels' as const;
export const CONTRIBUTION_GET_TOOLBAR = 'contribution:getToolbar' as const;
export const CONTRIBUTION_GET_NODES = 'contribution:getNodes' as const;
export const CONTRIBUTION_GET_VALIDATORS = 'contribution:getValidators' as const;

// ─── Core: Plugin ────────────────────────────────────────────
export const PLUGIN_GET_ALL = 'plugin:getAll' as const;
export const PLUGIN_RELOAD = 'plugin:reload' as const;

// ─── Core: Scene Graph ───────────────────────────────────────
export const SCENE_GET_ROOT = 'scene:getRoot' as const;
export const SCENE_GET_ALL_NODES = 'scene:getAllNodes' as const;
export const SCENE_GET_NODE = 'scene:getNode' as const;
export const SCENE_ADD_NODE = 'scene:addNode' as const;
export const SCENE_REMOVE_NODE = 'scene:removeNode' as const;
export const SCENE_UPDATE_NODE = 'scene:updateNode' as const;
export const SCENE_RENAME_NODE = 'scene:renameNode' as const;
export const SCENE_SET_VISIBLE = 'scene:setVisible' as const;
export const SCENE_SET_LOCKED = 'scene:setLocked' as const;
export const SCENE_REPARENT = 'scene:reparent' as const;
export const SCENE_SELECT = 'scene:select' as const;
export const SCENE_GET_SELECTION = 'scene:getSelection' as const;
export const SCENE_NODE_UPDATED = 'scene:nodeUpdated' as const;

// ─── Channel groups for validation ────────────────────────────
export const NATIVE_CHANNELS = [
  NATIVE_GET_VERSION,
  NATIVE_PLAN_GENERATION,
  NATIVE_START_GENERATION,
  NATIVE_CANCEL_GENERATION,
  NATIVE_GET_PROGRESS,
  NATIVE_EXPORT_PACKAGE,
  NATIVE_CANCEL_EXPORT,
  NATIVE_PROGRESS_UPDATE,
] as const;

export const DIALOG_CHANNELS = [
  DIALOG_SELECT_FOLDER,
  DIALOG_SELECT_PACKAGE,
  DIALOG_SAVE_PROJECT,
  DIALOG_LOAD_PROJECT,
  DIALOG_NEW_PROJECT,
  DIALOG_IMPORT_FILE,
  DIALOG_GET_DEFAULT_PROJECTS_DIR,
] as const;

export const SETTINGS_CHANNELS = [
  SETTINGS_GET_API_KEYS,
  SETTINGS_SET_API_KEYS,
] as const;

export const FS_CHANNELS = [
  FS_SAVE_PROJECT,
  FS_LOAD_PROJECT,
  FS_READ_MANIFEST,
  FS_WRITE_MANIFEST,
  FS_READ_FILE_BINARY,
] as const;

export type NativeChannel = typeof NATIVE_CHANNELS[number];
export type DialogChannel = typeof DIALOG_CHANNELS[number];
export type SettingsChannel = typeof SETTINGS_CHANNELS[number];
export type FsChannel = typeof FS_CHANNELS[number];
export type IpcChannel = NativeChannel | DialogChannel | SettingsChannel | FsChannel;
