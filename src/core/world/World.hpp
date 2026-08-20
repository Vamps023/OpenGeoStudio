#pragma once

// ============================================================
// World — Container for all world data
// ============================================================
//
// Pure data model — no rendering.
// Contains actors, layers, splines, PCG graphs, terrain tiles,
// masks, materials, biomes, water, and settings.
//

#include "WorldTypes.hpp"
#include <QSet>
#include <QFile>
#include <QJsonDocument>
#include <QDir>
#include <algorithm>
#include <functional>

namespace world {

class World {
public:
    WorldSettings settings;
    QList<Actor> actors;
    QList<Layer> layers;
    QList<Spline> splines;
    QList<PCGGraph> pcgGraphs;
    QList<TerrainTile> terrainTiles;
    QList<TerrainMask> masks;
    QList<TerrainMaterial> materials;
    QList<Biome> biomes;
    QList<WaterBody> waterBodies;

    // Selection
    QSet<QString> selectedActorIds;

    World() {
        // Create default layer
        Layer defaultLayer;
        defaultLayer.id = "default";
        defaultLayer.name = "Default";
        defaultLayer.isDefault = true;
        layers.append(defaultLayer);

        // Create standard layers
        for (const char* rawName : {"Terrain", "Roads", "Buildings", "Vegetation",
                                     "Water", "Infrastructure", "Lighting", "PCG"}) {
            Layer l;
            QString name = QString::fromLatin1(rawName);
            l.id = name.toLower();
            l.name = name;
            layers.append(l);
        }
    }

    // ============================================================
    // Actor management
    // ============================================================

    Actor* addActor(const Actor& actor) {
        actors.append(actor);
        return &actors.last();
    }

    Actor* addActor(ActorType type, const QString& name, const QString& layerId = "default") {
        Actor a;
        a.type = type;
        a.name = name;
        a.layerId = layerId;
        actors.append(a);
        return &actors.last();
    }

    bool removeActor(const QString& id) {
        // Also remove children
        QList<QString> toRemove;
        collectChildren(id, toRemove);
        toRemove.append(id);

        for (const auto& rid : toRemove) {
            selectedActorIds.remove(rid);
            actors.erase(std::remove_if(actors.begin(), actors.end(),
                [&](const Actor& a) { return a.id == rid; }), actors.end());
        }
        return true;
    }

    Actor* findActor(const QString& id) {
        for (auto& a : actors)
            if (a.id == id) return &a;
        return nullptr;
    }

    const Actor* findActor(const QString& id) const {
        for (const auto& a : actors)
            if (a.id == id) return &a;
        return nullptr;
    }

    QList<Actor*> children(const QString& parentId) {
        QList<Actor*> result;
        for (auto& a : actors)
            if (a.parentId == parentId) result.append(&a);
        return result;
    }

    QList<const Actor*> children(const QString& parentId) const {
        QList<const Actor*> result;
        for (const auto& a : actors)
            if (a.parentId == parentId) result.append(&a);
        return result;
    }

    QList<Actor*> actorsByType(ActorType type) {
        QList<Actor*> result;
        for (auto& a : actors)
            if (a.type == type) result.append(&a);
        return result;
    }

    QList<Actor*> actorsByLayer(const QString& layerId) {
        QList<Actor*> result;
        for (auto& a : actors)
            if (a.layerId == layerId) result.append(&a);
        return result;
    }

    QList<Actor*> rootActors() {
        QList<Actor*> result;
        for (auto& a : actors)
            if (a.parentId.isEmpty()) result.append(&a);
        return result;
    }

    void setParent(const QString& actorId, const QString& newParentId) {
        // Prevent cycles
        if (wouldCreateCycle(actorId, newParentId)) return;
        Actor* a = findActor(actorId);
        if (a) {
            a->parentId = newParentId;
            a->touch();
        }
    }

    // ============================================================
    // Selection
    // ============================================================

    void select(const QString& id) {
        Actor* a = findActor(id);
        if (a && a->selectable && !isLayerLocked(a->layerId))
            selectedActorIds.insert(id);
    }

    void selectOnly(const QString& id) {
        selectedActorIds.clear();
        select(id);
    }

    void addToSelection(const QString& id) { select(id); }
    void removeFromSelection(const QString& id) { selectedActorIds.remove(id); }
    void clearSelection() { selectedActorIds.clear(); }
    void selectAll() {
        selectedActorIds.clear();
        for (const auto& a : actors)
            if (a.selectable && !isLayerLocked(a.layerId) && isLayerVisible(a.layerId))
                selectedActorIds.insert(a.id);
    }

