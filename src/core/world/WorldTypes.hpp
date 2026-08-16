#pragma once

// ============================================================
// WorldTypes — Core data types for the World Authoring System
// ============================================================
//
// Pure data model — no rendering dependencies.
// All scene objects, transforms, layers, splines, PCG data
// are defined here so they can be tested without OGRE.
//

#include <QString>
#include <QList>
#include <QMap>
#include <QJsonObject>
#include <QJsonArray>
#include <QUuid>
#include <QDateTime>
#include <cmath>
#include <vector>

namespace world {

// ============================================================
// Transform — 3D position, rotation, scale
// ============================================================

struct Transform {
    float posX = 0, posY = 0, posZ = 0;
    float rotX = 0, rotY = 0, rotZ = 0;  // Euler degrees
    float scaleX = 1, scaleY = 1, scaleZ = 1;

    QJsonObject toJson() const {
        QJsonObject j;
        j["posX"] = posX; j["posY"] = posY; j["posZ"] = posZ;
        j["rotX"] = rotX; j["rotY"] = rotY; j["rotZ"] = rotZ;
        j["scaleX"] = scaleX; j["scaleY"] = scaleY; j["scaleZ"] = scaleZ;
        return j;
    }

    static Transform fromJson(const QJsonObject& j) {
        Transform t;
        t.posX = float(j["posX"].toDouble(0));
        t.posY = float(j["posY"].toDouble(0));
        t.posZ = float(j["posZ"].toDouble(0));
        t.rotX = float(j["rotX"].toDouble(0));
        t.rotY = float(j["rotY"].toDouble(0));
        t.rotZ = float(j["rotZ"].toDouble(0));
        t.scaleX = float(j["scaleX"].toDouble(1));
        t.scaleY = float(j["scaleY"].toDouble(1));
        t.scaleZ = float(j["scaleZ"].toDouble(1));
        return t;
    }

    bool operator==(const Transform& o) const {
        return posX == o.posX && posY == o.posY && posZ == o.posZ &&
               rotX == o.rotX && rotY == o.rotY && rotZ == o.rotZ &&
               scaleX == o.scaleX && scaleY == o.scaleY && scaleZ == o.scaleZ;
    }
};

// ============================================================
// ActorType — Enumeration of all actor types
// ============================================================

enum class ActorType {
    Unknown,
    Terrain,
    TerrainTile,
    Road,
    RoadSegment,
    Building,
    Tree,
    Vegetation,
    Grass,
    Rock,
    Water,
    River,
    Lake,
    Prop,
    Light,
    SunLight,
    SkyLight,
    Camera,
    Spline,
    SplineControlPoint,
    PCGGraph,
    PCGNode,
    Volume,
    Group,
    Empty,        // organizational node
    Custom
};

inline QString actorTypeToString(ActorType type) {
    switch (type) {
    case ActorType::Terrain: return "Terrain";
    case ActorType::TerrainTile: return "TerrainTile";
    case ActorType::Road: return "Road";
    case ActorType::RoadSegment: return "RoadSegment";
    case ActorType::Building: return "Building";
    case ActorType::Tree: return "Tree";
    case ActorType::Vegetation: return "Vegetation";
    case ActorType::Grass: return "Grass";
    case ActorType::Rock: return "Rock";
    case ActorType::Water: return "Water";
    case ActorType::River: return "River";
    case ActorType::Lake: return "Lake";
    case ActorType::Prop: return "Prop";
    case ActorType::Light: return "Light";
    case ActorType::SunLight: return "SunLight";
    case ActorType::SkyLight: return "SkyLight";
    case ActorType::Camera: return "Camera";
    case ActorType::Spline: return "Spline";
    case ActorType::SplineControlPoint: return "SplineControlPoint";
    case ActorType::PCGGraph: return "PCGGraph";
    case ActorType::PCGNode: return "PCGNode";
    case ActorType::Volume: return "Volume";
    case ActorType::Group: return "Group";
    case ActorType::Empty: return "Empty";
    case ActorType::Custom: return "Custom";
    default: return "Unknown";
    }
}

inline ActorType stringToActorType(const QString& s) {
    if (s == "Terrain") return ActorType::Terrain;
    if (s == "TerrainTile") return ActorType::TerrainTile;
    if (s == "Road") return ActorType::Road;
    if (s == "RoadSegment") return ActorType::RoadSegment;
    if (s == "Building") return ActorType::Building;
    if (s == "Tree") return ActorType::Tree;
    if (s == "Vegetation") return ActorType::Vegetation;
    if (s == "Grass") return ActorType::Grass;
    if (s == "Rock") return ActorType::Rock;
    if (s == "Water") return ActorType::Water;
    if (s == "River") return ActorType::River;
    if (s == "Lake") return ActorType::Lake;
    if (s == "Prop") return ActorType::Prop;
    if (s == "Light") return ActorType::Light;
    if (s == "SunLight") return ActorType::SunLight;
    if (s == "SkyLight") return ActorType::SkyLight;
    if (s == "Camera") return ActorType::Camera;
    if (s == "Spline") return ActorType::Spline;
    if (s == "SplineControlPoint") return ActorType::SplineControlPoint;
    if (s == "PCGGraph") return ActorType::PCGGraph;
    if (s == "PCGNode") return ActorType::PCGNode;
    if (s == "Volume") return ActorType::Volume;
    if (s == "Group") return ActorType::Group;
    if (s == "Empty") return ActorType::Empty;
    if (s == "Custom") return ActorType::Custom;
    return ActorType::Unknown;
}

// ============================================================
// Actor — Any object in the world
// ============================================================

struct Actor {
    QString id;              // unique identifier (UUID)
    QString name;            // display name
    ActorType type = ActorType::Unknown;
    Transform transform;

