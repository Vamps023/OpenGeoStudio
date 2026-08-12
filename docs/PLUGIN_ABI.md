# OpenGeoStudio — C++ Plugin ABI Design Specification

> Phase 2a deliverable. Defines the C++ plugin interface for the Qt 6
> native application, mapping directly from the existing TypeScript
> `Plugin` / `PluginCapability` / `PluginManager` types in
> `core/interfaces.ts` and `core/module/plugin-loader.ts`.
> Status: **Design + header skeleton — no implementations yet.**

---

## 1. Goals

1. **Preserve the TS plugin model.** The TS app defines `Plugin extends Module`
   with `provides: PluginCapability[]` and a `PluginManager` that registers,
   queries by capability, and loads/unloads. The C++ ABI must support the
   same operations.
2. **Use Qt's native plugin system.** `QPluginLoader` + `Q_DECLARE_INTERFACE`
   + `Q_PLUGIN_METADATA` is the standard Qt mechanism for runtime-loaded
   shared libraries. It handles platform-specific DLL/SO/dylib loading,
   metadata extraction, and instance creation.
3. **Be header-only and compile-clean.** The skeleton defines interfaces
   only — no concrete implementations. It must compile with Qt 6 + C++20
   and be linkable from Phase 2b's CMake build.
4. **Support capability-based discovery.** Plugins declare what they
   provide (importer, exporter, road-generator, terrain-processor,
   validator, visualization-layer, tool). The host queries by capability
   type, not by plugin ID.
5. **Support versioning.** Plugins declare a semantic version and a
   minimum host version. The loader rejects incompatible plugins.

---

## 2. TS → C++ Mapping

| TypeScript (`core/interfaces.ts`) | C++ (`plugin/PluginApi.hpp`) |
|---|---|
| `interface Module { id, name, version, description?, author?, init?(ctx), dispose?() }` | `class IPlugin { id(), name(), version(), description(), author(), init(ctx), dispose() }` |
| `interface Plugin extends Module { provides: PluginCapability[] }` | `class IPlugin` has `capabilities()` returning `QList<PluginCapability>` |
| `type PluginCapability = { type: 'importer'; format: string } \| ...` | `struct PluginCapability { enum Type; QString name; QString format; }` |
| `interface PluginManager { register, unregister, getAll, getByCapability }` | `class PluginManager : public QObject` with `registerPlugin`, `unregisterPlugin`, `allPlugins`, `pluginsByCapability` |
| `interface PluginManifest { id, name, version, ... enabled, dependencies }` | Qt metadata JSON via `Q_PLUGIN_METADATA(IID, FILE)` — the manifest is embedded in the shared library |
| `class PluginLoader { loadPlugin, unloadPlugin, discoverPlugins, loadAll }` | `class PluginLoader : public QObject` using `QPluginLoader` internally |

### 2.1 Capability Types (1:1 with TS)

| TS `PluginCapability.type` | C++ `PluginCapability::Type` | Description |
|---|---|---|
| `'importer'` | `Importer` | Imports a file format (e.g. OSM, GeoJSON) |
| `'exporter'` | `Exporter` | Exports to a file format (e.g. OpenDRIVE, GeoTIFF) |
| `'road-generator'` | `RoadGenerator` | Generates road geometry from parameters |
| `'terrain-processor'` | `TerrainProcessor` | Processes terrain data (e.g. DEM merging) |
| `'validator'` | `Validator` | Validates project/road/terrain data |
| `'visualization-layer'` | `VisualizationLayer` | Adds a render layer to the viewport |
| `'tool'` | `Tool` | Adds an editing tool to the toolbar |

---

## 3. QPluginLoader Mechanism

### 3.1 How Qt Plugins Work

1. A plugin is a shared library (`.dll` / `.so` / `.dylib`) that exports a
   class inheriting from `QObject` with `Q_PLUGIN_METADATA` and
   `Q_INTERFACES` macros.
2. The host uses `QPluginLoader` to load the shared library, call
   `instance()` to get a `QObject*`, and `qobject_cast<IPlugin*>` to get
   the typed interface.
3. Metadata is embedded via `Q_PLUGIN_METADATA(IID "opengeostudio.plugin/1.0"
   FILE "pluginmetadata.json")` — the JSON file is compiled into the
   library as a Qt resource and can be read without loading the library.