    bool isSelected(const QString& id) const { return selectedActorIds.contains(id); }
    int selectionCount() const { return selectedActorIds.size(); }
    QList<Actor*> selection() {
        QList<Actor*> result;
        for (const auto& id : selectedActorIds) {
            Actor* a = findActor(id);
            if (a) result.append(a);
        }
        return result;
    }

    QString primarySelection() const {
        if (selectedActorIds.isEmpty()) return QString();
        return *selectedActorIds.begin();
    }

    // ============================================================
    // Layer management
    // ============================================================

    Layer* addLayer(const QString& name) {
        Layer l;
        l.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        l.name = name;
        layers.append(l);
        return &layers.last();
    }

    bool removeLayer(const QString& id) {
        // Can't remove default layer
        for (const auto& l : layers)
            if (l.id == id && l.isDefault) return false;

        // Move actors to default layer
        for (auto& a : actors)
            if (a.layerId == id) a.layerId = "default";

        layers.erase(std::remove_if(layers.begin(), layers.end(),
            [&](const Layer& l) { return l.id == id; }), layers.end());
        return true;
    }

    Layer* findLayer(const QString& id) {
        for (auto& l : layers)
            if (l.id == id) return &l;
        return nullptr;
    }

    bool isLayerVisible(const QString& layerId) const {
        for (const auto& l : layers)
            if (l.id == layerId) return l.visible;
        return true;  // unknown layer = visible
    }

    bool isLayerLocked(const QString& layerId) const {
        for (const auto& l : layers)
            if (l.id == layerId) return l.locked;
        return false;
    }

    bool isLayerSelectable(const QString& layerId) const {
        for (const auto& l : layers)
            if (l.id == layerId) return l.selectable;
        return true;
    }

    void setLayerVisible(const QString& id, bool visible) {
        Layer* l = findLayer(id);
        if (l) l->visible = visible;
    }

    void setLayerLocked(const QString& id, bool locked) {
        Layer* l = findLayer(id);
        if (l) l->locked = locked;
    }

    // ============================================================
    // Spline management
    // ============================================================

    Spline* addSpline(SplineType type, const QString& name) {
        Spline s;
        s.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        s.name = name;
        s.type = type;
        splines.append(s);
        return &splines.last();
    }

    Spline* findSpline(const QString& id) {
        for (auto& s : splines)
            if (s.id == id) return &s;
        return nullptr;
    }

    bool removeSpline(const QString& id) {
        splines.erase(std::remove_if(splines.begin(), splines.end(),
            [&](const Spline& s) { return s.id == id; }), splines.end());
        return true;
    }

    // ============================================================
    // PCG graph management
    // ============================================================

    PCGGraph* addPCGGraph(const QString& name) {
        PCGGraph g;
        g.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        g.name = name;
        pcgGraphs.append(g);
        return &pcgGraphs.last();
    }

    PCGGraph* findPCGGraph(const QString& idOrName) {
        for (auto& g : pcgGraphs)
            if (g.id == idOrName || g.name == idOrName) return &g;
        return nullptr;
    }

    bool removePCGGraph(const QString& id) {
        pcgGraphs.erase(std::remove_if(pcgGraphs.begin(), pcgGraphs.end(),
            [&](const PCGGraph& g) { return g.id == id; }), pcgGraphs.end());
        return true;
    }

    // ============================================================
    // Terrain tile management
    // ============================================================

    TerrainTile* addTerrainTile(int row, int col) {
        TerrainTile t;
        t.id = QString("tile_%1_%2").arg(row).arg(col);
        t.row = row; t.col = col;
        terrainTiles.append(t);
        return &terrainTiles.last();
    }

    TerrainTile* findTerrainTile(int row, int col) {
        for (auto& t : terrainTiles)
            if (t.row == row && t.col == col) return &t;
        return nullptr;
    }

    // ============================================================
    // Mask management
    // ============================================================

    TerrainMask* addMask(const QString& name, MaskType type) {
        TerrainMask m;
        m.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m.name = name;
        m.type = type;
        masks.append(m);
        return &masks.last();
    }

    TerrainMask* findMask(const QString& id) {
        for (auto& m : masks)
            if (m.id == id) return &m;
        return nullptr;
    }

    // ============================================================
    // Water management
    // ============================================================

