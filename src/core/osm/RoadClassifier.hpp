#pragma once

// ============================================================
// RoadClassifier — OSM highway tags → road classification
// ============================================================
//
// Maps OSM highway types to engineering road classes with
// configurable defaults for width, lanes, speed, markings, etc.
//
// Priority for road parameters:
//   1. Explicit OSM tags (width, lanes, maxspeed)
//   2. Road class defaults (from this classifier)
//   3. Project-level defaults
//

#include <QString>
#include <QMap>
#include <cmath>

namespace osm {

// ─── RoadClass — engineering road classification ───
enum class RoadClass {
    Motorway,       // highway=motorway
    Trunk,          // highway=trunk
    Primary,        // highway=primary
    Secondary,      // highway=secondary
    Tertiary,       // highway=tertiary
    Residential,    // highway=residential
    Service,        // highway=service
    LivingStreet,   // highway=living_street
    Unclassified,   // highway=unclassified
    Track,          // highway=track
    Path,           // highway=path
    Footway,        // highway=footway
    Cycleway,       // highway=cycleway
    Pedestrian,     // highway=pedestrian
    Bridleway,      // highway=bridleway
    Steps,          // highway=steps
    Road,           // highway=road (unknown type)
    Unknown
};

// ─── RoadClassInfo — defaults for a road class ───
struct RoadClassInfo {
    RoadClass cls = RoadClass::Unknown;
    QString osmValue;          // OSM highway tag value
    QString displayName;       // Human-readable name

    // Geometry defaults
    double defaultWidth = 7.0;       // total road width (meters)
    int defaultLanes = 2;            // total lane count
    double defaultLaneWidth = 3.5;   // per-lane width (meters)
    double defaultSpeed = 50;        // km/h

    // Feature flags
    bool hasSidewalk = false;
    bool hasShoulder = false;
    bool hasMedian = false;
    bool hasCenterline = true;
    bool hasLaneMarkings = true;
    bool isOneWay = false;
    bool hasLighting = false;

    // Surface
    QString defaultSurface = "asphalt";

    // ─── Serialization ───
    QJsonObject toJson() const {
        return {
            {"osmValue", osmValue}, {"displayName", displayName},
            {"defaultWidth", defaultWidth}, {"defaultLanes", defaultLanes},
            {"defaultLaneWidth", defaultLaneWidth}, {"defaultSpeed", defaultSpeed},
            {"hasSidewalk", hasSidewalk}, {"hasShoulder", hasShoulder},
            {"hasMedian", hasMedian}, {"hasCenterline", hasCenterline},
            {"hasLaneMarkings", hasLaneMarkings}, {"isOneWay", isOneWay},
            {"hasLighting", hasLighting}, {"defaultSurface", defaultSurface}
        };
    }