    QString parentId;        // parent actor ID (empty = root)
    QString layerId = "default";

    bool visible = true;
    bool locked = false;
    bool selectable = true;

    // Asset reference (mesh, material, texture, etc.)
    QString assetPath;       // path to mesh/asset file
    QString materialPath;    // path to material override

    // Color (for procedural objects without mesh)
    float colorR = 0.8f, colorG = 0.8f, colorB = 0.8f, colorA = 1.0f;

    // Metadata — arbitrary key-value pairs
    QMap<QString, QString> metadata;

    // Procedural source (if generated by PCG)
    QString pcgGraphId;      // which PCG graph generated this
    QString pcgNodeId;       // which node in the graph
    int seed = 0;            // deterministic seed

    // Type-specific data (stored as JSON for flexibility)
    QJsonObject typeData;

    // Timestamps
    QString createdAt;
    QString modifiedAt;

    Actor() {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        createdAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        modifiedAt = createdAt;
    }

    QJsonObject toJson() const {
        QJsonObject j;
        j["id"] = id;
        j["name"] = name;
        j["type"] = actorTypeToString(type);
        j["transform"] = transform.toJson();
        j["parentId"] = parentId;
        j["layerId"] = layerId;
        j["visible"] = visible;
        j["locked"] = locked;
        j["selectable"] = selectable;
        j["assetPath"] = assetPath;
        j["materialPath"] = materialPath;
        j["colorR"] = colorR; j["colorG"] = colorG;
        j["colorB"] = colorB; j["colorA"] = colorA;

        QJsonObject meta;
        for (auto it = metadata.begin(); it != metadata.end(); ++it)
            meta[it.key()] = it.value();
        j["metadata"] = meta;

        j["pcgGraphId"] = pcgGraphId;
        j["pcgNodeId"] = pcgNodeId;
        j["seed"] = seed;
        j["typeData"] = typeData;
        j["createdAt"] = createdAt;
        j["modifiedAt"] = modifiedAt;
        return j;
    }

    static Actor fromJson(const QJsonObject& j) {
        Actor a;
        a.id = j["id"].toString();
        a.name = j["name"].toString();
        a.type = stringToActorType(j["type"].toString());
        a.transform = Transform::fromJson(j["transform"].toObject());
        a.parentId = j["parentId"].toString();
        a.layerId = j["layerId"].toString("default");
        a.visible = j["visible"].toBool(true);
        a.locked = j["locked"].toBool(false);
        a.selectable = j["selectable"].toBool(true);
        a.assetPath = j["assetPath"].toString();
        a.materialPath = j["materialPath"].toString();
        a.colorR = float(j["colorR"].toDouble(0.8));
        a.colorG = float(j["colorG"].toDouble(0.8));
        a.colorB = float(j["colorB"].toDouble(0.8));
        a.colorA = float(j["colorA"].toDouble(1.0));

        QJsonObject meta = j["metadata"].toObject();
        for (auto it = meta.begin(); it != meta.end(); ++it)
            a.metadata[it.key()] = it.value().toString();

        a.pcgGraphId = j["pcgGraphId"].toString();
        a.pcgNodeId = j["pcgNodeId"].toString();
        a.seed = j["seed"].toInt(0);
        a.typeData = j["typeData"].toObject();
        a.createdAt = j["createdAt"].toString();
        a.modifiedAt = j["modifiedAt"].toString();
        return a;
    }