    WaterBody* addWater(const QString& name, WaterType type) {
        WaterBody w;
        w.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        w.name = name;
        w.type = type;
        waterBodies.append(w);
        return &waterBodies.last();
    }

    WaterBody* findWater(const QString& id) {
        for (auto& w : waterBodies)
            if (w.id == id) return &w;
        return nullptr;
    }

    // ============================================================
    // Validation
    // ============================================================

    struct ValidationError {
        QString severity;  // "critical", "high", "medium", "low"
        QString category;  // "world", "terrain", "road", "pcg", "asset"
        QString message;
        QString actorId;
        QString suggestedFix;
    };

    QList<ValidationError> validate() const {
        QList<ValidationError> errors;

        // Check for duplicate IDs
        QSet<QString> seenIds;
        for (const auto& a : actors) {
            if (seenIds.contains(a.id)) {
                errors.append({"high", "world", "Duplicate actor ID: " + a.id, a.id,
                               "Regenerate unique ID for this actor"});
            }
            seenIds.insert(a.id);
        }

        // Check for broken parent references
        for (const auto& a : actors) {
            if (!a.parentId.isEmpty() && !seenIds.contains(a.parentId)) {
                errors.append({"medium", "world", "Broken parent reference: " + a.parentId,
                               a.id, "Clear parent or fix reference"});
            }
        }

        // Check for broken layer references
        QSet<QString> layerIds;
        for (const auto& l : layers) layerIds.insert(l.id);
        for (const auto& a : actors) {
            if (!layerIds.contains(a.layerId)) {
                errors.append({"low", "world", "Invalid layer reference: " + a.layerId,
                               a.id, "Assign to valid layer"});
            }
        }

        // Check PCG graphs for cycles
        for (const auto& g : pcgGraphs) {
            if (g.hasCycles()) {
                errors.append({"high", "pcg", "Cyclic graph: " + g.name,
                               g.id, "Remove cycle in graph"});
            }
        }

        // Check PCG graphs for broken connections
        for (const auto& g : pcgGraphs) {
            QSet<QString> nodeIds;
            for (const auto& n : g.nodes) nodeIds.insert(n.id);
            for (const auto& n : g.nodes) {
                for (const auto& in : n.inputNodeIds) {
                    if (!nodeIds.contains(in)) {
                        errors.append({"medium", "pcg",
                            "Broken connection in " + g.name + ": " + in,
                            g.id, "Remove broken connection"});
                    }
                }
            }
        }

        // Check for missing assets
        for (const auto& a : actors) {
            if (!a.assetPath.isEmpty() && !QFile::exists(a.assetPath)) {
                errors.append({"medium", "asset",
                    "Missing asset: " + a.assetPath + " for actor " + a.name,
                    a.id, "Re-import asset or update reference"});
            }
        }

        // Check terrain tiles
        for (const auto& t : terrainTiles) {
            if (!t.heightmapPath.isEmpty() && !QFile::exists(t.heightmapPath)) {
                errors.append({"high", "terrain",
                    "Missing heightmap for tile " + t.id,
                    t.id, "Re-generate terrain tile"});
            }
        }

        return errors;
    }

    // ============================================================
    // Serialization
    // ============================================================

    QJsonObject toJson() const {
        QJsonObject j;
        j["version"] = "2.0";
        j["settings"] = settings.toJson();

        QJsonArray actorsArr;
        for (const auto& a : actors) actorsArr.append(a.toJson());
        j["actors"] = actorsArr;

        QJsonArray layersArr;
        for (const auto& l : layers) layersArr.append(l.toJson());
        j["layers"] = layersArr;

        QJsonArray splinesArr;
        for (const auto& s : splines) splinesArr.append(s.toJson());
        j["splines"] = splinesArr;

        QJsonArray pcgArr;
        for (const auto& g : pcgGraphs) pcgArr.append(g.toJson());
        j["pcgGraphs"] = pcgArr;

        QJsonArray tilesArr;
        for (const auto& t : terrainTiles) tilesArr.append(t.toJson());
        j["terrainTiles"] = tilesArr;

        QJsonArray masksArr;
        for (const auto& m : masks) masksArr.append(m.toJson());
        j["masks"] = masksArr;

        QJsonArray matArr;
        for (const auto& m : materials) matArr.append(m.toJson());
        j["materials"] = matArr;

        QJsonArray biomeArr;
        for (const auto& b : biomes) biomeArr.append(b.toJson());
        j["biomes"] = biomeArr;

        QJsonArray waterArr;
        for (const auto& w : waterBodies) waterArr.append(w.toJson());
        j["waterBodies"] = waterArr;

        QJsonArray selArr;
        for (const auto& id : selectedActorIds) selArr.append(id);
        j["selection"] = selArr;

        return j;
    }

