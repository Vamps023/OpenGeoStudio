#pragma once

// ============================================================
// RoadTypes — Core road data model types
// ============================================================
//
// Mirrors modules/road-studio/shared/types.ts.
// These types are used by the RoadStudioStore and the 2D/3D viewports.
//

#include <QString>
#include <QList>
#include <QPointF>
#include <QJsonObject>
#include <QJsonArray>
#include <QColor>
#include <optional>
#include <cmath>

namespace roads {

// RoadProfile — SCANeR-style road profile
struct RoadProfile {
    QString type = "city_2x1";  // profile type key
    QString surfaceTexture;
    QString markingTexture;
    double laneWidth = 3.5;
    bool hasSidewalk = false;
    bool hasCurb = false;
    int leftLanes = 1;       // lanes on left side
    int rightLanes = 1;      // lanes on right side
    int leftOffsetX2 = 0;    // left offset (×2)
    int rightOffsetX2 = 0;   // right offset (×2)
    double speedLimit = 50;  // km/h
    QString description;

    // ─── Road style classification (RoadBuilder-inspired) ───
    // Controls prop generation, mesh style, and visualization.
    enum class Style { AtGrade, Elevated, Tunnel, Bridge };
    Style style = Style::AtGrade;

    // Elevated road parameters
    double elevatedHeight = 8.0;       // deck height above ground (m)
    double pillarSpacing = 30.0;       // center-to-center pillar spacing (m)
    double pillarWidth = 1.2;          // pillar cross-section width (m)

    // Tunnel parameters
    double tunnelClearance = 5.5;      // interior clearance height (m)
    double tunnelWidth = 12.0;         // interior width (m)
    bool tunnelLighting = true;        // generate tunnel light props

    // Prop generation flags
    bool generateGuardrails = false;
    bool generateStreetlights = false;
    bool generatePillars = false;      // elevated
    bool generateTunnelShell = false;  // tunnel

    QJsonObject toJson() const {
        return {
            {"type", type}, {"surfaceTexture", surfaceTexture},
            {"markingTexture", markingTexture}, {"laneWidth", laneWidth},
            {"hasSidewalk", hasSidewalk}, {"hasCurb", hasCurb},
            {"leftLanes", leftLanes}, {"rightLanes", rightLanes},
            {"leftOffsetX2", leftOffsetX2}, {"rightOffsetX2", rightOffsetX2},
            {"speedLimit", speedLimit}, {"description", description},
            {"style", int(style)},
            {"elevatedHeight", elevatedHeight}, {"pillarSpacing", pillarSpacing},
            {"pillarWidth", pillarWidth},
            {"tunnelClearance", tunnelClearance}, {"tunnelWidth", tunnelWidth},
            {"tunnelLighting", tunnelLighting},
            {"generateGuardrails", generateGuardrails},
            {"generateStreetlights", generateStreetlights},
            {"generatePillars", generatePillars},
            {"generateTunnelShell", generateTunnelShell}
        };
    }

