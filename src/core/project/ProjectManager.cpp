// ProjectManager — Project CRUD and persistence implementation

#include "ProjectManager.hpp"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QStandardPaths>
#include <QSettings>
#include <algorithm>

const QStringList ProjectManager::kSubfolders = {
    "Terrain", "GIS", "Roads", "Railway", "Scene", "Simulation",
    "Infrastructure", "Assets", "Environment", "Validation",
    "Exports", "Cache", "Temp", "Logs", "Config"
};

ProjectManager::ProjectManager(EventBus* bus, QObject* parent)
    : QObject(parent), m_bus(bus), m_log("ProjectManager") {
    // Recent projects stored in a JSON file in the app data location
    const QString dataDir = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    m_recentFilePath = dataDir + "/recent-projects.json";

    loadRecent();

    // Autosave timer (default: 60 seconds)
    m_autosaveTimer.setInterval(60000);
    connect(&m_autosaveTimer, &QTimer::timeout, this, &ProjectManager::onAutosave);
}

Project ProjectManager::create(const QString& name, const QString& workspaceId) {
    Project p;
    p.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    p.name = name;
    p.createdAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    p.modifiedAt = p.createdAt;
    p.workspaceId = workspaceId;
    p.dirty = false;

    m_current = p;
    m_log.info("Project created:", name, "id:", p.id);
    m_bus->publish("project:created", {{"id", p.id}, {"name", p.name}});
    emit projectCreated(p);
    emit projectChanged(p);
    return p;
}

Project ProjectManager::createWithFolder(const QString& name,
                                          const QString& folderPath,
                                          const QString& workspaceId) {
    Project p = create(name, workspaceId);
    p.basePath = folderPath;
    p.filePath = folderPath + "/" + name + ".ogproj";

    createFolderStructure(folderPath);

    // Save the .ogproj file immediately
    QFile file(p.filePath);
    if (file.open(QIODevice::WriteOnly)) {
        QJsonDocument doc(p.toJson());
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
        m_log.info("Project file created:", p.filePath);
    } else {
        m_log.error("Failed to create project file:", p.filePath);
    }

    m_current = p;
    addRecent(p);

    m_log.info("Project created with folder:", name, "at:", folderPath);
    m_bus->publish("project:created", {{"id", p.id}, {"name", p.name}, {"path", p.filePath}});
    emit projectCreated(p);
    emit projectChanged(p);
    return p;
}

std::optional<Project> ProjectManager::open(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        m_log.error("Failed to open project file:", filePath);
        return std::nullopt;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    file.close();

    if (err.error != QJsonParseError::NoError) {
        m_log.error("JSON parse error in project file:", err.errorString());
        return std::nullopt;
    }

    Project p = Project::fromJson(doc.object());
    if (p.isNull()) {
        m_log.error("Invalid project file (missing id):", filePath);
        return std::nullopt;
    }

    p.filePath = filePath;
    if (p.basePath.isEmpty()) {
        p.basePath = QFileInfo(filePath).absolutePath();
    }

    m_current = p;
    addRecent(p);

    m_log.info("Project opened:", p.name, "from:", filePath);
    m_bus->publish("project:opened", {{"id", p.id}, {"name", p.name}, {"path", p.filePath}});
    emit projectOpened(p);
    emit projectChanged(p);
    return p;
}

bool ProjectManager::save() {
    if (!hasProject()) return false;
    if (m_current.filePath.isEmpty()) return false;

    updateTimestamp();

    // Ensure parent directory exists
    QDir().mkpath(QFileInfo(m_current.filePath).absolutePath());

    QFile file(m_current.filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        m_log.error("Failed to save project:", m_current.filePath);
        return false;
    }

    QJsonDocument doc(m_current.toJson());
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    m_current.dirty = false;
    m_log.info("Project saved:", m_current.name, "to:", m_current.filePath);
    m_bus->publish("project:saved", {{"id", m_current.id}, {"path", m_current.filePath}});
    emit projectSaved(m_current);
    emit projectChanged(m_current);
    return true;
}

bool ProjectManager::saveAs(const QString& filePath) {
    if (!hasProject()) return false;

    m_current.filePath = filePath;
    if (m_current.basePath.isEmpty()) {
        m_current.basePath = QFileInfo(filePath).absolutePath();
    }

    return save();
}