    static World fromJson(const QJsonObject& j) {
        World w;
        w.settings = WorldSettings::fromJson(j["settings"].toObject());

        w.actors.clear();
        QJsonArray actorsArr = j["actors"].toArray();
        for (const auto& v : actorsArr)
            w.actors.append(Actor::fromJson(v.toObject()));

        w.layers.clear();
        QJsonArray layersArr = j["layers"].toArray();
        for (const auto& v : layersArr)
            w.layers.append(Layer::fromJson(v.toObject()));

        w.splines.clear();
        QJsonArray splinesArr = j["splines"].toArray();
        for (const auto& v : splinesArr)
            w.splines.append(Spline::fromJson(v.toObject()));

        w.pcgGraphs.clear();
        QJsonArray pcgArr = j["pcgGraphs"].toArray();
        for (const auto& v : pcgArr)
            w.pcgGraphs.append(PCGGraph::fromJson(v.toObject()));

        w.terrainTiles.clear();
        QJsonArray tilesArr = j["terrainTiles"].toArray();
        for (const auto& v : tilesArr)
            w.terrainTiles.append(TerrainTile::fromJson(v.toObject()));

        w.masks.clear();
        QJsonArray masksArr = j["masks"].toArray();
        for (const auto& v : masksArr)
            w.masks.append(TerrainMask::fromJson(v.toObject()));

        w.materials.clear();
        QJsonArray matArr = j["materials"].toArray();
        for (const auto& v : matArr)
            w.materials.append(TerrainMaterial::fromJson(v.toObject()));

        w.biomes.clear();
        QJsonArray biomeArr = j["biomes"].toArray();
        for (const auto& v : biomeArr)
            w.biomes.append(Biome::fromJson(v.toObject()));

        w.waterBodies.clear();
        QJsonArray waterArr = j["waterBodies"].toArray();
        for (const auto& v : waterArr)
            w.waterBodies.append(WaterBody::fromJson(v.toObject()));

        w.selectedActorIds.clear();
        QJsonArray selArr = j["selection"].toArray();
        for (const auto& v : selArr)
            w.selectedActorIds.insert(v.toString());

        return w;
    }

    bool saveToFile(const QString& path) const {
        QDir().mkpath(QFileInfo(path).absolutePath());
        // Transactional save: write to temp file, then atomically rename.
        QString tempPath = path + ".tmp";
        QFile f(tempPath);
        if (!f.open(QIODevice::WriteOnly)) return false;
        QJsonDocument doc(toJson());
        qint64 written = f.write(doc.toJson(QJsonDocument::Indented));
        f.flush();
        f.close();
        if (written < 0) {
            QFile::remove(tempPath);
            return false;
        }
        if (QFile::exists(path)) QFile::remove(path);
        if (!QFile::rename(tempPath, path)) {
            QFile::remove(tempPath);
            return false;
        }
        return true;
    }

    static World loadFromFile(const QString& path) {
        World w;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return w;
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        f.close();
        if (doc.isObject()) w = fromJson(doc.object());
        return w;
    }

    // ============================================================
    // Statistics
    // ============================================================

    int actorCount() const { return actors.size(); }
    int actorCountByType(ActorType type) const {
        int count = 0;
        for (const auto& a : actors)
            if (a.type == type) count++;
        return count;
    }
    int splineCount() const { return splines.size(); }
    int pcgGraphCount() const { return pcgGraphs.size(); }
    int tileCount() const { return terrainTiles.size(); }
    int maskCount() const { return masks.size(); }
    int waterCount() const { return waterBodies.size(); }
    int layerCount() const { return layers.size(); }

private:
    void collectChildren(const QString& parentId, QList<QString>& result) const {
        for (const auto& a : actors) {
            if (a.parentId == parentId) {
                result.append(a.id);
                collectChildren(a.id, result);
            }
        }
    }

    bool wouldCreateCycle(const QString& actorId, const QString& newParentId) const {
        if (actorId == newParentId) return true;
        QString current = newParentId;
        while (!current.isEmpty()) {
            const Actor* a = findActor(current);
            if (!a) return false;
            if (a->parentId == actorId) return true;
            current = a->parentId;
        }
        return false;
    }
};

} // namespace world
