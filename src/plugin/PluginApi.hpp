// ═══════════════════════════════════════════════════════════
// OpenGeoStudio — C++ Plugin API (Header Skeleton)
// ═══════════════════════════════════════════════════════════
//
// Phase 2a deliverable. Defines the plugin interface for the Qt 6
// native application. Maps directly from the TypeScript Plugin /
// PluginCapability / PluginManager types in core/interfaces.ts.
//
// This is a HEADER-ONLY skeleton — no concrete implementations.
// It compiles with Qt 6 + C++20 and is linkable from Phase 2b's
// CMake build. Phase 3 will integrate PluginManager into
// ApplicationContext.
//
// See docs/PLUGIN_ABI.md for the full design specification.
// ═══════════════════════════════════════════════════════════

#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QPluginLoader>
#include <QDir>
#include <QStandardPaths>
#include <QVersionNumber>
#include <QCoreApplication>
#include "../core/logger/Logger.hpp"
#include <memory>

// ─── Forward declarations ────────────────────────────────────

class ApplicationContext;

// ═══════════════════════════════════════════════════════════
// PluginCapability — maps TS PluginCapability type
// ═══════════════════════════════════════════════════════════

struct PluginCapability {
    /// Capability type — 1:1 with TS PluginCapability.type
    enum Type {
        Importer,             ///< { type: 'importer'; format: string }
        Exporter,             ///< { type: 'exporter'; format: string }
        RoadGenerator,        ///< { type: 'road-generator'; name: string }
        TerrainProcessor,     ///< { type: 'terrain-processor'; name: string }
        Validator,            ///< { type: 'validator'; name: string }
        VisualizationLayer,   ///< { type: 'visualization-layer'; name: string }
        Tool                  ///< { type: 'tool'; name: string }
    };

    Type type;
    /// Format string (for importer/exporter) or name (for others)
    QString name;
    /// Format identifier (importer/exporter only); empty for others
    QString format;

    /// Convert type to string (for serialization/logging)
    static QString typeToString(Type t) {
        switch (t) {
            case Importer:           return QStringLiteral("importer");
            case Exporter:           return QStringLiteral("exporter");
            case RoadGenerator:      return QStringLiteral("road-generator");
            case TerrainProcessor:   return QStringLiteral("terrain-processor");
            case Validator:          return QStringLiteral("validator");
            case VisualizationLayer: return QStringLiteral("visualization-layer");
            case Tool:               return QStringLiteral("tool");
        }
        return QStringLiteral("unknown");
    }

    /// Parse type from string (for metadata JSON parsing)
    static Type typeFromString(const QString& s) {
        if (s == "importer")             return Importer;
        if (s == "exporter")             return Exporter;
        if (s == "road-generator")       return RoadGenerator;
        if (s == "terrain-processor")    return TerrainProcessor;
        if (s == "validator")            return Validator;
        if (s == "visualization-layer")  return VisualizationLayer;
        if (s == "tool")                 return Tool;
        return Tool; // fallback
    }
};

// ═══════════════════════════════════════════════════════════
// IPlugin — maps TS Plugin (extends Module)
// ═══════════════════════════════════════════════════════════
//
// A plugin is a shared library loaded at runtime via QPluginLoader.
// It inherits QObject and uses Q_DECLARE_INTERFACE so the host can
// qobject_cast<IPlugin*> on the loaded instance.
//
// To create a plugin, inherit from IPlugin and use:
//   Q_INTERFACES(IPlugin)
//   Q_PLUGIN_METADATA(IID "opengeostudio.plugin/1.0" FILE "myplugin.json")
//
// ═══════════════════════════════════════════════════════════

class IPlugin : public QObject {
public:
    virtual ~IPlugin() = default;

    // ─── Module identity (maps TS Module interface) ────────

    /// Unique plugin identifier (e.g. "osm-importer")
    virtual QString id() const = 0;

    /// Human-readable name
    virtual QString name() const = 0;

    /// Semantic version string (e.g. "1.0.0")
    virtual QString version() const = 0;

    /// Optional description
    virtual QString description() const { return {}; }