void ProjectManager::close() {
    if (!hasProject()) return;

    const QString id = m_current.id;
    const QString name = m_current.name;
    m_current = Project();

    m_log.info("Project closed:", name);
    m_bus->publish("project:closed", {{"id", id}});
    emit projectClosed();
    emit projectChanged(m_current);
}

void ProjectManager::markDirty() {
    if (!hasProject()) return;
    if (!m_current.dirty) {
        m_current.dirty = true;
        emit projectChanged(m_current);
    }
}

// --- Recent projects ---

void ProjectManager::addRecent(const Project& project) {
    if (project.filePath.isEmpty()) return;

    // Remove existing entry for this path
    m_recent.erase(
        std::remove_if(m_recent.begin(), m_recent.end(),
            [&](const RecentEntry& e) { return e.filePath == project.filePath; }),
        m_recent.end());

    RecentEntry entry;
    entry.id = project.id;
    entry.name = project.name;
    entry.filePath = project.filePath;
    entry.modifiedAt = project.modifiedAt;
    entry.pinned = false;

    m_recent.insert(m_recent.begin(), entry);

    // Keep max 20 unpinned entries (pinned entries are always kept)
    int unpinnedCount = 0;
    for (int i = m_recent.size() - 1; i >= 0; --i) {
        if (!m_recent[i].pinned) {
            unpinnedCount++;
            if (unpinnedCount > 20) {
                m_recent.erase(m_recent.begin() + i);
            }
        }
    }

    saveRecent();
    emit recentChanged();
}

void ProjectManager::removeRecent(const QString& filePath) {
    m_recent.erase(
        std::remove_if(m_recent.begin(), m_recent.end(),
            [&](const RecentEntry& e) { return e.filePath == filePath; }),
        m_recent.end());
    saveRecent();
    emit recentChanged();
}

void ProjectManager::clearRecent() {
    m_recent.clear();
    saveRecent();
    emit recentChanged();
}

void ProjectManager::togglePin(const QString& filePath) {
    for (auto& e : m_recent) {
        if (e.filePath == filePath) {
            e.pinned = !e.pinned;
            break;
        }
    }
    saveRecent();
    emit recentChanged();
}

// --- Autosave ---

void ProjectManager::setAutosaveInterval(int seconds) {
    if (seconds <= 0) {
        m_autosaveTimer.stop();
    } else {
        m_autosaveTimer.setInterval(seconds * 1000);
        m_autosaveTimer.start();
    }
}

void ProjectManager::onAutosave() {
    if (!hasProject() || !m_current.dirty) return;
    m_log.info("Autosaving project:", m_current.name);
    m_bus->publish("project:autosave-requested", {{"id", m_current.id}});
    save();
}

// --- Project folder helpers ---

QString ProjectManager::getSubfolder(const QString& name) const {
    if (!hasProject()) return {};
    return m_current.basePath + "/" + name;
}

QString ProjectManager::getExportPath() const {
    return getSubfolder("Exports");
}

// --- Private ---

void ProjectManager::loadRecent() {
    QFile file(m_recentFilePath);
    if (!file.open(QIODevice::ReadOnly)) return;

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    file.close();
    if (err.error != QJsonParseError::NoError) return;

    const QJsonArray arr = doc.array();
    m_recent.clear();
    for (const auto& v : arr) {
        const QJsonObject o = v.toObject();
        RecentEntry e;
        e.id = o["id"].toString();
        e.name = o["name"].toString();
        e.filePath = o["filePath"].toString();
        e.modifiedAt = o["modifiedAt"].toString();
        e.pinned = o["pinned"].toBool(false);
        if (!e.filePath.isEmpty()) m_recent.push_back(e);
    }
    m_log.info("Loaded", m_recent.size(), "recent projects");
}

void ProjectManager::saveRecent() {
    QJsonArray arr;
    for (const auto& e : m_recent) {
        arr.append(QJsonObject{
            {"id", e.id}, {"name", e.name},
            {"filePath", e.filePath}, {"modifiedAt", e.modifiedAt},
            {"pinned", e.pinned}
        });
    }
    QFile file(m_recentFilePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
        file.close();
    }
}

void ProjectManager::createFolderStructure(const QString& basePath) {
    QDir dir(basePath);
    if (!dir.exists()) {
        dir.mkpath(basePath);
    }
    for (const QString& sub : kSubfolders) {
        dir.mkpath(sub);
    }
    m_log.info("Created folder structure at:", basePath);
}

void ProjectManager::updateTimestamp() {
    m_current.modifiedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}
