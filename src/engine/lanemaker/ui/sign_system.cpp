#include "sign_system.h"
#include "road.h"
#include "world.h"
#include <cmath>
#include <algorithm>

namespace LM
{

// ═══════════════════════════════════════════════════════════
// SignRegistry
// ═══════════════════════════════════════════════════════════

SignRegistry* SignRegistry::Instance()
{
    static SignRegistry instance;
    return &instance;
}

SignRegistry::SignRegistry()
{
    registerStandardSigns();
}

void SignRegistry::registerStandardSigns()
{
    // ─── Regulatory ───
    m_definitions["stop"] = {"stop", "Stop", SignCategory::Regulatory,
        ":/rs/svg/sign_mode.svg", 0.6, 0.6, 2.5, "Stop sign — octagonal red", "octagon", "red"};
    m_definitions["yield"] = {"yield", "Yield", SignCategory::Regulatory,
        "", 0.9, 0.7, 2.5, "Yield sign — triangular", "triangle", "red"};
    m_definitions["one_way"] = {"one_way", "One Way", SignCategory::Regulatory,
        "", 0.6, 0.6, 2.5, "One way road", "rectangle", "blue"};

    // ─── Prohibition ───
    m_definitions["no_entry"] = {"no_entry", "No Entry", SignCategory::Prohibition,
        "", 0.6, 0.6, 2.5, "No entry — red circle", "circle", "red"};
    m_definitions["no_parking"] = {"no_parking", "No Parking", SignCategory::Prohibition,
        "", 0.5, 0.5, 2.5, "No parking", "circle", "red"};
    m_definitions["no_stopping"] = {"no_stopping", "No Stopping", SignCategory::Prohibition,
        "", 0.5, 0.5, 2.5, "No stopping", "circle", "red"};
    m_definitions["no_left_turn"] = {"no_left_turn", "No Left Turn", SignCategory::Prohibition,
        "", 0.6, 0.6, 2.5, "No left turn", "circle", "red"};
    m_definitions["no_right_turn"] = {"no_right_turn", "No Right Turn", SignCategory::Prohibition,
        "", 0.6, 0.6, 2.5, "No right turn", "circle", "red"};
    m_definitions["no_u_turn"] = {"no_u_turn", "No U-Turn", SignCategory::Prohibition,
        "", 0.6, 0.6, 2.5, "No U-turn", "circle", "red"};

    // ─── Warning ───
    m_definitions["curve_left"] = {"curve_left", "Curve Left", SignCategory::Warning,
        "", 0.5, 0.5, 2.5, "Curve to left warning", "triangle", "yellow"};
    m_definitions["curve_right"] = {"curve_right", "Curve Right", SignCategory::Warning,
        "", 0.5, 0.5, 2.5, "Curve to right warning", "triangle", "yellow"};
    m_definitions["road_narrows"] = {"road_narrows", "Road Narrows", SignCategory::Warning,
        "", 0.5, 0.5, 2.5, "Road narrows warning", "triangle", "yellow"};
    m_definitions["intersection"] = {"intersection", "Intersection", SignCategory::Warning,
        "", 0.5, 0.5, 2.5, "Intersection ahead", "triangle", "yellow"};
    m_definitions["school_zone"] = {"school_zone", "School Zone", SignCategory::Warning,
        "", 0.5, 0.5, 2.5, "School zone warning", "diamond", "yellow"};
    m_definitions["road_work"] = {"road_work", "Road Work", SignCategory::Warning,
        "", 0.5, 0.5, 2.5, "Road work ahead", "triangle", "yellow"};
    m_definitions["slippery"] = {"slippery", "Slippery Road", SignCategory::Warning,
        "", 0.5, 0.5, 2.5, "Slippery road ahead", "triangle", "yellow"};

    // ─── Speed ───
    m_definitions["speed_20"] = {"speed_20", "Speed 20", SignCategory::Speed,
        "", 0.6, 0.6, 2.5, "Speed limit 20 km/h", "circle", "red"};
    m_definitions["speed_30"] = {"speed_30", "Speed 30", SignCategory::Speed,
        "", 0.6, 0.6, 2.5, "Speed limit 30 km/h", "circle", "red"};
    m_definitions["speed_40"] = {"speed_40", "Speed 40", SignCategory::Speed,
        "", 0.6, 0.6, 2.5, "Speed limit 40 km/h", "circle", "red"};
    m_definitions["speed_50"] = {"speed_50", "Speed 50", SignCategory::Speed,
        "", 0.6, 0.6, 2.5, "Speed limit 50 km/h", "circle", "red"};
    m_definitions["speed_60"] = {"speed_60", "Speed 60", SignCategory::Speed,
        "", 0.6, 0.6, 2.5, "Speed limit 60 km/h", "circle", "red"};
    m_definitions["speed_70"] = {"speed_70", "Speed 70", SignCategory::Speed,
        "", 0.6, 0.6, 2.5, "Speed limit 70 km/h", "circle", "red"};
    m_definitions["speed_80"] = {"speed_80", "Speed 80", SignCategory::Speed,
        "", 0.6, 0.6, 2.5, "Speed limit 80 km/h", "circle", "red"};
    m_definitions["speed_90"] = {"speed_90", "Speed 90", SignCategory::Speed,
        "", 0.6, 0.6, 2.5, "Speed limit 90 km/h", "circle", "red"};
    m_definitions["speed_100"] = {"speed_100", "Speed 100", SignCategory::Speed,
        "", 0.6, 0.6, 2.5, "Speed limit 100 km/h", "circle", "red"};
    m_definitions["speed_110"] = {"speed_110", "Speed 110", SignCategory::Speed,
        "", 0.6, 0.6, 2.5, "Speed limit 110 km/h", "circle", "red"};
    m_definitions["speed_120"] = {"speed_120", "Speed 120", SignCategory::Speed,
        "", 0.6, 0.6, 2.5, "Speed limit 120 km/h", "circle", "red"};

    // ─── Mandatory ───
    m_definitions["straight_only"] = {"straight_only", "Straight Only", SignCategory::Mandatory,
        "", 0.6, 0.6, 2.5, "Straight only", "circle", "blue"};
    m_definitions["turn_left"] = {"turn_left", "Turn Left", SignCategory::Mandatory,
        "", 0.6, 0.6, 2.5, "Turn left only", "circle", "blue"};
    m_definitions["turn_right"] = {"turn_right", "Turn Right", SignCategory::Mandatory,
        "", 0.6, 0.6, 2.5, "Turn right only", "circle", "blue"};
    m_definitions["straight_or_left"] = {"straight_or_left", "Straight or Left", SignCategory::Mandatory,
        "", 0.6, 0.6, 2.5, "Straight or left", "circle", "blue"};
    m_definitions["straight_or_right"] = {"straight_or_right", "Straight or Right", SignCategory::Mandatory,
        "", 0.6, 0.6, 2.5, "Straight or right", "circle", "blue"};
    m_definitions["roundabout"] = {"roundabout", "Roundahead", SignCategory::Mandatory,
        "", 0.6, 0.6, 2.5, "Roundabout ahead", "circle", "blue"};

    // ─── Pedestrian ───
    m_definitions["pedestrian"] = {"pedestrian", "Pedestrian Crossing", SignCategory::Pedestrian,
        "", 0.5, 0.5, 2.5, "Pedestrian crossing ahead", "triangle", "yellow"};
    m_definitions["pedestrian_zone"] = {"pedestrian_zone", "Pedestrian Zone", SignCategory::Pedestrian,
        "", 0.6, 0.6, 2.5, "Pedestrian zone", "circle", "blue"};

    // ─── Bicycle ───
    m_definitions["bicycle"] = {"bicycle", "Bicycle", SignCategory::Bicycle,
        "", 0.5, 0.5, 2.5, "Bicycle crossing", "triangle", "yellow"};
    m_definitions["bike_lane"] = {"bike_lane", "Bike Lane", SignCategory::Bicycle,
        "", 0.6, 0.6, 2.5, "Bicycle lane", "circle", "blue"};

    // ─── Parking ───
    m_definitions["parking"] = {"parking", "Parking", SignCategory::Parking,
        "", 0.5, 0.5, 2.5, "Parking area", "rectangle", "blue"};
    m_definitions["disabled_parking"] = {"disabled_parking", "Disabled Parking", SignCategory::Parking,
        "", 0.5, 0.5, 2.5, "Disabled parking", "rectangle", "blue"};

    // ─── Information ───
    m_definitions["bus_stop"] = {"bus_stop", "Bus Stop", SignCategory::Information,
        "", 0.5, 0.5, 2.5, "Bus stop", "rectangle", "blue"};
    m_definitions["gas_station"] = {"gas_station", "Gas Station", SignCategory::Information,
        "", 0.5, 0.5, 2.5, "Gas station", "rectangle", "blue"};
    m_definitions["hospital"] = {"hospital", "Hospital", SignCategory::Information,
        "", 0.5, 0.5, 2.5, "Hospital", "rectangle", "red"};
    m_definitions["airport"] = {"airport", "Airport", SignCategory::Information,
        "", 0.5, 0.5, 2.5, "Airport", "rectangle", "blue"};

    // ─── Direction ───
    m_definitions["direction_forward"] = {"direction_forward", "Direction Forward", SignCategory::Direction,
        "", 0.6, 0.6, 2.5, "Direction — forward", "rectangle", "blue"};
    m_definitions["direction_left"] = {"direction_left", "Direction Left", SignCategory::Direction,
        "", 0.6, 0.6, 2.5, "Direction — left", "rectangle", "blue"};
    m_definitions["direction_right"] = {"direction_right", "Direction Right", SignCategory::Direction,
        "", 0.6, 0.6, 2.5, "Direction — right", "rectangle", "blue"};
    m_definitions["highway_exit"] = {"highway_exit", "Highway Exit", SignCategory::Direction,
        "", 0.6, 0.6, 2.5, "Highway exit", "rectangle", "green"};

    // ─── Priority ───
    m_definitions["priority_road"] = {"priority_road", "Priority Road", SignCategory::Priority,
        "", 0.6, 0.6, 2.5, "Priority road", "diamond", "yellow"};
    m_definitions["give_way"] = {"give_way", "Give Way", SignCategory::Priority,
        "", 0.9, 0.7, 2.5, "Give way", "triangle", "red"};
}

const SignDefinition* SignRegistry::get(const QString& id) const
{
    auto it = m_definitions.find(id);
    if (it == m_definitions.end()) return nullptr;
    return &it.value();
}

QStringList SignRegistry::categoryNames() const
{
    return {"Warning", "Regulatory", "Mandatory", "Prohibition",
            "Information", "Direction", "Pedestrian", "Bicycle",
            "Parking", "Speed", "Priority"};
}

QList<SignDefinition> SignRegistry::byCategory(SignCategory cat) const
{
    QList<SignDefinition> result;
    for (auto it = m_definitions.begin(); it != m_definitions.end(); ++it)
    {
        if (it.value().category == cat)
            result.append(it.value());
    }
    return result;
}

QString SignRegistry::categoryToString(SignCategory cat)
{
    switch (cat) {
    case SignCategory::Warning:      return "Warning";
    case SignCategory::Regulatory:   return "Regulatory";
    case SignCategory::Mandatory:    return "Mandatory";
    case SignCategory::Prohibition:  return "Prohibition";
    case SignCategory::Information:  return "Information";
    case SignCategory::Direction:    return "Direction";
    case SignCategory::Pedestrian:   return "Pedestrian";
    case SignCategory::Bicycle:      return "Bicycle";
    case SignCategory::Parking:      return "Parking";
    case SignCategory::Speed:        return "Speed";
    case SignCategory::Priority:     return "Priority";
    }
    return "Unknown";
}

SignCategory SignRegistry::stringToCategory(const QString& name)
{
    if (name == "Warning")      return SignCategory::Warning;
    if (name == "Regulatory")   return SignCategory::Regulatory;
    if (name == "Mandatory")    return SignCategory::Mandatory;
    if (name == "Prohibition")  return SignCategory::Prohibition;
    if (name == "Information")  return SignCategory::Information;
    if (name == "Direction")    return SignCategory::Direction;
    if (name == "Pedestrian")   return SignCategory::Pedestrian;
    if (name == "Bicycle")      return SignCategory::Bicycle;
    if (name == "Parking")      return SignCategory::Parking;
    if (name == "Speed")        return SignCategory::Speed;
    if (name == "Priority")     return SignCategory::Priority;
    return SignCategory::Warning;
}

void SignRegistry::addSign(const PlacedSign& sign)
{
    m_placedSigns.push_back(sign);
}

void SignRegistry::updateSign(const PlacedSign& sign)
{
    for (auto& s : m_placedSigns)
    {
        if (s.id == sign.id)
        {
            s = sign;
            return;
        }
    }
}

void SignRegistry::removeSign(const std::string& id)
{
    m_placedSigns.erase(
        std::remove_if(m_placedSigns.begin(), m_placedSigns.end(),
            [&id](const PlacedSign& s) { return s.id == id; }),
        m_placedSigns.end());
}

PlacedSign* SignRegistry::findSign(const std::string& id)
{
    for (auto& s : m_placedSigns)
    {
        if (s.id == id) return &s;
    }
    return nullptr;
}

QJsonArray SignRegistry::placedToJson() const
{
    QJsonArray arr;
    for (const auto& sign : m_placedSigns)
        arr.append(sign.toJson());
    return arr;
}

void SignRegistry::placedFromJson(const QJsonArray& json)
{
    m_placedSigns.clear();
    for (const auto& val : json)
        m_placedSigns.push_back(PlacedSign::fromJson(val.toObject()));
}

std::vector<const PlacedSign*> SignRegistry::signsForRoad(const std::string& roadID) const
{
    std::vector<const PlacedSign*> result;
    for (const auto& sign : m_placedSigns)
    {
        if (sign.roadID == roadID)
            result.push_back(&sign);
    }
    return result;
}

// ─── PlacedSign JSON ───────────────────────────────────────

QJsonObject PlacedSign::toJson() const
{
    QJsonObject obj;
    obj["id"] = QString::fromStdString(id);
    obj["signType"] = signType;
    obj["roadID"] = QString::fromStdString(roadID);
    obj["s"] = s;
    obj["tOffset"] = tOffset;
    obj["rotation"] = rotation;
    obj["height"] = height;
    obj["side"] = static_cast<int>(side);
    QJsonObject meta;
    for (const auto& [k, v] : metadata)
        meta[k] = v;
    obj["metadata"] = meta;
    return obj;
}

PlacedSign PlacedSign::fromJson(const QJsonObject& json)
{
    PlacedSign sign;
    sign.id = json["id"].toString().toStdString();
    sign.signType = json["signType"].toString();
    sign.roadID = json["roadID"].toString().toStdString();
    sign.s = json["s"].toDouble();
    sign.tOffset = json["tOffset"].toDouble();
    sign.rotation = json["rotation"].toDouble();
    sign.height = json["height"].toDouble();
    sign.side = static_cast<SignSide>(json["side"].toInt());
    if (json.contains("metadata"))
    {
        auto meta = json["metadata"].toObject();
        for (auto it = meta.begin(); it != meta.end(); ++it)
            sign.metadata[it.key()] = it.value().toString();
    }
    return sign;
}

// ═══════════════════════════════════════════════════════════
// MarkingRegistry
// ═══════════════════════════════════════════════════════════

MarkingRegistry* MarkingRegistry::Instance()
{
    static MarkingRegistry instance;
    return &instance;
}

MarkingRegistry::MarkingRegistry()
{
    registerStandardMarkings();
}

void MarkingRegistry::registerStandardMarkings()
{
    // ─── Lines (longitudinal) ───
    m_definitions[MarkingType::SolidLine] = {MarkingType::SolidLine, "Solid Line",
        "white", 0.15, 0, 0, true, MarkingPattern::Continuous, "paint", "line"};
    m_definitions[MarkingType::DashedLine] = {MarkingType::DashedLine, "Dashed Line",
        "white", 0.15, 3.0, 6.0, true, MarkingPattern::Dashed, "paint", "line"};
    m_definitions[MarkingType::DoubleSolid] = {MarkingType::DoubleSolid, "Double Solid",
        "white", 0.15, 0, 0, true, MarkingPattern::DoubleLine, "paint", "line"};
    m_definitions[MarkingType::DoubleDashed] = {MarkingType::DoubleDashed, "Double Dashed",
        "white", 0.15, 3.0, 6.0, true, MarkingPattern::Dashed, "paint", "line"};
    m_definitions[MarkingType::SolidDashed] = {MarkingType::SolidDashed, "Solid-Dashed",
        "white", 0.15, 3.0, 6.0, true, MarkingPattern::SolidDashed, "paint", "line"};
    m_definitions[MarkingType::DashedSolid] = {MarkingType::DashedSolid, "Dashed-Solid",
        "white", 0.15, 3.0, 6.0, true, MarkingPattern::DashedSolid, "paint", "line"};
    m_definitions[MarkingType::EdgeLine] = {MarkingType::EdgeLine, "Edge Line",
        "white", 0.15, 0, 0, true, MarkingPattern::Continuous, "paint", "line"};
    m_definitions[MarkingType::CenterLine] = {MarkingType::CenterLine, "Center Line",
        "yellow", 0.15, 0, 0, true, MarkingPattern::Continuous, "paint", "line"};
    m_definitions[MarkingType::LaneDivider] = {MarkingType::LaneDivider, "Lane Divider",
        "white", 0.15, 3.0, 6.0, true, MarkingPattern::Dashed, "paint", "line"};
    m_definitions[MarkingType::ShoulderLine] = {MarkingType::ShoulderLine, "Shoulder Line",
        "white", 0.1, 0, 0, true, MarkingPattern::Continuous, "paint", "line"};

    // ─── Transverse ───
    m_definitions[MarkingType::StopLine] = {MarkingType::StopLine, "Stop Line",
        "white", 0.3, 0, 0, false, MarkingPattern::Continuous, "paint", "transverse"};
    m_definitions[MarkingType::YieldLine] = {MarkingType::YieldLine, "Yield Line",
        "white", 0.2, 0, 0, false, MarkingPattern::Continuous, "paint", "transverse"};
    m_definitions[MarkingType::Crosswalk] = {MarkingType::Crosswalk, "Crosswalk",
        "white", 0.4, 0, 0, false, MarkingPattern::Continuous, "paint", "transverse"};
    m_definitions[MarkingType::ZebraCrossing] = {MarkingType::ZebraCrossing, "Zebra Crossing",
        "white", 0.4, 0.5, 0.5, false, MarkingPattern::Dashed, "paint", "transverse"};
    m_definitions[MarkingType::BicycleCrossing] = {MarkingType::BicycleCrossing, "Bicycle Crossing",
        "white", 0.4, 0, 0, false, MarkingPattern::Continuous, "paint", "transverse"};

    // ─── Arrows ───
    m_definitions[MarkingType::ArrowStraight] = {MarkingType::ArrowStraight, "Arrow Straight",
        "white", 0.5, 0, 0, false, MarkingPattern::Continuous, "paint", "arrow"};
    m_definitions[MarkingType::ArrowLeft] = {MarkingType::ArrowLeft, "Arrow Left",
        "white", 0.5, 0, 0, false, MarkingPattern::Continuous, "paint", "arrow"};
    m_definitions[MarkingType::ArrowRight] = {MarkingType::ArrowRight, "Arrow Right",
        "white", 0.5, 0, 0, false, MarkingPattern::Continuous, "paint", "arrow"};
    m_definitions[MarkingType::ArrowStraightLeft] = {MarkingType::ArrowStraightLeft, "Arrow Straight-Left",
        "white", 0.5, 0, 0, false, MarkingPattern::Continuous, "paint", "arrow"};
    m_definitions[MarkingType::ArrowStraightRight] = {MarkingType::ArrowStraightRight, "Arrow Straight-Right",
        "white", 0.5, 0, 0, false, MarkingPattern::Continuous, "paint", "arrow"};
    m_definitions[MarkingType::ArrowUTurn] = {MarkingType::ArrowUTurn, "U-Turn Arrow",
        "white", 0.5, 0, 0, false, MarkingPattern::Continuous, "paint", "arrow"};
    m_definitions[MarkingType::ArrowMerge] = {MarkingType::ArrowMerge, "Merge Arrow",
        "white", 0.5, 0, 0, false, MarkingPattern::Continuous, "paint", "arrow"};
    m_definitions[MarkingType::ArrowDiverge] = {MarkingType::ArrowDiverge, "Diverge Arrow",
        "white", 0.5, 0, 0, false, MarkingPattern::Continuous, "paint", "arrow"};

    // ─── Symbols ───
    m_definitions[MarkingType::SymbolBus] = {MarkingType::SymbolBus, "Bus Symbol",
        "white", 0.5, 0, 0, false, MarkingPattern::Continuous, "paint", "symbol"};
    m_definitions[MarkingType::SymbolBicycle] = {MarkingType::SymbolBicycle, "Bicycle Symbol",
        "white", 0.5, 0, 0, false, MarkingPattern::Continuous, "paint", "symbol"};
    m_definitions[MarkingType::SymbolAccessibility] = {MarkingType::SymbolAccessibility, "Accessibility Symbol",
        "white", 0.5, 0, 0, false, MarkingPattern::Continuous, "paint", "symbol"};
    m_definitions[MarkingType::SymbolParking] = {MarkingType::SymbolParking, "Parking Symbol",
        "white", 0.5, 0, 0, false, MarkingPattern::Continuous, "paint", "symbol"};

    // ─── Areas ───
    m_definitions[MarkingType::HatchedArea] = {MarkingType::HatchedArea, "Hatched Area",
        "white", 0.15, 1.0, 1.5, true, MarkingPattern::Dashed, "paint", "area"};
    m_definitions[MarkingType::ChevronArea] = {MarkingType::ChevronArea, "Chevron Area",
        "white", 0.3, 0, 0, false, MarkingPattern::Continuous, "paint", "area"};
    m_definitions[MarkingType::GoreArea] = {MarkingType::GoreArea, "Gore Area",
        "white", 0.15, 0, 0, true, MarkingPattern::Continuous, "paint", "area"};
    m_definitions[MarkingType::ParkingBay] = {MarkingType::ParkingBay, "Parking Bay",
        "white", 0.1, 1.0, 1.0, true, MarkingPattern::Dashed, "paint", "area"};
    m_definitions[MarkingType::BusStopMarking] = {MarkingType::BusStopMarking, "Bus Stop Marking",
        "white", 0.15, 0.5, 0.5, true, MarkingPattern::Dashed, "paint", "area"};
}

const MarkingDefinition* MarkingRegistry::get(MarkingType type) const
{
    auto it = m_definitions.find(type);
    if (it == m_definitions.end()) return nullptr;
    return &it.value();
}

QString MarkingRegistry::typeToString(MarkingType type)
{
    const auto* def = Instance()->get(type);
    return def ? def->displayName : "Unknown";
}

MarkingType MarkingRegistry::stringToType(const QString& name)
{
    auto& all = Instance()->all();
    for (auto it = all.begin(); it != all.end(); ++it)
    {
        if (it.value().displayName == name)
            return it.value().type;
    }
    return MarkingType::SolidLine;
}

void MarkingRegistry::addMarking(const PlacedMarking& marking)
{
    m_placedMarkings.push_back(marking);
}

void MarkingRegistry::updateMarking(const PlacedMarking& marking)
{
    for (auto& m : m_placedMarkings)
    {
        if (m.id == marking.id)
        {
            m = marking;
            return;
        }
    }
}

void MarkingRegistry::removeMarking(const std::string& id)
{
    m_placedMarkings.erase(
        std::remove_if(m_placedMarkings.begin(), m_placedMarkings.end(),
            [&id](const PlacedMarking& m) { return m.id == id; }),
        m_placedMarkings.end());
}

PlacedMarking* MarkingRegistry::findMarking(const std::string& id)
{
    for (auto& m : m_placedMarkings)
    {
        if (m.id == id) return &m;
    }
    return nullptr;
}

QJsonArray MarkingRegistry::placedToJson() const
{
    QJsonArray arr;
    for (const auto& m : m_placedMarkings)
        arr.append(m.toJson());
    return arr;
}

void MarkingRegistry::placedFromJson(const QJsonArray& json)
{
    m_placedMarkings.clear();
    for (const auto& val : json)
        m_placedMarkings.push_back(PlacedMarking::fromJson(val.toObject()));
}

std::vector<const PlacedMarking*> MarkingRegistry::markingsForRoad(const std::string& roadID) const
{
    std::vector<const PlacedMarking*> result;
    for (const auto& m : m_placedMarkings)
    {
        if (m.roadID == roadID)
            result.push_back(&m);
    }
    return result;
}

// ─── PlacedMarking JSON ────────────────────────────────────

QJsonObject PlacedMarking::toJson() const
{
    QJsonObject obj;
    obj["id"] = QString::fromStdString(id);
    obj["type"] = static_cast<int>(type);
    obj["roadID"] = QString::fromStdString(roadID);
    obj["sStart"] = sStart;
    obj["sEnd"] = sEnd;
    obj["tOffset"] = tOffset;
    obj["width"] = width;
    obj["color"] = color;
    obj["laneAssociation"] = laneAssociation;
    obj["pattern"] = static_cast<int>(pattern);
    obj["material"] = material;
    return obj;
}

PlacedMarking PlacedMarking::fromJson(const QJsonObject& json)
{
    PlacedMarking m;
    m.id = json["id"].toString().toStdString();
    m.type = static_cast<MarkingType>(json["type"].toInt());
    m.roadID = json["roadID"].toString().toStdString();
    m.sStart = json["sStart"].toDouble();
    m.sEnd = json["sEnd"].toDouble();
    m.tOffset = json["tOffset"].toDouble();
    m.width = json["width"].toDouble();
    m.color = json["color"].toString();
    m.laneAssociation = json["laneAssociation"].toInt();
    m.pattern = static_cast<MarkingPattern>(json["pattern"].toInt());
    m.material = json["material"].toString();
    return m;
}

// ═══════════════════════════════════════════════════════════
// FurnitureRegistry
// ═══════════════════════════════════════════════════════════

FurnitureRegistry* FurnitureRegistry::Instance()
{
    static FurnitureRegistry instance;
    return &instance;
}

FurnitureRegistry::FurnitureRegistry()
{
    registerStandardFurniture();
}

void FurnitureRegistry::registerStandardFurniture()
{
    m_definitions["guardrail"] = {"guardrail", "Guardrail", FurnitureType::Guardrail,
        0.3, 0.75, 1.0, "silver", "Metal guardrail along road edge", true};
    m_definitions["barrier_concrete"] = {"barrier_concrete", "Concrete Barrier", FurnitureType::Barrier,
        0.5, 0.9, 1.0, "gray", "Concrete barrier", true};
    m_definitions["barrier_jersey"] = {"barrier_jersey", "Jersey Barrier", FurnitureType::Barrier,
        0.8, 0.8, 1.0, "gray", "Jersey barrier", true};
    m_definitions["bollard"] = {"bollard", "Bollard", FurnitureType::Bollard,
        0.15, 1.0, 0.15, "yellow", "Traffic bollard", false};
    m_definitions["delineator"] = {"delineator", "Delineator Post", FurnitureType::Delineator,
        0.1, 1.0, 0.1, "white", "Delineator post", false};
    m_definitions["street_light"] = {"street_light", "Street Light", FurnitureType::StreetLight,
        0.2, 6.0, 0.2, "gray", "Street light pole", false};
    m_definitions["ped_barrier"] = {"ped_barrier", "Pedestrian Barrier", FurnitureType::PedestrianBarrier,
        0.1, 1.0, 1.0, "gray", "Pedestrian barrier fence", true};
    m_definitions["bus_stop_shelter"] = {"bus_stop_shelter", "Bus Stop Shelter", FurnitureType::BusStop,
        3.0, 2.5, 1.0, "gray", "Bus stop shelter", false};
    m_definitions["traffic_signal"] = {"traffic_signal", "Traffic Signal", FurnitureType::TrafficSignal,
        0.3, 3.5, 0.3, "gray", "Traffic signal pole", false};
    m_definitions["camera"] = {"camera", "Traffic Camera", FurnitureType::Camera,
        0.2, 4.0, 0.2, "gray", "Traffic camera pole", false};
    m_definitions["utility_pole"] = {"utility_pole", "Utility Pole", FurnitureType::UtilityPole,
        0.3, 8.0, 0.3, "brown", "Utility pole", false};
}

const FurnitureDefinition* FurnitureRegistry::get(const QString& id) const
{
    auto it = m_definitions.find(id);
    if (it == m_definitions.end()) return nullptr;
    return &it.value();
}

QStringList FurnitureRegistry::typeNames() const
{
    QStringList names;
    for (auto it = m_definitions.begin(); it != m_definitions.end(); ++it)
        names << it.value().displayName;
    return names;
}

QString FurnitureRegistry::typeToString(FurnitureType type)
{
    switch (type) {
    case FurnitureType::Guardrail:           return "Guardrail";
    case FurnitureType::Barrier:             return "Barrier";
    case FurnitureType::Bollard:             return "Bollard";
    case FurnitureType::Delineator:          return "Delineator";
    case FurnitureType::StreetLight:         return "Street Light";
    case FurnitureType::PedestrianBarrier:   return "Pedestrian Barrier";
    case FurnitureType::BusStop:             return "Bus Stop";
    case FurnitureType::TrafficSignal:       return "Traffic Signal";
    case FurnitureType::Camera:              return "Camera";
    case FurnitureType::UtilityPole:         return "Utility Pole";
    }
    return "Unknown";
}

FurnitureType FurnitureRegistry::stringToType(const QString& name)
{
    if (name == "Guardrail")           return FurnitureType::Guardrail;
    if (name == "Barrier")             return FurnitureType::Barrier;
    if (name == "Bollard")             return FurnitureType::Bollard;
    if (name == "Delineator")          return FurnitureType::Delineator;
    if (name == "Street Light")        return FurnitureType::StreetLight;
    if (name == "Pedestrian Barrier")  return FurnitureType::PedestrianBarrier;
    if (name == "Bus Stop")            return FurnitureType::BusStop;
    if (name == "Traffic Signal")      return FurnitureType::TrafficSignal;
    if (name == "Camera")              return FurnitureType::Camera;
    if (name == "Utility Pole")        return FurnitureType::UtilityPole;
    return FurnitureType::Bollard;
}

void FurnitureRegistry::addFurniture(const PlacedFurniture& furniture)
{
    m_placedFurniture.push_back(furniture);
}

void FurnitureRegistry::updateFurniture(const PlacedFurniture& furniture)
{
    for (auto& f : m_placedFurniture)
    {
        if (f.id == furniture.id)
        {
            f = furniture;
            return;
        }
    }
}

void FurnitureRegistry::removeFurniture(const std::string& id)
{
    m_placedFurniture.erase(
        std::remove_if(m_placedFurniture.begin(), m_placedFurniture.end(),
            [&id](const PlacedFurniture& f) { return f.id == id; }),
        m_placedFurniture.end());
}

PlacedFurniture* FurnitureRegistry::findFurniture(const std::string& id)
{
    for (auto& f : m_placedFurniture)
    {
        if (f.id == id) return &f;
    }
    return nullptr;
}

QJsonArray FurnitureRegistry::placedToJson() const
{
    QJsonArray arr;
    for (const auto& f : m_placedFurniture)
        arr.append(f.toJson());
    return arr;
}

void FurnitureRegistry::placedFromJson(const QJsonArray& json)
{
    m_placedFurniture.clear();
    for (const auto& val : json)
        m_placedFurniture.push_back(PlacedFurniture::fromJson(val.toObject()));
}

std::vector<const PlacedFurniture*> FurnitureRegistry::furnitureForRoad(const std::string& roadID) const
{
    std::vector<const PlacedFurniture*> result;
    for (const auto& f : m_placedFurniture)
    {
        if (f.roadID == roadID)
            result.push_back(&f);
    }
    return result;
}

// ─── PlacedFurniture JSON ──────────────────────────────────

QJsonObject PlacedFurniture::toJson() const
{
    QJsonObject obj;
    obj["id"] = QString::fromStdString(id);
    obj["furnitureType"] = furnitureType;
    obj["roadID"] = QString::fromStdString(roadID);
    obj["sStart"] = sStart;
    obj["sEnd"] = sEnd;
    obj["tOffset"] = tOffset;
    obj["height"] = height;
    obj["side"] = static_cast<int>(side);
    obj["repeatCount"] = repeatCount;
    obj["repeatSpacing"] = repeatSpacing;
    obj["minTurnRadius"] = minTurnRadius;
    return obj;
}

PlacedFurniture PlacedFurniture::fromJson(const QJsonObject& json)
{
    PlacedFurniture f;
    f.id = json["id"].toString().toStdString();
    f.furnitureType = json["furnitureType"].toString();
    f.roadID = json["roadID"].toString().toStdString();
    f.sStart = json["sStart"].toDouble();
    f.sEnd = json["sEnd"].toDouble();
    f.tOffset = json["tOffset"].toDouble();
    f.height = json["height"].toDouble();
    f.side = static_cast<SignSide>(json["side"].toInt());
    f.repeatCount = json["repeatCount"].toInt();
    f.repeatSpacing = json["repeatSpacing"].toDouble();
    f.minTurnRadius = json["minTurnRadius"].toDouble(0.0);
    return f;
}

// ═══════════════════════════════════════════════════════════
// SnapSettings
// ═══════════════════════════════════════════════════════════

SnapSettings* SnapSettings::Instance()
{
    static SnapSettings instance;
    return &instance;
}

SnapSettings::SnapSettings()
{
    // Enable common snap categories by default
    m_enabled = {SnapCategory::Endpoint, SnapCategory::Road, SnapCategory::Lane};
}

void SnapSettings::setEnabled(SnapCategory cat, bool enabled)
{
    if (enabled)
        m_enabled.insert(cat);
    else
        m_enabled.erase(cat);
}

bool SnapSettings::isEnabled(SnapCategory cat) const
{
    return m_enabled.count(cat) > 0;
}

void SnapSettings::toggleAll(bool enabled)
{
    if (enabled)
    {
        m_enabled = {SnapCategory::Endpoint, SnapCategory::Midpoint, SnapCategory::Vertex,
                     SnapCategory::Intersection, SnapCategory::Tangent, SnapCategory::Perpendicular,
                     SnapCategory::Road, SnapCategory::Lane, SnapCategory::Terrain, SnapCategory::Grid};
    }
    else
    {
        m_enabled.clear();
    }
}

std::set<SnapCategory> SnapSettings::enabledCategories() const
{
    return m_enabled;
}

QString SnapSettings::categoryToString(SnapCategory cat)
{
    switch (cat) {
    case SnapCategory::Endpoint:      return "Endpoint";
    case SnapCategory::Midpoint:      return "Midpoint";
    case SnapCategory::Vertex:        return "Vertex";
    case SnapCategory::Intersection:  return "Intersection";
    case SnapCategory::Tangent:       return "Tangent";
    case SnapCategory::Perpendicular: return "Perpendicular";
    case SnapCategory::Road:          return "Road";
    case SnapCategory::Lane:          return "Lane";
    case SnapCategory::Terrain:       return "Terrain";
    case SnapCategory::Grid:          return "Grid";
    }
    return "Unknown";
}

// ═══════════════════════════════════════════════════════════
// MeasurementSystem
// ═══════════════════════════════════════════════════════════

MeasurementSystem* MeasurementSystem::Instance()
{
    static MeasurementSystem instance;
    return &instance;
}

MeasurementResult MeasurementSystem::measureDistance(
    double x1, double y1, double z1,
    double x2, double y2, double z2) const
{
    MeasurementResult r;
    r.type = MeasurementType::Distance;
    double dx = x2 - x1, dy = y2 - y1, dz = z2 - z1;
    r.value = std::sqrt(dx*dx + dy*dy + dz*dz);
    r.unit = "m";
    r.description = QString("Distance: %1 m").arg(r.value, 0, 'f', 2);
    r.points = {{x1,y1,z1}, {x2,y2,z2}};
    return r;
}

MeasurementResult MeasurementSystem::measureAngle(
    double x1, double y1,
    double x2, double y2,
    double x3, double y3) const
{
    MeasurementResult r;
    r.type = MeasurementType::Angle;
    // Angle at p2 between p1-p2 and p3-p2
    double v1x = x1 - x2, v1y = y1 - y2;
    double v2x = x3 - x2, v2y = y3 - y2;
    double dot = v1x*v2x + v1y*v2y;
    double cross = v1x*v2y - v1y*v2x;
    r.value = std::atan2(std::abs(cross), dot) * 180.0 / M_PI;
    r.unit = "deg";
    r.description = QString("Angle: %1 deg").arg(r.value, 0, 'f', 1);
    r.points = {{x1,y1,0}, {x2,y2,0}, {x3,y3,0}};
    return r;
}

MeasurementResult MeasurementSystem::measureArea(
    const std::vector<std::array<double, 2>>& polygon) const
{
    MeasurementResult r;
    r.type = MeasurementType::Area;
    // Shoelace formula
    double area = 0;
    size_t n = polygon.size();
    for (size_t i = 0; i < n; i++)
    {
        size_t j = (i + 1) % n;
        area += polygon[i][0] * polygon[j][1];
        area -= polygon[j][0] * polygon[i][1];
    }
    r.value = std::abs(area) / 2.0;
    r.unit = "m^2";
    r.description = QString("Area: %1 m^2").arg(r.value, 0, 'f', 2);
    return r;
}

MeasurementResult MeasurementSystem::measureStation(
    const std::string& roadID, double s) const
{
    MeasurementResult r;
    r.type = MeasurementType::Station;
    r.value = s;
    r.unit = "m";
    r.description = QString("Road %1, Station: %2 m")
        .arg(QString::fromStdString(roadID))
        .arg(s, 0, 'f', 2);
    return r;
}

MeasurementResult MeasurementSystem::measureCoordinate(
    double x, double y, double z) const
{
    MeasurementResult r;
    r.type = MeasurementType::Coordinate;
    r.description = QString("X: %1  Y: %2  Z: %3")
        .arg(x, 0, 'f', 2).arg(y, 0, 'f', 2).arg(z, 0, 'f', 2);
    r.points = {{x, y, z}};
    return r;
}

MeasurementResult MeasurementSystem::measureRoadLength(
    const std::string& roadID) const
{
    MeasurementResult r;
    r.type = MeasurementType::RoadLength;
    auto* world = World::Instance();
    if (world)
    {
        for (auto& road : world->allRoads)
        {
            if (road && road->ID() == roadID)
            {
                r.value = road->Length();
                break;
            }
        }
    }
    r.unit = "m";
    r.description = QString("Road %1 Length: %2 m")
        .arg(QString::fromStdString(roadID))
        .arg(r.value, 0, 'f', 2);
    return r;
}

MeasurementResult MeasurementSystem::measureRadius(
    double x1, double y1,
    double x2, double y2,
    double x3, double y3) const
{
    MeasurementResult r;
    r.type = MeasurementType::Radius;
    // Circumradius of triangle
    double a = std::sqrt((x2-x3)*(x2-x3) + (y2-y3)*(y2-y3));
    double b = std::sqrt((x1-x3)*(x1-x3) + (y1-y3)*(y1-y3));
    double c = std::sqrt((x1-x2)*(x1-x2) + (y1-y2)*(y1-y2));
    double s = (a + b + c) / 2.0;
    double area = std::sqrt(std::max(0.0, s*(s-a)*(s-b)*(s-c)));
    if (area < 1e-9)
    {
        r.value = 0;
        r.description = "Radius: N/A (collinear points)";
    }
    else
    {
        r.value = (a * b * c) / (4.0 * area);
        r.description = QString("Radius: %1 m").arg(r.value, 0, 'f', 2);
    }
    r.unit = "m";
    r.points = {{x1,y1,0}, {x2,y2,0}, {x3,y3,0}};
    return r;
}

QString MeasurementSystem::typeToString(MeasurementType type)
{
    switch (type) {
    case MeasurementType::Distance:    return "Distance";
    case MeasurementType::Angle:       return "Angle";
    case MeasurementType::Area:        return "Area";
    case MeasurementType::Station:     return "Station";
    case MeasurementType::Coordinate:  return "Coordinate";
    case MeasurementType::RoadLength:  return "Road Length";
    case MeasurementType::Radius:      return "Radius";
    }
    return "Unknown";
}

} // namespace LM