    void touch() {
        modifiedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    }
};

// ============================================================
// Layer — Organizational layer
// ============================================================

struct Layer {
    QString id;
    QString name;
    bool visible = true;
    bool locked = false;
    bool selectable = true;
    int colorR = 100, colorG = 150, colorB = 255;
    bool isDefault = false;

    QJsonObject toJson() const {
        QJsonObject j;
        j["id"] = id;
        j["name"] = name;
        j["visible"] = visible;
        j["locked"] = locked;
        j["selectable"] = selectable;
        j["colorR"] = colorR; j["colorG"] = colorG; j["colorB"] = colorB;
        j["isDefault"] = isDefault;
        return j;
    }

    static Layer fromJson(const QJsonObject& j) {
        Layer l;
        l.id = j["id"].toString();
        l.name = j["name"].toString();
        l.visible = j["visible"].toBool(true);
        l.locked = j["locked"].toBool(false);
        l.selectable = j["selectable"].toBool(true);
        l.colorR = j["colorR"].toInt(100);
        l.colorG = j["colorG"].toInt(150);
        l.colorB = j["colorB"].toInt(255);
        l.isDefault = j["isDefault"].toBool(false);
        return l;
    }
};

// ============================================================
// SplineControlPoint — A point on a spline
// ============================================================

struct SplineControlPoint {
    float x = 0, y = 0, z = 0;
    float tangentInX = 0, tangentInY = 0, tangentInZ = 0;
    float tangentOutX = 0, tangentOutY = 0, tangentOutZ = 0;
    float roll = 0;  // banking angle

    QJsonObject toJson() const {
        QJsonObject j;
        j["x"] = x; j["y"] = y; j["z"] = z;
        j["tInX"] = tangentInX; j["tInY"] = tangentInY; j["tInZ"] = tangentInZ;
        j["tOutX"] = tangentOutX; j["tOutY"] = tangentOutY; j["tOutZ"] = tangentOutZ;
        j["roll"] = roll;
        return j;
    }

    static SplineControlPoint fromJson(const QJsonObject& j) {
        SplineControlPoint p;
        p.x = float(j["x"].toDouble(0)); p.y = float(j["y"].toDouble(0)); p.z = float(j["z"].toDouble(0));
        p.tangentInX = float(j["tInX"].toDouble(0));
        p.tangentInY = float(j["tInY"].toDouble(0));
        p.tangentInZ = float(j["tInZ"].toDouble(0));
        p.tangentOutX = float(j["tOutX"].toDouble(0));
        p.tangentOutY = float(j["tOutY"].toDouble(0));
        p.tangentOutZ = float(j["tOutZ"].toDouble(0));
        p.roll = float(j["roll"].toDouble(0));
        return p;
    }
};

// ============================================================
// Spline — Reusable spline for roads, rivers, fences, etc.
// ============================================================

enum class SplineType {
    Road,
    Railway,
    River,
    Fence,
    Pipeline,
    Powerline,
    CameraPath,
    Generic
};

struct Spline {
    QString id;
    QString name;
    SplineType type = SplineType::Generic;
    QList<SplineControlPoint> points;
    float width = 8.0f;
    float heightOffset = 0.5f;
    bool projectToTerrain = true;
    int materialId = 0;

    // Road-specific
    int laneCount = 2;
    float laneWidth = 3.5f;
    float shoulderWidth = 1.5f;
    bool hasMedian = false;
    float medianWidth = 2.0f;
    bool hasSidewalk = false;
    float sidewalkWidth = 2.0f;
    float banking = 0.0f;
    float crossfall = 0.0f;
    QString roadType = "highway";

    QJsonObject toJson() const;
    static Spline fromJson(const QJsonObject& j);
};

// ============================================================
// PCGNode — A node in a procedural generation graph
// ============================================================

enum class PCGNodeType {
    // Input
    WorldInput,
    TerrainInput,
    SplineInput,
    PolygonInput,
    MaskInput,
    ActorInput,
    PointInput,

    // Sampling
    TerrainHeight,
    SlopeSample,
    NormalSample,
    CurvatureSample,
    DistanceSample,
    MaskSample,