    static RoadClassInfo fromJson(const QJsonObject& j) {
        RoadClassInfo r;
        r.osmValue = j["osmValue"].toString();
        r.displayName = j["displayName"].toString();
        r.defaultWidth = j["defaultWidth"].toDouble(7.0);
        r.defaultLanes = j["defaultLanes"].toInt(2);
        r.defaultLaneWidth = j["defaultLaneWidth"].toDouble(3.5);
        r.defaultSpeed = j["defaultSpeed"].toDouble(50);
        r.hasSidewalk = j["hasSidewalk"].toBool(false);
        r.hasShoulder = j["hasShoulder"].toBool(false);
        r.hasMedian = j["hasMedian"].toBool(false);
        r.hasCenterline = j["hasCenterline"].toBool(true);
        r.hasLaneMarkings = j["hasLaneMarkings"].toBool(true);
        r.isOneWay = j["isOneWay"].toBool(false);
        r.hasLighting = j["hasLighting"].toBool(false);
        r.defaultSurface = j["defaultSurface"].toString("asphalt");
        return r;
    }
};

// ─── RoadClassifier ───
class RoadClassifier {
public:
    // Get default info for a road class
    static RoadClassInfo getInfo(RoadClass cls) {
        switch (cls) {
        case RoadClass::Motorway:
            return {cls, "motorway", "Motorway",
                    14.0, 4, 3.75, 120,
                    false, true, true, false, true, true, false, "asphalt"};
        case RoadClass::Trunk:
            return {cls, "trunk", "Trunk Road",
                    11.0, 2, 3.75, 100,
                    false, true, true, true, true, false, false, "asphalt"};
        case RoadClass::Primary:
            return {cls, "primary", "Primary Road",
                    9.0, 2, 3.5, 80,
                    false, false, false, true, true, false, false, "asphalt"};
        case RoadClass::Secondary:
            return {cls, "secondary", "Secondary Road",
                    8.0, 2, 3.5, 60,
                    false, false, false, true, true, false, false, "asphalt"};
        case RoadClass::Tertiary:
            return {cls, "tertiary", "Tertiary Road",
                    7.5, 2, 3.5, 50,
                    false, false, false, true, true, false, false, "asphalt"};
        case RoadClass::Residential:
            return {cls, "residential", "Residential",
                    7.0, 2, 3.0, 30,
                    true, false, false, true, true, false, true, "asphalt"};
        case RoadClass::Service:
            return {cls, "service", "Service Road",
                    4.0, 1, 3.0, 20,
                    false, false, false, false, false, true, false, "asphalt"};
        case RoadClass::LivingStreet:
            return {cls, "living_street", "Living Street",
                    5.0, 1, 3.0, 10,
                    true, false, false, false, false, false, true, "paving_stones"};
        case RoadClass::Unclassified:
            return {cls, "unclassified", "Unclassified",
                    6.0, 2, 3.0, 50,
                    false, false, false, true, true, false, false, "asphalt"};
        case RoadClass::Track:
            return {cls, "track", "Track",
                    3.0, 1, 2.5, 20,
                    false, false, false, false, false, false, false, "gravel"};
        case RoadClass::Path:
            return {cls, "path", "Path",
                    1.5, 0, 1.5, 0,
                    false, false, false, false, false, true, false, "dirt"};
        case RoadClass::Footway:
            return {cls, "footway", "Footway",
                    1.5, 0, 1.5, 0,
                    false, false, false, false, false, true, false, "paving_stones"};
        case RoadClass::Cycleway:
            return {cls, "cycleway", "Cycleway",
                    2.0, 1, 2.0, 25,
                    false, false, false, false, false, true, false, "asphalt"};
        case RoadClass::Pedestrian:
            return {cls, "pedestrian", "Pedestrian",
                    3.0, 0, 3.0, 0,
                    false, false, false, false, false, true, false, "paving_stones"};
        case RoadClass::Bridleway:
            return {cls, "bridleway", "Bridleway",
                    2.0, 0, 2.0, 0,
                    false, false, false, false, false, true, false, "dirt"};
        case RoadClass::Steps:
            return {cls, "steps", "Steps",
                    1.0, 0, 1.0, 0,
                    false, false, false, false, false, true, false, "concrete"};
        case RoadClass::Road:
            return {cls, "road", "Road (Unknown)",
                    7.0, 2, 3.5, 50,
                    false, false, false, true, true, false, false, "asphalt"};
        default:
            return {cls, "unknown", "Unknown",
                    5.0, 1, 3.0, 30,
                    false, false, false, false, false, false, false, "asphalt"};
        }
    }

    // Convenience: classify and get info in one call
    static RoadClassInfo classifyAndGet(const QString& highwayValue) {
        return getInfo(classify(highwayValue));
    }

    // Check if a road class is drivable
    static bool isDrivable(RoadClass cls) {
        return cls != RoadClass::Path &&
               cls != RoadClass::Footway &&
               cls != RoadClass::Pedestrian &&
               cls != RoadClass::Bridleway &&
               cls != RoadClass::Steps &&
               cls != RoadClass::Unknown;
    }

private:
    // Classify an OSM highway tag value into a RoadClass
    static RoadClass classify(const QString& highwayValue) {
        static const QMap<QString, RoadClass> map = {
            {"motorway",       RoadClass::Motorway},
            {"motorway_link",  RoadClass::Motorway},
            {"trunk",          RoadClass::Trunk},
            {"trunk_link",     RoadClass::Trunk},
            {"primary",        RoadClass::Primary},
            {"primary_link",   RoadClass::Primary},
            {"secondary",      RoadClass::Secondary},
            {"secondary_link", RoadClass::Secondary},
            {"tertiary",       RoadClass::Tertiary},
            {"tertiary_link",  RoadClass::Tertiary},
            {"residential",    RoadClass::Residential},
            {"service",        RoadClass::Service},
            {"living_street",  RoadClass::LivingStreet},
            {"unclassified",   RoadClass::Unclassified},
            {"track",          RoadClass::Track},
            {"path",           RoadClass::Path},
            {"footway",        RoadClass::Footway},
            {"cycleway",       RoadClass::Cycleway},
            {"pedestrian",     RoadClass::Pedestrian},
            {"bridleway",      RoadClass::Bridleway},
            {"steps",          RoadClass::Steps},
            {"road",           RoadClass::Road},
        };
        return map.value(highwayValue, RoadClass::Unknown);
    }
};

} // namespace osm
