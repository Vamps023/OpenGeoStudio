#pragma once

// ═══════════════════════════════════════════════════════════
// Road Sign & Marking System — Data-driven registries, placement,
// persistence, and road furniture support.
// ═══════════════════════════════════════════════════════════

#include <QString>
#include <QMap>
#include <QJsonObject>
#include <QJsonArray>
#include <vector>
#include <string>
#include <set>

namespace LM
{

// ─── SignCategory ──────────────────────────────────────────
enum class SignCategory {
    Warning,
    Regulatory,
    Mandatory,
    Prohibition,
    Information,
    Direction,
    Pedestrian,
    Bicycle,
    Parking,
    Speed,
    Priority
};

// ─── SignSide ──────────────────────────────────────────────
enum class SignSide {
    Left,    // left side of road (negative t)
    Right,   // right side of road (positive t)
    Overhead // above the road
};

// ─── SignDefinition ────────────────────────────────────────
struct SignDefinition {
    QString id;           // unique sign type ID (e.g. "stop", "yield")
    QString displayName;  // user-visible name
    SignCategory category;
    QString iconPath;     // resource path to icon
    double defaultWidth = 0.6;   // meters
    double defaultHeight = 0.6;  // meters
    double mountHeight = 2.5;    // meters above road
    QString description;
    QString shape;        // "circle", "triangle", "octagon", "rectangle", "diamond"
    QString primaryColor; // "red", "blue", "yellow", "white"
};

// ─── PlacedSign ────────────────────────────────────────────
struct PlacedSign {
    std::string id;        // unique instance ID
    QString signType;      // references SignDefinition::id
    std::string roadID;    // road this sign is attached to
    double s = 0.0;        // station along road
    double tOffset = 0.0;  // lateral offset from ref line (negative=left)
    double rotation = 0.0; // rotation in degrees (0 = facing oncoming traffic)
    double height = 2.5;   // mount height
    SignSide side = SignSide::Right;
    std::map<QString, QString> metadata; // extensible key-value metadata

    QJsonObject toJson() const;
    static PlacedSign fromJson(const QJsonObject& json);
};

// ─── SignRegistry ──────────────────────────────────────────
class SignRegistry {
public:
    static SignRegistry* Instance();

    const QMap<QString, SignDefinition>& all() const { return m_definitions; }
    const SignDefinition* get(const QString& id) const;
    QStringList categoryNames() const;
    QList<SignDefinition> byCategory(SignCategory cat) const;
    static QString categoryToString(SignCategory cat);
    static SignCategory stringToCategory(const QString& name);

    // Placed sign management
    void addSign(const PlacedSign& sign);
    void updateSign(const PlacedSign& sign);
    void removeSign(const std::string& id);
    PlacedSign* findSign(const std::string& id);
    const std::vector<PlacedSign>& placedSigns() const { return m_placedSigns; }
    void clearPlaced() { m_placedSigns.clear(); }

    // Persistence
    QJsonArray placedToJson() const;
    void placedFromJson(const QJsonArray& json);

    // Get signs for a specific road
    std::vector<const PlacedSign*> signsForRoad(const std::string& roadID) const;

private:
    SignRegistry();
    void registerStandardSigns();