    // Generation
    ScatterPoints,
    GridPoints,
    SplineSampling,
    SurfaceSampling,
    VolumeSampling,

    // Filtering
    DensityFilter,
    HeightFilter,
    SlopeFilter,
    MaskFilter,
    DistanceFilter,
    AttributeFilter,
    RandomFilter,

    // Transform
    RandomRotation,
    RandomScale,
    AlignToSurface,
    OffsetTransform,
    TransformPoints,

    // Output
    StaticMeshOutput,
    ActorOutput,
    InstanceOutput,
    SplineOutput,
    CollectionOutput,
    TerrainMaskOutput
};

struct PCGNode {
    QString id;
    QString name;
    PCGNodeType type = PCGNodeType::ScatterPoints;
    QList<QString> inputNodeIds;  // ordered input connections
    QMap<QString, QString> properties;  // node-specific parameters
    float posX = 0, posY = 0;  // position in graph editor
    bool enabled = true;

    PCGNode() {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    QJsonObject toJson() const {
        QJsonObject j;
        j["id"] = id;
        j["name"] = name;
        j["type"] = static_cast<int>(type);
        QJsonArray inputs;
        for (const auto& id : inputNodeIds) inputs.append(id);
        j["inputs"] = inputs;
        QJsonObject props;
        for (auto it = properties.begin(); it != properties.end(); ++it)
            props[it.key()] = it.value();
        j["properties"] = props;
        j["posX"] = posX; j["posY"] = posY;
        j["enabled"] = enabled;
        return j;
    }

    static PCGNode fromJson(const QJsonObject& j) {
        PCGNode n;
        n.id = j["id"].toString();
        n.name = j["name"].toString();
        n.type = static_cast<PCGNodeType>(j["type"].toInt(0));
        QJsonArray inputs = j["inputs"].toArray();
        for (const auto& v : inputs) n.inputNodeIds.append(v.toString());
        QJsonObject props = j["properties"].toObject();
        for (auto it = props.begin(); it != props.end(); ++it)
            n.properties[it.key()] = it.value().toString();
        n.posX = float(j["posX"].toDouble(0));
        n.posY = float(j["posY"].toDouble(0));
        n.enabled = j["enabled"].toBool(true);
        return n;
    }
};

// ============================================================
// PCGPoint — A generated point with attributes
// ============================================================

struct PCGPoint {
    float x = 0, y = 0, z = 0;
    float rotX = 0, rotY = 0, rotZ = 0;
    float scaleX = 1, scaleY = 1, scaleZ = 1;
    float density = 1.0f;
    QMap<QString, float> attributes;

    QJsonObject toJson() const {
        QJsonObject j;
        j["x"] = x; j["y"] = y; j["z"] = z;
        j["rotX"] = rotX; j["rotY"] = rotY; j["rotZ"] = rotZ;
        j["scaleX"] = scaleX; j["scaleY"] = scaleY; j["scaleZ"] = scaleZ;
        j["density"] = density;
        QJsonObject attrs;
        for (auto it = attributes.begin(); it != attributes.end(); ++it)
            attrs[it.key()] = it.value();
        j["attributes"] = attrs;
        return j;
    }

    static PCGPoint fromJson(const QJsonObject& j) {
        PCGPoint p;
        p.x = float(j["x"].toDouble(0)); p.y = float(j["y"].toDouble(0)); p.z = float(j["z"].toDouble(0));
        p.rotX = float(j["rotX"].toDouble(0)); p.rotY = float(j["rotY"].toDouble(0)); p.rotZ = float(j["rotZ"].toDouble(0));
        p.scaleX = float(j["scaleX"].toDouble(1)); p.scaleY = float(j["scaleY"].toDouble(1)); p.scaleZ = float(j["scaleZ"].toDouble(1));
        p.density = float(j["density"].toDouble(1));
        QJsonObject attrs = j["attributes"].toObject();
        for (auto it = attrs.begin(); it != attrs.end(); ++it)
            p.attributes[it.key()] = float(it.value().toDouble());
        return p;
    }
};

// ============================================================
// PCGGraph — A procedural generation graph
// ============================================================

struct PCGGraph {
    QString id;
    QString name;
    QList<PCGNode> nodes;
    QList<PCGPoint> cachedPoints;  // last generated points
    int seed = 42;
    bool dirty = true;
    QString targetMeshPath;  // mesh to instance

