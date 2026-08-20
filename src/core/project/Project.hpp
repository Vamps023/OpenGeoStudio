#pragma once

// ============================================================
// Project — Data model for an OpenGeoStudio project (.ogproj)
// ============================================================
//
// Mirrors the TypeScript Project interface from
// core/project/project-manager.ts and docs/ogproj-schema.json.
//
// The .ogproj file is JSON with this structure:
// {
//   "id": "uuid",
//   "name": "My Project",
//   "createdAt": "ISO-8601",
//   "modifiedAt": "ISO-8601",
//   "workspaceId": "road-studio",
//   "moduleState": { "road-studio": {...}, "terrain": {...} },
//   "dirty": false,
//   "filePath": "/path/to/project.ogproj",
//   "basePath": "/path/to/project_folder",
//   "bounds": { "minLat": ..., "minLon": ..., "maxLat": ..., "maxLon": ... }
// }
//

#include <QString>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonDocument>
#include <QUuid>
#include <optional>

struct ProjectBounds {
    double minLat = 0, minLon = 0, maxLat = 0, maxLon = 0;
    bool valid = false;

    QJsonObject toJson() const {
        if (!valid) return {};
        return QJsonObject{
            {"minLat", minLat}, {"minLon", minLon},
            {"maxLat", maxLat}, {"maxLon", maxLon}
        };
    }

    static std::optional<ProjectBounds> fromJson(const QJsonObject& j) {
        if (!j.contains("minLat")) return std::nullopt;
        ProjectBounds b;
        b.minLat = j["minLat"].toDouble();
        b.minLon = j["minLon"].toDouble();
        b.maxLat = j["maxLat"].toDouble();
        b.maxLon = j["maxLon"].toDouble();
        b.valid = true;
        return b;
    }
};

struct Project {
    static constexpr int SCHEMA_VERSION = 1;  // Increment on breaking schema changes

    int schemaVersion = SCHEMA_VERSION;  // .ogproj schema version for migration support
    QString id;
    QString name;
    QString createdAt;   // ISO-8601
    QString modifiedAt;  // ISO-8601
    QString workspaceId;
    QJsonObject moduleState;
    bool dirty = false;
    QString filePath;    // path to .ogproj file
    QString basePath;    // project folder path
    ProjectBounds bounds;

    bool isNull() const { return id.isEmpty(); }

    QJsonObject toJson() const {
        QJsonObject j;
        j["schemaVersion"] = schemaVersion;
        j["id"] = id;
        j["name"] = name;
        j["createdAt"] = createdAt;
        j["modifiedAt"] = modifiedAt;
        j["workspaceId"] = workspaceId;
        j["moduleState"] = moduleState;
        j["dirty"] = dirty;
        if (!filePath.isEmpty()) j["filePath"] = filePath;
        if (!basePath.isEmpty()) j["basePath"] = basePath;
        if (bounds.valid) j["bounds"] = bounds.toJson();
        return j;
    }

    static Project fromJson(const QJsonObject& j) {
        Project p;
        // schemaVersion: defaults to 0 if missing (legacy projects without
        // the field). Migration logic should check this value and upgrade
        // the project as needed.
        p.schemaVersion = j["schemaVersion"].toInt(0);
        p.id = j["id"].toString();
        p.name = j["name"].toString();
        p.createdAt = j["createdAt"].toString();
        p.modifiedAt = j["modifiedAt"].toString();
        p.workspaceId = j["workspaceId"].toString("home");
        p.moduleState = j["moduleState"].toObject();
        p.dirty = j["dirty"].toBool(false);
        p.filePath = j["filePath"].toString();
        p.basePath = j["basePath"].toString();
        if (auto b = ProjectBounds::fromJson(j["bounds"].toObject())) {
            p.bounds = *b;
        }
        // If the loaded schema is older than current, mark for migration.
        // Future migration functions will be called here:
        //   if (p.schemaVersion < SCHEMA_VERSION) p = migrate(p);
        // For now, just update the version so new saves include it.
        if (p.schemaVersion < SCHEMA_VERSION) {
            p.schemaVersion = SCHEMA_VERSION;
        }
        return p;
    }
};