    QMap<QString, SignDefinition> m_definitions;
    std::vector<PlacedSign> m_placedSigns;
};

// ═══════════════════════════════════════════════════════════
// Marking System
// ═══════════════════════════════════════════════════════════

// ─── MarkingType ───────────────────────────────────────────
enum class MarkingType {
    // Lines (longitudinal)
    SolidLine,
    DashedLine,
    DoubleSolid,
    DoubleDashed,
    SolidDashed,
    DashedSolid,
    EdgeLine,
    CenterLine,
    LaneDivider,
    ShoulderLine,
    // Transverse
    StopLine,
    YieldLine,
    Crosswalk,
    ZebraCrossing,
    BicycleCrossing,
    // Arrows
    ArrowStraight,
    ArrowLeft,
    ArrowRight,
    ArrowStraightLeft,
    ArrowStraightRight,
    ArrowUTurn,
    ArrowMerge,
    ArrowDiverge,
    // Symbols
    SymbolBus,
    SymbolBicycle,
    SymbolAccessibility,
    SymbolParking,
    // Areas
    HatchedArea,
    ChevronArea,
    GoreArea,
    ParkingBay,
    BusStopMarking
};

// ─── MarkingPattern ────────────────────────────────────────
enum class MarkingPattern {
    Continuous,
    Dashed,
    DoubleLine,
    SolidDashed,
    DashedSolid,
    Zigzag,
    Dots
};

// ─── MarkingDefinition ─────────────────────────────────────
struct MarkingDefinition {
    MarkingType type;
    QString displayName;
    QString color = "white";
    double defaultWidth = 0.15;
    double dashLength = 3.0;
    double gapLength = 3.0;
    bool isLongitudinal = true; // true = along road, false = transverse
    MarkingPattern pattern = MarkingPattern::Continuous;
    QString material = "paint"; // "paint", "thermoplastic", "tape"
    QString category; // "line", "transverse", "arrow", "symbol", "area"
};

// ─── PlacedMarking ─────────────────────────────────────────
struct PlacedMarking {
    std::string id;
    MarkingType type = MarkingType::SolidLine;
    std::string roadID;
    double sStart = 0.0;
    double sEnd = 0.0;
    double tOffset = 0.0;  // lateral offset
    double width = 0.15;
    QString color = "white";
    int laneAssociation = 0; // lane ID this marking is associated with (0 = center)
    MarkingPattern pattern = MarkingPattern::Continuous;
    QString material = "paint";

    QJsonObject toJson() const;
    static PlacedMarking fromJson(const QJsonObject& json);
};

// ─── MarkingRegistry ───────────────────────────────────────
class MarkingRegistry {
public:
    static MarkingRegistry* Instance();

    const QMap<MarkingType, MarkingDefinition>& all() const { return m_definitions; }
    const MarkingDefinition* get(MarkingType type) const;
    static QString typeToString(MarkingType type);
    static MarkingType stringToType(const QString& name);

    void addMarking(const PlacedMarking& marking);
    void updateMarking(const PlacedMarking& marking);
    void removeMarking(const std::string& id);
    PlacedMarking* findMarking(const std::string& id);
    const std::vector<PlacedMarking>& placedMarkings() const { return m_placedMarkings; }
    void clearPlaced() { m_placedMarkings.clear(); }

    // Persistence
    QJsonArray placedToJson() const;
    void placedFromJson(const QJsonArray& json);

    // Get markings for a specific road
    std::vector<const PlacedMarking*> markingsForRoad(const std::string& roadID) const;

private:
    MarkingRegistry();
    void registerStandardMarkings();

    QMap<MarkingType, MarkingDefinition> m_definitions;
    std::vector<PlacedMarking> m_placedMarkings;
};

// ═══════════════════════════════════════════════════════════
// Road Furniture System
// ═══════════════════════════════════════════════════════════

// ─── FurnitureType ─────────────────────────────────────────
enum class FurnitureType {
    Guardrail,
    Barrier,
    Bollard,
    Delineator,
    StreetLight,
    PedestrianBarrier,
    BusStop,
    TrafficSignal,
    Camera,
    UtilityPole
};

// ─── FurnitureDefinition ───────────────────────────────────
struct FurnitureDefinition {
    QString id;
    QString displayName;
    FurnitureType type;
    double defaultWidth = 0.3;
    double defaultHeight = 1.0;
    double defaultLength = 1.0; // for linear objects like guardrails
    QString color = "gray";
    QString description;
    bool isLinear = false; // true = spans a distance, false = point object
};

// ─── PlacedFurniture ───────────────────────────────────────
struct PlacedFurniture {
    std::string id;
    QString furnitureType;  // references FurnitureDefinition::id
    std::string roadID;
    double sStart = 0.0;
    double sEnd = 0.0;   // for linear furniture
    double tOffset = 0.0;
    double height = 1.0;
    SignSide side = SignSide::Right;
    int repeatCount = 1;     // for repeated objects (e.g., bollards)
    double repeatSpacing = 10.0; // spacing between repeats
    // Skip repeated instances on curves tighter than this radius (m).
    // 0 = disabled (place everywhere). Point objects only.
    double minTurnRadius = 0.0;