    static RoadProfile fromJson(const QJsonObject& j) {
        RoadProfile p;
        p.type = j["type"].toString("city_2x1");
        p.surfaceTexture = j["surfaceTexture"].toString();
        p.markingTexture = j["markingTexture"].toString();
        p.laneWidth = j["laneWidth"].toDouble(3.5);
        p.hasSidewalk = j["hasSidewalk"].toBool(false);
        p.hasCurb = j["hasCurb"].toBool(false);
        p.leftLanes = j["leftLanes"].toInt(1);
        p.rightLanes = j["rightLanes"].toInt(1);
        p.leftOffsetX2 = j["leftOffsetX2"].toInt(0);
        p.rightOffsetX2 = j["rightOffsetX2"].toInt(0);
        p.speedLimit = j["speedLimit"].toDouble(50);
        p.description = j["description"].toString();
        p.style = Style(j["style"].toInt(int(Style::AtGrade)));
        p.elevatedHeight = j["elevatedHeight"].toDouble(8.0);
        p.pillarSpacing = j["pillarSpacing"].toDouble(30.0);
        p.pillarWidth = j["pillarWidth"].toDouble(1.2);
        p.tunnelClearance = j["tunnelClearance"].toDouble(5.5);
        p.tunnelWidth = j["tunnelWidth"].toDouble(12.0);
        p.tunnelLighting = j["tunnelLighting"].toBool(true);
        p.generateGuardrails = j["generateGuardrails"].toBool(false);
        p.generateStreetlights = j["generateStreetlights"].toBool(false);
        p.generatePillars = j["generatePillars"].toBool(false);
        p.generateTunnelShell = j["generateTunnelShell"].toBool(false);
        return p;
    }
};

// ─── SCANeR-style Road Profile Catalog ──────────────────────
// Comprehensive set of predefined road profiles based on
// SCANeR Studio .rndProfile files and real-world road standards.
struct RoadProfileCatalog {
    // Get all predefined profiles as a map (key → profile)
    static QMap<QString, RoadProfile> all() {
        return {
            // ─── Urban / City ───────────────────────────
            {"city_2x1", {
                "city_2x1", "asphalt", "marking", 3.5,
                true, true, 1, 1, 0, 0, 50,
                "City road — 1 lane each way, sidewalk, curb"
            }},
            {"city_2x2", {
                "city_2x2", "asphalt", "marking", 3.5,
                true, true, 2, 2, 0, 0, 50,
                "City road — 2 lanes each way, sidewalk, curb"
            }},
            {"city_2x3", {
                "city_2x3", "asphalt", "marking", 3.5,
                true, true, 3, 3, 0, 0, 60,
                "City boulevard — 3 lanes each way, sidewalk, curb"
            }},
            {"city_oneway_1x2", {
                "city_oneway_1x2", "asphalt", "marking", 3.5,
                true, true, 0, 2, 0, 0, 40,
                "City one-way — 2 lanes, sidewalk, curb"
            }},
            {"city_oneway_1x3", {
                "city_oneway_1x3", "asphalt", "marking", 3.5,
                true, true, 0, 3, 0, 0, 40,
                "City one-way — 3 lanes, sidewalk, curb"
            }},

            // ─── Rural / Country ────────────────────────
            {"country_2x1", {
                "country_2x1", "asphalt", "marking", 3.5,
                false, false, 1, 1, 0, 0, 80,
                "Country road — 1 lane each way, no sidewalk"
            }},
            {"country_2x2", {
                "country_2x2", "asphalt", "marking", 3.5,
                false, false, 2, 2, 0, 0, 80,
                "Country road — 2 lanes each way, no sidewalk"
            }},
            {"rural_narrow_2x1", {
                "rural_narrow_2x1", "asphalt", "marking", 3.0,
                false, false, 1, 1, 0, 0, 70,
                "Rural narrow — 1 lane each way, 3.0m lanes"
            }},

            // ─── Highway / Motorway ─────────────────────
            {"highway_2x2", {
                "highway_2x2", "asphalt", "marking", 3.75,
                false, false, 2, 2, 0, 0, 120,
                "Highway — 2 lanes each way, 3.75m lanes"
            }},
            {"highway_2x3", {
                "highway_2x3", "asphalt", "marking", 3.75,
                false, false, 3, 3, 0, 0, 120,
                "Highway — 3 lanes each way, 3.75m lanes"
            }},
            {"highway_2x4", {
                "highway_2x4", "asphalt", "marking", 3.75,
                false, false, 4, 4, 0, 0, 120,
                "Major highway — 4 lanes each way, 3.75m lanes"
            }},

            // ─── Ramp / Interchange ─────────────────────
            {"ramp_1x1", {
                "ramp_1x1", "asphalt", "marking", 4.0,
                false, false, 1, 0, 0, 0, 60,
                "Exit ramp — 1 lane, 4.0m width"
            }},
            {"ramp_1x2", {
                "ramp_1x2", "asphalt", "marking", 4.0,
                false, false, 2, 0, 0, 0, 50,
                "Wide ramp — 2 lanes, 4.0m width"
            }},

            // ─── Roundabout / Circle ────────────────────
            {"roundabout_1x1", {
                "roundabout_1x1", "asphalt", "marking", 4.5,
                true, true, 1, 0, 0, 0, 30,
                "Roundabout — 1 lane, 4.5m width, sidewalk"
            }},
            {"roundabout_2x1", {
                "roundabout_2x1", "asphalt", "marking", 4.5,
                true, true, 2, 0, 0, 0, 25,
                "Roundabout — 2 lanes, 4.5m width, sidewalk"
            }},

            // ─── Parking / Service ──────────────────────
            {"parking_1x1", {
                "parking_1x1", "asphalt", "marking", 3.0,
                true, true, 1, 0, 0, 0, 15,
                "Parking road — 1 lane, 3.0m, sidewalk"
            }},
            {"service_1x1", {
                "service_1x1", "asphalt", "marking", 3.0,
                false, false, 1, 0, 0, 0, 20,
                "Service road — 1 lane, 3.0m"
            }},

            // ─── Asymmetric / Divided ───────────────────
            {"divided_2x3", {
                "divided_2x3", "asphalt", "marking", 3.5,
                false, false, 2, 3, 0, 0, 80,
                "Asymmetric divided — 2 left, 3 right"
            }},
            {"divided_1x2", {
                "divided_1x2", "asphalt", "marking", 3.5,
                false, false, 1, 2, 0, 0, 60,
                "Asymmetric divided — 1 left, 2 right"
            }},

            // ─── Custom ─────────────────────────────────
            {"custom", {
                "custom", "asphalt", "marking", 3.5,
                false, false, 1, 1, 0, 0, 50,
                "Custom — user-defined configuration"
            }},

            // ─── Bicycle / Bike Lane ────────────────────
            {"urban_bike_2x1", {
                "urban_bike_2x1", "asphalt", "marking", 3.25,
                true, true, 1, 1, 0, 0, 40,
                "Urban with bike lane — 1 lane each way, sidewalk, 3.25m"
            }},
            {"bike_lane_only", {
                "bike_lane_only", "asphalt", "marking", 1.5,
                true, true, 0, 1, 0, 0, 25,
                "Bike lane only — 1.5m width, sidewalk"
            }},

            // ─── Bus Lane ───────────────────────────────
            {"bus_lane_2x2", {
                "bus_lane_2x2", "asphalt", "marking", 3.5,
                true, true, 2, 2, 0, 0, 50,
                "Bus corridor — 2 lanes each way, sidewalk, curb"
            }},

            // ─── Divided Highway with Median ────────────
            {"divided_highway_2x3", {
                "divided_highway_2x3", "asphalt", "marking", 3.75,
                false, false, 3, 3, 2, 2, 110,
                "Divided highway — 3 lanes each way, 2m median offset"
            }},
            {"divided_highway_2x2", {
                "divided_highway_2x2", "asphalt", "marking", 3.75,
                false, false, 2, 2, 2, 2, 100,
                "Divided highway — 2 lanes each way, 2m median offset"
            }},

            // ─── Residential with Sidewalk ──────────────
            {"residential_2x1", {
                "residential_2x1", "asphalt", "marking", 3.0,
                true, true, 1, 1, 0, 0, 30,
                "Residential — 1 lane each way, 3.0m, sidewalk, curb"
            }},
            {"residential_narrow", {
                "residential_narrow", "asphalt", "marking", 2.75,
                true, true, 1, 1, 0, 0, 20,
                "Narrow residential — 2.75m lanes, sidewalk"
            }},

            // ─── Industrial ─────────────────────────────
            {"industrial_2x1", {
                "industrial_2x1", "asphalt", "marking", 4.0,
                false, true, 1, 1, 0, 0, 40,
                "Industrial — 4.0m lanes, curb, no sidewalk"
            }},
            {"industrial_2x2", {
                "industrial_2x2", "asphalt", "marking", 4.0,
                false, true, 2, 2, 0, 0, 40,
                "Industrial — 2 lanes each way, 4.0m, curb"
            }},

            // ─── Expressway ─────────────────────────────
            {"expressway_2x3", {
                "expressway_2x3", "asphalt", "marking", 3.75,
                false, false, 3, 3, 1, 1, 100,
                "Expressway — 3 lanes each way, 1m median"
            }},
            {"expressway_2x4", {
                "expressway_2x4", "asphalt", "marking", 3.75,
                false, false, 4, 4, 1, 1, 110,
                "Expressway — 4 lanes each way, 1m median"
            }},

            // ─── Elevated / Viaduct ─────────────────────
            {"elevated_2x2", {
                "elevated_2x2", "asphalt", "marking", 3.75,
                false, false, 2, 2, 0, 0, 80,
                "Elevated viaduct — 2 lanes each way on pillars"
            }},
            {"elevated_2x3", {
                "elevated_2x3", "asphalt", "marking", 3.75,
                false, false, 3, 3, 0, 0, 80,
                "Elevated viaduct — 3 lanes each way on pillars"
            }},
            {"elevated_ramp_1x1", {
                "elevated_ramp_1x1", "asphalt", "marking", 4.0,
                false, false, 1, 0, 0, 0, 50,
                "Elevated ramp — 1 lane on pillars, 4.0m width"
            }},
            {"elevated_2x4", {
                "elevated_2x4", "asphalt", "marking", 3.75,
                false, false, 4, 4, 0, 0, 100,
                "Elevated highway — 4 lanes each way on pillars"
            }},

            // ─── Tunnel ─────────────────────────────────
            {"tunnel_2x2", {
                "tunnel_2x2", "asphalt", "marking", 3.75,
                false, false, 2, 2, 0, 0, 80,
                "Tunnel — 2 lanes each way, enclosed, lit"
            }},
            {"tunnel_2x3", {
                "tunnel_2x3", "asphalt", "marking", 3.75,
                false, false, 3, 3, 0, 0, 80,
                "Tunnel — 3 lanes each way, enclosed, lit"
            }},
            {"tunnel_ramp_1x1", {
                "tunnel_ramp_1x1", "asphalt", "marking", 4.0,
                false, false, 1, 0, 0, 0, 50,
                "Tunnel ramp — 1 lane, enclosed, 4.0m width"
            }},
            {"tunnel_narrow_2x1", {
                "tunnel_narrow_2x1", "asphalt", "marking", 3.25,
                false, false, 1, 1, 0, 0, 60,
                "Narrow tunnel — 1 lane each way, 3.25m, lit"
            }},
        };
    }