    QJsonObject toJson() const {
        QJsonObject j;
        j["id"] = id;
        j["name"] = name;
        j["seed"] = seed;
        j["dirty"] = dirty;
        j["targetMeshPath"] = targetMeshPath;
        QJsonArray nodesArr;
        for (const auto& n : nodes) nodesArr.append(n.toJson());
        j["nodes"] = nodesArr;
        // Don't serialize cached points — they can be regenerated
        return j;
    }

    static PCGGraph fromJson(const QJsonObject& j) {
        PCGGraph g;
        g.id = j["id"].toString();
        g.name = j["name"].toString();
        g.seed = j["seed"].toInt(42);
        g.dirty = j["dirty"].toBool(true);
        g.targetMeshPath = j["targetMeshPath"].toString();
        QJsonArray nodesArr = j["nodes"].toArray();
        for (const auto& v : nodesArr)
            g.nodes.append(PCGNode::fromJson(v.toObject()));
        return g;
    }

    PCGNode* findNode(const QString& id) {
        for (auto& n : nodes)
            if (n.id == id) return &n;
        return nullptr;
    }

    bool hasCycles() const {
        // Simple cycle detection using DFS
        QSet<QString> visited, recStack;
        std::function<bool(const QString&)> dfs = [&](const QString& id) -> bool {
            if (recStack.contains(id)) return true;
            if (visited.contains(id)) return false;
            visited.insert(id);
            recStack.insert(id);
            for (const auto& n : nodes) {
                if (n.id == id) {
                    for (const auto& in : n.inputNodeIds) {
                        if (dfs(in)) return true;
                    }
                }
            }
            recStack.remove(id);
            return false;
        };
        for (const auto& n : nodes)
            if (dfs(n.id)) return true;
        return false;
    }
};

// ============================================================
// WorldSettings — Global world configuration
// ============================================================

struct WorldSettings {
    QString name = "Untitled World";
    QString description;

    // CRS / GIS
    int crsEpsg = 4326;
    double originLat = 0, originLon = 0;
    double originX = 0, originY = 0, originZ = 0;  // world origin offset

    // Terrain
    float terrainSize = 4000.0f;  // meters
    float heightScale = 100.0f;
    QString heightmapPath;
    QString albedoPath;

    // Lighting / Environment
    float sunYaw = 45.0f;
    float sunPitch = 60.0f;
    float sunIntensity = 3.0f;
    float skyColorR = 0.4f, skyColorG = 0.6f, skyColorB = 0.9f;
    float fogDensity = 0.0f;
    float fogColorR = 0.5f, fogColorG = 0.5f, fogColorB = 0.5f;
    float exposure = 1.0f;
    float timeOfDay = 12.0f;  // 0-24 hours

    // Grid
    float gridSize = 100.0f;
    int gridDivisions = 40;
    bool gridVisible = true;

    // Snap
    bool snapEnabled = false;
    float snapSize = 1.0f;
    bool snapToSurface = false;

    QJsonObject toJson() const {
        QJsonObject j;
        j["name"] = name;
        j["description"] = description;
        j["crsEpsg"] = crsEpsg;
        j["originLat"] = originLat; j["originLon"] = originLon;
        j["originX"] = originX; j["originY"] = originY; j["originZ"] = originZ;
        j["terrainSize"] = terrainSize;
        j["heightScale"] = heightScale;
        j["heightmapPath"] = heightmapPath;
        j["albedoPath"] = albedoPath;
        j["sunYaw"] = sunYaw; j["sunPitch"] = sunPitch; j["sunIntensity"] = sunIntensity;
        j["skyR"] = skyColorR; j["skyG"] = skyColorG; j["skyB"] = skyColorB;
        j["fogDensity"] = fogDensity;
        j["fogR"] = fogColorR; j["fogG"] = fogColorG; j["fogB"] = fogColorB;
        j["exposure"] = exposure;
        j["timeOfDay"] = timeOfDay;
        j["gridSize"] = gridSize; j["gridDivisions"] = gridDivisions;
        j["gridVisible"] = gridVisible;
        j["snapEnabled"] = snapEnabled; j["snapSize"] = snapSize;
        j["snapToSurface"] = snapToSurface;
        return j;
    }