    /// Optional author
    virtual QString author() const { return {}; }

    // ─── Capabilities (maps TS Plugin.provides) ───────────

    /// Capabilities this plugin provides
    virtual QList<PluginCapability> capabilities() const = 0;

    // ─── Lifecycle (maps TS Module.init/dispose) ──────────

    /// Called once on app startup. Throw to abort boot.
    /// ctx is the ApplicationContext (DI root). In Phase 2a this is
    /// a forward declaration; Phase 3 provides the full definition.
    virtual void init(ApplicationContext* ctx) { (void)ctx; }

    /// Called on app shutdown / plugin unload
    virtual void dispose() {}
};

// Qt interface declaration — enables qobject_cast<IPlugin*>
Q_DECLARE_INTERFACE(IPlugin, "opengeostudio.plugin/1.0")

// ═══════════════════════════════════════════════════════════
// PluginManager — maps TS PluginManager
// ═══════════════════════════════════════════════════════════
//
// Registers, tracks, and queries plugins by capability.
// Owned by ApplicationContext (Phase 3).
//
// ═══════════════════════════════════════════════════════════

class PluginManager : public QObject {
public:
    explicit PluginManager(QObject* parent = nullptr)
        : QObject(parent) {}

    // ─── Registration (maps TS PluginManager.register/unregister) ─

    /// Register a loaded plugin. Takes ownership (parent-child).
    void registerPlugin(IPlugin* plugin) {
        if (!plugin) return;
        plugin->setParent(this);
        m_plugins.append(plugin);
        emit pluginRegistered(plugin->id());
    }

    /// Unregister a plugin by ID. Does not delete the plugin.
    void unregisterPlugin(const QString& pluginId) {
        for (int i = 0; i < m_plugins.size(); ++i) {
            if (m_plugins[i]->id() == pluginId) {
                IPlugin* p = m_plugins.takeAt(i);
                emit pluginUnregistered(pluginId);
                p->setParent(nullptr);
                return;
            }
        }
    }

    // ─── Queries (maps TS PluginManager.getAll/getByCapability) ────

    /// All registered plugins
    QList<IPlugin*> allPlugins() const { return m_plugins; }

    /// Plugins matching a specific capability type
    QList<IPlugin*> pluginsByCapability(PluginCapability::Type type) const {
        QList<IPlugin*> result;
        for (IPlugin* p : m_plugins) {
            for (const auto& cap : p->capabilities()) {
                if (cap.type == type) {
                    result.append(p);
                    break;
                }
            }
        }
        return result;
    }

    /// Plugins matching a specific capability type + name/format
    QList<IPlugin*> pluginsByCapability(PluginCapability::Type type,
                                        const QString& nameOrFormat) const {
        QList<IPlugin*> result;
        for (IPlugin* p : m_plugins) {
            for (const auto& cap : p->capabilities()) {
                if (cap.type == type &&
                    (cap.name == nameOrFormat || cap.format == nameOrFormat)) {
                    result.append(p);
                    break;
                }
            }
        }
        return result;
    }

    /// Find a plugin by ID
    IPlugin* pluginById(const QString& id) const {
        for (IPlugin* p : m_plugins) {
            if (p->id() == id) return p;
        }
        return nullptr;
    }

    /// Number of registered plugins
    int count() const { return m_plugins.size(); }

signals:
    void pluginRegistered(const QString& id);
    void pluginUnregistered(const QString& id);

private:
    QList<IPlugin*> m_plugins;
};

// ═══════════════════════════════════════════════════════════
// PluginLoader — maps TS PluginLoader
// ═══════════════════════════════════════════════════════════
//
// Discovers and loads plugin shared libraries from the filesystem
// using QPluginLoader. Handles metadata validation, version
// compatibility, and dependency resolution.
//
// ═══════════════════════════════════════════════════════════

class PluginLoader : public QObject {
public:
    explicit PluginLoader(PluginManager* manager, QObject* parent = nullptr)
        : QObject(parent), m_manager(manager) {}