    // Get a list of (key, description) pairs for UI dropdowns
    static QStringList profileNames() {
        QMap<QString, RoadProfile> profiles = all();
        QStringList names;
        for (auto it = profiles.begin(); it != profiles.end(); ++it) {
            names << it.key();
        }
        return names;
    }

    // Get profile by key, returns custom if not found
    static RoadProfile get(const QString& key) {
        auto profiles = all();
        if (!profiles.contains(key)) return profiles["custom"];
        RoadProfile p = profiles[key];
        // Auto-classify style and props from type name prefix
        // (RoadBuilder-inspired elevated/tunnel/bridge styles)
        if (key.startsWith("elevated_")) {
            p.style = RoadProfile::Style::Elevated;
            p.generatePillars = true;
            p.generateGuardrails = true;
            p.generateStreetlights = true;
        } else if (key.startsWith("tunnel_")) {
            p.style = RoadProfile::Style::Tunnel;
            p.generateTunnelShell = true;
            p.tunnelLighting = true;
            p.generateStreetlights = true;
        } else if (key.startsWith("highway_") || key.startsWith("expressway_")) {
            p.style = RoadProfile::Style::AtGrade;
            p.generateGuardrails = true;
            p.generateStreetlights = true;
        } else if (key.startsWith("ramp_")) {
            p.style = RoadProfile::Style::AtGrade;
            p.generateGuardrails = true;
        }
        return p;
    }