    static WorldSettings fromJson(const QJsonObject& j) {
        WorldSettings s;
        s.name = j["name"].toString("Untitled World");
        s.description = j["description"].toString();
        s.crsEpsg = j["crsEpsg"].toInt(4326);
        s.originLat = j["originLat"].toDouble(0);
        s.originLon = j["originLon"].toDouble(0);
        s.originX = j["originX"].toDouble(0);
        s.originY = j["originY"].toDouble(0);
        s.originZ = j["originZ"].toDouble(0);
        s.terrainSize = float(j["terrainSize"].toDouble(4000));
        s.heightScale = float(j["heightScale"].toDouble(100));
        s.heightmapPath = j["heightmapPath"].toString();
        s.albedoPath = j["albedoPath"].toString();
        s.sunYaw = float(j["sunYaw"].toDouble(45));
        s.sunPitch = float(j["sunPitch"].toDouble(60));
        s.sunIntensity = float(j["sunIntensity"].toDouble(3));
        s.skyColorR = float(j["skyR"].toDouble(0.4));
        s.skyColorG = float(j["skyG"].toDouble(0.6));
        s.skyColorB = float(j["skyB"].toDouble(0.9));
        s.fogDensity = float(j["fogDensity"].toDouble(0));
        s.fogColorR = float(j["fogR"].toDouble(0.5));
        s.fogColorG = float(j["fogG"].toDouble(0.5));
        s.fogColorB = float(j["fogB"].toDouble(0.5));
        s.exposure = float(j["exposure"].toDouble(1));
        s.timeOfDay = float(j["timeOfDay"].toDouble(12));
        s.gridSize = float(j["gridSize"].toDouble(100));
        s.gridDivisions = j["gridDivisions"].toInt(40);
        s.gridVisible = j["gridVisible"].toBool(true);
        s.snapEnabled = j["snapEnabled"].toBool(false);
        s.snapSize = float(j["snapSize"].toDouble(1));
        s.snapToSurface = j["snapToSurface"].toBool(false);
        return s;
    }
};

// ============================================================
// TerrainTile — A single terrain tile for large worlds
// ============================================================

struct TerrainTile {
    QString id;
    int row = 0, col = 0;
    float west = 0, east = 0, north = 0, south = 0;  // world bounds
    int width = 256, height = 256;  // grid resolution
    QString heightmapPath;
    QString albedoPath;
    bool loaded = false;
    bool dirty = false;

    QJsonObject toJson() const {
        QJsonObject j;
        j["id"] = id;
        j["row"] = row; j["col"] = col;
        j["west"] = west; j["east"] = east;
        j["north"] = north; j["south"] = south;
        j["width"] = width; j["height"] = height;
        j["heightmapPath"] = heightmapPath;
        j["albedoPath"] = albedoPath;
        j["loaded"] = loaded;
        j["dirty"] = dirty;
        return j;
    }

    static TerrainTile fromJson(const QJsonObject& j) {
        TerrainTile t;
        t.id = j["id"].toString();
        t.row = j["row"].toInt(); t.col = j["col"].toInt();
        t.west = float(j["west"].toDouble());
        t.east = float(j["east"].toDouble());
        t.north = float(j["north"].toDouble());
        t.south = float(j["south"].toDouble());
        t.width = j["width"].toInt(256);
        t.height = j["height"].toInt(256);
        t.heightmapPath = j["heightmapPath"].toString();
        t.albedoPath = j["albedoPath"].toString();
        t.loaded = j["loaded"].toBool(false);
        t.dirty = j["dirty"].toBool(false);
        return t;
    }
};

// ============================================================
// TerrainMask — A mask layer for terrain
// ============================================================

enum class MaskType {
    Height, Slope, Elevation, Curvature,
    Road, Building, Water, Vegetation, Forest,
    Urban, Sand, Rock, Grass, Custom
};

struct TerrainMask {
    QString id;
    QString name;
    MaskType type = MaskType::Custom;
    int width = 512, height = 512;
    std::vector<uint8_t> data;  // 0-255
    bool enabled = true;
    float opacity = 1.0f;
    QString sourcePath;  // if loaded from file

    // Blend operations
    enum class BlendOp { Add, Subtract, Multiply, Min, Max, Replace };
    BlendOp blendOp = BlendOp::Replace;

    QJsonObject toJson() const {
        QJsonObject j;
        j["id"] = id;
        j["name"] = name;
        j["type"] = static_cast<int>(type);
        j["width"] = width; j["height"] = height;
        j["enabled"] = enabled;
        j["opacity"] = opacity;
        j["sourcePath"] = sourcePath;
        j["blendOp"] = static_cast<int>(blendOp);
        // Data is not serialized directly — use sourcePath
        return j;
    }