    QJsonObject toJson() const;
    static PlacedFurniture fromJson(const QJsonObject& json);
};

// ─── FurnitureRegistry ─────────────────────────────────────
class FurnitureRegistry {
public:
    static FurnitureRegistry* Instance();

    const QMap<QString, FurnitureDefinition>& all() const { return m_definitions; }
    const FurnitureDefinition* get(const QString& id) const;
    QStringList typeNames() const;
    static QString typeToString(FurnitureType type);
    static FurnitureType stringToType(const QString& name);

    void addFurniture(const PlacedFurniture& furniture);
    void updateFurniture(const PlacedFurniture& furniture);
    void removeFurniture(const std::string& id);
    PlacedFurniture* findFurniture(const std::string& id);
    const std::vector<PlacedFurniture>& placedFurniture() const { return m_placedFurniture; }
    void clearPlaced() { m_placedFurniture.clear(); }

    // Persistence
    QJsonArray placedToJson() const;
    void placedFromJson(const QJsonArray& json);

    // Get furniture for a specific road
    std::vector<const PlacedFurniture*> furnitureForRoad(const std::string& roadID) const;

private:
    FurnitureRegistry();
    void registerStandardFurniture();

    QMap<QString, FurnitureDefinition> m_definitions;
    std::vector<PlacedFurniture> m_placedFurniture;
};

// ═══════════════════════════════════════════════════════════
// Snapping System
// ═══════════════════════════════════════════════════════════

// ─── SnapCategory ──────────────────────────────────────────
enum class SnapCategory {
    Endpoint,
    Midpoint,
    Vertex,
    Intersection,
    Tangent,
    Perpendicular,
    Road,
    Lane,
    Terrain,
    Grid
};

// ─── SnapResult ────────────────────────────────────────────
struct SnapResult {
    bool snapped = false;
    double x = 0, y = 0, z = 0;
    std::string roadID;
    double s = 0;        // station on road
    int laneId = 0;
    SnapCategory category;
    QString description;
};

// ─── SnapSettings ──────────────────────────────────────────
class SnapSettings {
public:
    static SnapSettings* Instance();

    void setEnabled(SnapCategory cat, bool enabled);
    bool isEnabled(SnapCategory cat) const;
    void toggleAll(bool enabled);
    std::set<SnapCategory> enabledCategories() const;
    static QString categoryToString(SnapCategory cat);

    double snapRadius = 5.0;      // snap radius in meters
    double gridSize = 10.0;       // grid spacing in meters

private:
    SnapSettings();
    std::set<SnapCategory> m_enabled;
};

// ═══════════════════════════════════════════════════════════
// Measurement System
// ═══════════════════════════════════════════════════════════

// ─── MeasurementType ───────────────────────────────────────
enum class MeasurementType {
    Distance,
    Angle,
    Area,
    Station,
    Coordinate,
    RoadLength,
    Radius
};

// ─── MeasurementResult ─────────────────────────────────────
struct MeasurementResult {
    MeasurementType type;
    double value = 0.0;
    QString unit;
    QString description;
    std::vector<std::array<double, 3>> points; // points used in measurement
};

// ─── MeasurementSystem ─────────────────────────────────────
class MeasurementSystem {
public:
    static MeasurementSystem* Instance();

    // Point-to-point distance
    MeasurementResult measureDistance(double x1, double y1, double z1,
                                      double x2, double y2, double z2) const;

    // Angle between three points (at p2)
    MeasurementResult measureAngle(double x1, double y1,
                                   double x2, double y2,
                                   double x3, double y3) const;

    // Area of polygon
    MeasurementResult measureArea(const std::vector<std::array<double, 2>>& polygon) const;

    // Station on a road
    MeasurementResult measureStation(const std::string& roadID, double s) const;

    // Coordinate display
    MeasurementResult measureCoordinate(double x, double y, double z) const;

    // Road length
    MeasurementResult measureRoadLength(const std::string& roadID) const;

    // Radius of a curve through three points
    MeasurementResult measureRadius(double x1, double y1,
                                    double x2, double y2,
                                    double x3, double y3) const;

    static QString typeToString(MeasurementType type);

private:
    MeasurementSystem() = default;
};

} // namespace LM