    // Get human-readable label for dropdown
    static QString label(const QString& key) {
        auto profiles = all();
        if (!profiles.contains(key)) return "Custom";
        return key + " — " + profiles[key].description;
    }
};

// ════════════════════════════════════════════════════════════
// Rail Profiles — Train Studio cross-section definitions
// ════════════════════════════════════════════════════════════

// RailProfile — railway track cross-section profile
struct RailProfile {
    QString type = "single_standard";
    double gauge = 1.435;          // rail gauge in meters (standard=1.435, narrow=1.067, broad=1.676)
    int trackCount = 1;            // number of parallel tracks (1-4)
    double trackSpacing = 4.0;     // center-to-center spacing between parallel tracks (m)
    double ballastWidth = 3.0;     // ballast width per track (m)
    double railHeight = 0.172;     // rail head height (m) — UIC60=0.172, BS80A=0.146
    double sleeperLength = 2.6;    // sleeper/tie length (m)
    double maxSpeed = 120;         // km/h
    QString railType = "UIC60";    // rail profile type
    QString sleeperType = "concrete";  // concrete, wood, steel
    QString description;

    QJsonObject toJson() const {
        return {
            {"type", type}, {"gauge", gauge}, {"trackCount", trackCount},
            {"trackSpacing", trackSpacing}, {"ballastWidth", ballastWidth},
            {"railHeight", railHeight}, {"sleeperLength", sleeperLength},
            {"maxSpeed", maxSpeed}, {"railType", railType},
            {"sleeperType", sleeperType}, {"description", description}
        };
    }