    static TerrainMask fromJson(const QJsonObject& j) {
        TerrainMask m;
        m.id = j["id"].toString();
        m.name = j["name"].toString();
        m.type = static_cast<MaskType>(j["type"].toInt(13));
        m.width = j["width"].toInt(512);
        m.height = j["height"].toInt(512);
        m.enabled = j["enabled"].toBool(true);
        m.opacity = float(j["opacity"].toDouble(1));
        m.sourcePath = j["sourcePath"].toString();
        m.blendOp = static_cast<BlendOp>(j["blendOp"].toInt(5));
        return m;
    }

    bool isValid() const { return width > 0 && height > 0; }
    uint8_t at(int x, int y) const {
        if (x < 0 || x >= width || y < 0 || y >= height) return 0;
        if (data.empty()) return 0;
        return data[y * width + x];
    }
    void set(int x, int y, uint8_t v) {
        if (x < 0 || x >= width || y < 0 || y >= height) return;
        if (static_cast<int>(data.size()) < width * height)
            data.resize(width * height, 0);
        data[y * width + x] = v;
    }
};

// ============================================================
// TerrainMaterial — Layered terrain material
// ============================================================

struct TerrainMaterialLayer {
    QString name;
    QString albedoPath;
    QString normalPath;
    QString roughnessPath;
    float slopeMin = 0, slopeMax = 90;  // degrees
    float heightMin = 0, heightMax = 10000;  // meters
    QString maskId;  // associated mask
    float maskInfluence = 1.0f;
    float tiling = 10.0f;

    QJsonObject toJson() const {
        QJsonObject j;
        j["name"] = name;
        j["albedoPath"] = albedoPath;
        j["normalPath"] = normalPath;
        j["roughnessPath"] = roughnessPath;
        j["slopeMin"] = slopeMin; j["slopeMax"] = slopeMax;
        j["heightMin"] = heightMin; j["heightMax"] = heightMax;
        j["maskId"] = maskId;
        j["maskInfluence"] = maskInfluence;
        j["tiling"] = tiling;
        return j;
    }

    static TerrainMaterialLayer fromJson(const QJsonObject& j) {
        TerrainMaterialLayer l;
        l.name = j["name"].toString();
        l.albedoPath = j["albedoPath"].toString();
        l.normalPath = j["normalPath"].toString();
        l.roughnessPath = j["roughnessPath"].toString();
        l.slopeMin = float(j["slopeMin"].toDouble(0));
        l.slopeMax = float(j["slopeMax"].toDouble(90));
        l.heightMin = float(j["heightMin"].toDouble(0));
        l.heightMax = float(j["heightMax"].toDouble(10000));
        l.maskId = j["maskId"].toString();
        l.maskInfluence = float(j["maskInfluence"].toDouble(1));
        l.tiling = float(j["tiling"].toDouble(10));
        return l;
    }
};

struct TerrainMaterial {
    QString id;
    QString name;
    QList<TerrainMaterialLayer> layers;

    QJsonObject toJson() const {
        QJsonObject j;
        j["id"] = id; j["name"] = name;
        QJsonArray arr;
        for (const auto& l : layers) arr.append(l.toJson());
        j["layers"] = arr;
        return j;
    }

    static TerrainMaterial fromJson(const QJsonObject& j) {
        TerrainMaterial m;
        m.id = j["id"].toString();
        m.name = j["name"].toString();
        QJsonArray arr = j["layers"].toArray();
        for (const auto& v : arr)
            m.layers.append(TerrainMaterialLayer::fromJson(v.toObject()));
        return m;
    }
};

// ============================================================
// Biome — Procedural biome definition
// ============================================================

struct Biome {
    QString id;
    QString name;
    float temperatureMin = 0, temperatureMax = 40;
    float moistureMin = 0, moistureMax = 1;
    float elevationMin = 0, elevationMax = 10000;
    float slopeMin = 0, slopeMax = 90;
    QString terrainMaterialId;
    struct VegEntry {
        QString name;
        float density = 0.5f;
        QString meshPath;
        float scaleMin = 1, scaleMax = 1;
    };
    QList<VegEntry> vegetation;
    QList<VegEntry> rocks;
    QList<VegEntry> groundObjects;