### 3.2 Plugin Discovery

```
QDir pluginsDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/plugins");
for (const QString& entry : pluginsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    QString pluginPath = pluginsDir.absoluteFilePath(entry + "/" + entry + ".dll"); // or .so/.dylib
    QPluginLoader loader(pluginPath);
    QJsonObject metadata = loader.metaData();  // Read without loading
    if (isCompatible(metadata)) {
        QObject* instance = loader.instance();  // Load + instantiate
        IPlugin* plugin = qobject_cast<IPlugin*>(instance);
        if (plugin) pluginManager->registerPlugin(plugin);
    }
}
```

### 3.3 Metadata JSON (embedded via `Q_PLUGIN_METADATA`)

```json
{
    "id": "osm-importer",
    "name": "OSM Railway Importer",
    "version": "1.0.0",
    "description": "Imports railway data from OpenStreetMap",
    "author": "OpenGeoStudio",
    "minHostVersion": "0.1.0",
    "capabilities": [
        { "type": "importer", "format": "osm-xml" },
        { "type": "tool", "name": "osm-railway-tool" }
    ]
}
```

### 3.4 Dynamic-Load Strategy

- **Location:** `QStandardPaths::AppDataLocation + "/plugins/"` (per-user)
  and optionally `<app-install-dir>/plugins/` (bundled).
- **Load timing:** At startup (after `ApplicationContext` is built) and
  on-demand (hot-load via `PluginLoader::loadPlugin(path)`).
- **Unload:** `QPluginLoader::unload()` attempts to unload the library.
  Note: Qt cannot guarantee unloading on all platforms (Windows may keep
  the DLL mapped). The `dispose()` method is always called before unload
  to release resources.
- **Error handling:** If `loader.instance()` returns `nullptr` or
  `qobject_cast` fails, the error is logged via `Logger` and the plugin
  is skipped (matching the TS behavior of logging + returning null).

---

## 4. Versioning

### 4.1 Plugin ABI Version

The `Q_PLUGIN_METADATA` IID includes a version: `"opengeostudio.plugin/1.0"`.
This is the **ABI version** — it changes only when the `IPlugin` interface
itself changes (new virtual methods, changed signatures). Plugins compiled
against ABI 1.0 will not load in a host that expects 2.0.

### 4.2 Plugin Semantic Version

Each plugin reports its own semantic version via `IPlugin::version()`. This
is for display and dependency resolution, not ABI compatibility.

### 4.3 Minimum Host Version

The metadata JSON includes `minHostVersion`. The loader compares this
against the host application version (`QCoreApplication::applicationVersion()`).
Plugins requiring a newer host are rejected with a log message.

### 4.4 Dependency Resolution

The metadata JSON includes `dependencies` (array of plugin IDs). The loader
processes plugins in dependency order — if a dependency is not yet loaded,
the plugin is deferred and retried after its dependencies (matching the TS
`PluginLoader` behavior).

---

## 5. C++ Header Skeleton

The header skeleton is at `app/native/src/plugin/PluginApi.hpp`. It defines:

- `PluginCapability` struct (type + name + format)
- `IPlugin` interface (inherits `QObject`, uses `Q_DECLARE_INTERFACE`)
- `PluginManager` class (registers, queries, tracks plugins)
- `PluginLoader` class (discovers, loads, unloads via `QPluginLoader`)

The skeleton compiles as a header-only definition with no concrete plugin
implementations. Phase 2b will link against it; Phase 3 will integrate
`PluginManager` into `ApplicationContext`.

---

## 6. Future Considerations (not in scope for Phase 2a)

- **Sandboxing:** Qt plugins run in-process with full host privileges.
  If sandboxing is needed, consider a separate process + IPC (like VS Code
  extensions). Not needed for v1.
- **Plugin settings UI:** Plugins may need configuration widgets. A
  `IConfigurable` mixin could provide a `createSettingsWidget()` method.
  Defer to Phase 3.
- **Plugin marketplace / signing:** Code signing for plugins. Defer to
  Phase 9 (Packaging).
- **Hot-reload:** `QPluginLoader::unload()` + `load()` cycle. May not work
  reliably on Windows. Defer.