    static RailProfile fromJson(const QJsonObject& j) {
        RailProfile p;
        p.type = j["type"].toString("single_standard");
        p.gauge = j["gauge"].toDouble(1.435);
        p.trackCount = j["trackCount"].toInt(1);
        p.trackSpacing = j["trackSpacing"].toDouble(4.0);
        p.ballastWidth = j["ballastWidth"].toDouble(3.0);
        p.railHeight = j["railHeight"].toDouble(0.172);
        p.sleeperLength = j["sleeperLength"].toDouble(2.6);
        p.maxSpeed = j["maxSpeed"].toDouble(120);
        p.railType = j["railType"].toString("UIC60");
        p.sleeperType = j["sleeperType"].toString("concrete");
        p.description = j["description"].toString();
        return p;
    }
};

// ─── Rail Profile Catalog ───────────────────────────────────
struct RailProfileCatalog {
    static QMap<QString, RailProfile> all() {
        return {
            // ─── Single Track ────────────────────────────
            {"single_standard", {
                "single_standard", 1.435, 1, 4.0, 3.0, 0.172, 2.6, 120,
                "UIC60", "concrete",
                "Single track — standard gauge 1435mm, UIC60 rail"
            }},
            {"single_narrow", {
                "single_narrow", 1.067, 1, 3.5, 2.5, 0.140, 2.0, 80,
                "BS80A", "wood",
                "Single track — narrow gauge 1067mm, wood sleepers"
            }},
            {"single_broad", {
                "single_broad", 1.676, 1, 4.5, 3.5, 0.180, 3.0, 100,
                "UIC60", "concrete",
                "Single track — broad gauge 1676mm (Indian Gauge)"
            }},
            {"single_light_rail", {
                "single_light_rail", 1.435, 1, 3.5, 2.5, 0.140, 2.2, 60,
                "BS80A", "concrete",
                "Light rail / tram — standard gauge, low speed"
            }},

            // ─── Double Track ────────────────────────────
            {"double_standard", {
                "double_standard", 1.435, 2, 4.0, 6.0, 0.172, 2.6, 120,
                "UIC60", "concrete",
                "Double track — standard gauge, 4m spacing"
            }},
            {"double_high_speed", {
                "double_high_speed", 1.435, 2, 5.0, 7.0, 0.172, 2.6, 300,
                "UIC60", "concrete",
                "Double track — high speed, 5m spacing, 300km/h"
            }},
            {"double_narrow", {
                "double_narrow", 1.067, 2, 3.5, 5.0, 0.140, 2.0, 80,
                "BS80A", "wood",
                "Double track — narrow gauge 1067mm"
            }},

            // ─── Triple/Quadruple Track ──────────────────
            {"triple_standard", {
                "triple_standard", 1.435, 3, 4.0, 9.0, 0.172, 2.6, 120,
                "UIC60", "concrete",
                "Triple track — standard gauge, 3 parallel tracks"
            }},
            {"quadruple_standard", {
                "quadruple_standard", 1.435, 4, 4.0, 12.0, 0.172, 2.6, 120,
                "UIC60", "concrete",
                "Quadruple track — 4 parallel tracks, mainline"
            }},

            // ─── Tram / Streetcar ────────────────────────
            {"tram_embedded", {
                "tram_embedded", 1.435, 1, 3.0, 2.5, 0.060, 1.8, 50,
                "grooved", "embedded",
                "Tram — embedded rail, grooved rail, street use"
            }},
            {"tram_double", {
                "tram_double", 1.435, 2, 3.0, 5.0, 0.060, 1.8, 50,
                "grooved", "embedded",
                "Double tram — embedded rail, 2 tracks"
            }},

            // ─── Subway / Metro ──────────────────────────
            {"subway_single", {
                "subway_single", 1.435, 1, 3.5, 3.0, 0.140, 2.4, 80,
                "BS80A", "concrete",
                "Subway — single track tunnel"
            }},
            {"subway_double", {
                "subway_double", 1.435, 2, 3.5, 6.0, 0.140, 2.4, 80,
                "BS80A", "concrete",
                "Subway — double track tunnel"
            }},

            // ─── Custom ──────────────────────────────────
            {"custom_rail", {
                "custom_rail", 1.435, 1, 4.0, 3.0, 0.172, 2.6, 120,
                "UIC60", "concrete",
                "Custom — user-defined rail configuration"
            }},
        };
    }

    static QStringList profileNames() {
        QMap<QString, RailProfile> profiles = all();
        QStringList names;
        for (auto it = profiles.begin(); it != profiles.end(); ++it) {
            names << it.key();
        }
        return names;
    }

    static RailProfile get(const QString& key) {
        auto profiles = all();
        if (profiles.contains(key)) return profiles[key];
        return profiles["custom_rail"];
    }

    static QString label(const QString& key) {
        auto profiles = all();
        if (!profiles.contains(key)) return "Custom Rail";
        return key + " — " + profiles[key].description;
    }
};

} // namespace roads