    QJsonObject toJson() const {
        QJsonObject j;
        j["id"] = id; j["name"] = name;
        j["tempMin"] = temperatureMin; j["tempMax"] = temperatureMax;
        j["moistMin"] = moistureMin; j["moistMax"] = moistureMax;
        j["elevMin"] = elevationMin; j["elevMax"] = elevationMax;
        j["slopeMin"] = slopeMin; j["slopeMax"] = slopeMax;
        j["terrainMaterialId"] = terrainMaterialId;
        QJsonArray vegArr;
        for (const auto& v : vegetation) {
            QJsonObject vj;
            vj["name"] = v.name; vj["density"] = v.density;
            vj["meshPath"] = v.meshPath;
            vj["scaleMin"] = v.scaleMin; vj["scaleMax"] = v.scaleMax;
            vegArr.append(vj);
        }
        j["vegetation"] = vegArr;
        return j;
    }

    static Biome fromJson(const QJsonObject& j) {
        Biome b;
        b.id = j["id"].toString(); b.name = j["name"].toString();
        b.temperatureMin = float(j["tempMin"].toDouble(0));
        b.temperatureMax = float(j["tempMax"].toDouble(40));
        b.moistureMin = float(j["moistMin"].toDouble(0));
        b.moistureMax = float(j["moistMax"].toDouble(1));
        b.elevationMin = float(j["elevMin"].toDouble(0));
        b.elevationMax = float(j["elevMax"].toDouble(10000));
        b.slopeMin = float(j["slopeMin"].toDouble(0));
        b.slopeMax = float(j["slopeMax"].toDouble(90));
        b.terrainMaterialId = j["terrainMaterialId"].toString();
        QJsonArray vegArr = j["vegetation"].toArray();
        for (const auto& v : vegArr) {
            auto vo = v.toObject();
            VegEntry e;
            e.name = vo["name"].toString();
            e.density = float(vo["density"].toDouble(0.5));
            e.meshPath = vo["meshPath"].toString();
            e.scaleMin = float(vo["scaleMin"].toDouble(1));
            e.scaleMax = float(vo["scaleMax"].toDouble(1));
            b.vegetation.append(e);
        }
        return b;
    }
};

// ============================================================
// WaterBody — Water feature
// ============================================================

enum class WaterType { Ocean, Lake, River, Pool };

struct WaterBody {
    QString id;
    QString name;
    WaterType type = WaterType::Lake;
    float level = 0;  // water surface height
    float x = 0, z = 0;  // center position
    float sizeX = 100, sizeZ = 100;  // dimensions
    QString splineId;  // for rivers
    float flowSpeed = 0;
    float flowDirection = 0;
    float transparency = 0.8f;
    float colorR = 0.2f, colorG = 0.4f, colorB = 0.6f, colorA = 0.8f;

    QJsonObject toJson() const {
        QJsonObject j;
        j["id"] = id; j["name"] = name;
        j["type"] = static_cast<int>(type);
        j["level"] = level;
        j["x"] = x; j["z"] = z;
        j["sizeX"] = sizeX; j["sizeZ"] = sizeZ;
        j["splineId"] = splineId;
        j["flowSpeed"] = flowSpeed;
        j["flowDirection"] = flowDirection;
        j["transparency"] = transparency;
        j["colorR"] = colorR; j["colorG"] = colorG;
        j["colorB"] = colorB; j["colorA"] = colorA;
        return j;
    }

    static WaterBody fromJson(const QJsonObject& j) {
        WaterBody w;
        w.id = j["id"].toString(); w.name = j["name"].toString();
        w.type = static_cast<WaterType>(j["type"].toInt(1));
        w.level = float(j["level"].toDouble(0));
        w.x = float(j["x"].toDouble(0)); w.z = float(j["z"].toDouble(0));
        w.sizeX = float(j["sizeX"].toDouble(100));
        w.sizeZ = float(j["sizeZ"].toDouble(100));
        w.splineId = j["splineId"].toString();
        w.flowSpeed = float(j["flowSpeed"].toDouble(0));
        w.flowDirection = float(j["flowDirection"].toDouble(0));
        w.transparency = float(j["transparency"].toDouble(0.8));
        w.colorR = float(j["colorR"].toDouble(0.2));
        w.colorG = float(j["colorG"].toDouble(0.4));
        w.colorB = float(j["colorB"].toDouble(0.6));
        w.colorA = float(j["colorA"].toDouble(0.8));
        return w;
    }
};

} // namespace world