    /// Discover plugin directories in a folder.
    /// A plugin directory is one containing a shared library with
    /// matching Q_PLUGIN_METADATA.
    QStringList discoverPlugins(const QString& pluginsDir) const {
        QStringList result;
        QDir dir(pluginsDir);
        if (!dir.exists()) return result;

        const QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDir);
        for (const QString& entry : entries) {
            QDir pluginDir(dir.absoluteFilePath(entry));
            // Look for shared library with same name as directory
            const QStringList filters =
#ifdef Q_OS_WIN
                { entry + ".dll" };
#elif defined(Q_OS_MAC)
                { "lib" + entry + ".dylib", entry + ".dylib" };
#else
                { "lib" + entry + ".so", entry + ".so" };
#endif
            pluginDir.setNameFilters(filters);
            const QStringList libs = pluginDir.entryList(QDir::Files);
            if (!libs.isEmpty()) {
                result.append(pluginDir.absoluteFilePath(libs.first()));
            }
        }
        return result;
    }

    /// Load a single plugin from a shared library path.
    /// Returns the loaded plugin, or nullptr on failure.
    IPlugin* loadPlugin(const QString& libraryPath, ApplicationContext* ctx) {
        QPluginLoader loader(libraryPath);

        // Read metadata without loading
        const QJsonObject metadata = loader.metaData();
        if (metadata.isEmpty()) {
            appLog().warn("Plugin metadata is empty for", libraryPath);
            return nullptr;
        }

        // Check minimum host version
        const QString minHostVersion = metadata.value("minHostVersion").toString();
        if (!minHostVersion.isEmpty()) {
            const QVersionNumber hostVersion =
                QVersionNumber::fromString(QCoreApplication::applicationVersion());
            const QVersionNumber required = QVersionNumber::fromString(minHostVersion);
            if (hostVersion < required) {
                appLog().warn("Plugin", libraryPath, "requires host version", minHostVersion, "but host is", hostVersion.toString());
                return nullptr;
            }
        }

        // Check dependencies
        const QJsonArray deps = metadata.value("dependencies").toArray();
        for (const QJsonValue& dep : deps) {
            const QString depId = dep.toString();
            if (!m_manager->pluginById(depId)) {
                appLog().warn("Plugin", libraryPath, "requires", depId, "which is not loaded");
                return nullptr;
            }
        }

        // Load and instantiate
        QObject* instance = loader.instance();
        if (!instance) {
            appLog().warn("Failed to load plugin", libraryPath, ":", loader.errorString());
            return nullptr;
        }

        IPlugin* plugin = qobject_cast<IPlugin*>(instance);
        if (!plugin) {
            appLog().warn("Plugin", libraryPath, "does not implement IPlugin interface");
            delete instance;
            return nullptr;
        }

        // Initialize
        plugin->init(ctx);

        // Register
        m_manager->registerPlugin(plugin);
        m_loaders.insert(plugin->id(), std::make_unique<QPluginLoader>(std::move(loader)));

        appLog().info("Plugin loaded:", plugin->name(), "v", plugin->version());
        return plugin;
    }

    /// Unload a plugin by ID
    void unloadPlugin(const QString& pluginId) {
        IPlugin* plugin = m_manager->pluginById(pluginId);
        if (!plugin) return;

        plugin->dispose();
        m_manager->unregisterPlugin(pluginId);

        auto it = m_loaders.find(pluginId);
        if (it != m_loaders.end()) {
            it.value()->unload();  // Best-effort; may not work on all platforms
            m_loaders.erase(it);
        }
    }

    /// Load all plugins from a directory
    void loadAll(const QString& pluginsDir, ApplicationContext* ctx) {
        const QStringList paths = discoverPlugins(pluginsDir);
        for (const QString& path : paths) {
            loadPlugin(path, ctx);
        }
    }

    /// Get default plugins directory
    static QString defaultPluginsDir() {
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
               + "/plugins";
    }

private:
    PluginManager* m_manager;
    /// Keep QPluginLoader instances alive (needed for unload)
    QMap<QString, std::unique_ptr<QPluginLoader>> m_loaders;
};
