#include <napi.h>
#include "road/geometry.hpp"
#include "road/road.hpp"
#include "road/arc.hpp"
#include "road/intersection.hpp"
#include "road/clothoid.hpp"
#include "road/mesh.hpp"
#include "road/opendrive.hpp"
#include "road/road_tools.hpp"
#include "road/road_adapter.hpp"
#include "road/lane_network.hpp"
#include "road/road_mark_generator.hpp"
#include "road/road_mesh_generator.hpp"
#include "road/road_graph.hpp"
#include "road/lane_graph.hpp"
#include "road/junction_builder.hpp"
#include <sstream>
#include <iostream>

// ─── Logging ───────────────────────────────────────────────
#ifdef _WIN32
#include <windows.h>
#endif

static void roadLog(const std::string& msg) {
    std::cerr << "[C++ RoadEngine] " << msg << std::endl;
#ifdef _WIN32
    std::string full = "[C++ RoadEngine] " + msg + "\n";
    OutputDebugStringA(full.c_str());
#endif
}

#define ROAD_LOG(msg) roadLog(msg)

namespace geo {

// ─── Helpers: JS ↔ C++ conversion ──────────────────────────

// ─── SegmentMetadata serialization ─────────────────────────
// segmentMeta is serialized as a nested object on each control point:
//   { kind: "arc"|"spiral"|"line"|"bezier", version: 1,
//     startHeading, curvature, arcLength,
//     curvatureStart, curvatureEnd, segmentLength }
//
// Old files without segmentMeta → std::nullopt (legacy adapter path)
// New files with segmentMeta   → exact adapter path

static const char* segmentKindToString(geo::SegmentKind kind) {
    switch (kind) {
    case geo::SegmentKind::Line:   return "line";
    case geo::SegmentKind::Bezier: return "bezier";
    case geo::SegmentKind::Arc:    return "arc";
    case geo::SegmentKind::Spiral: return "spiral";
    }
    return "line";
}

static std::optional<geo::SegmentKind> stringToSegmentKind(const std::string& s) {
    if (s == "line")   return geo::SegmentKind::Line;
    if (s == "bezier") return geo::SegmentKind::Bezier;
    if (s == "arc")    return geo::SegmentKind::Arc;
    if (s == "spiral") return geo::SegmentKind::Spiral;
    return std::nullopt;
}

// Serialize SegmentMetadata to a JS object
static Napi::Object segmentMetaToJs(Napi::Env env, const geo::SegmentMetadata& meta) {
    auto obj = Napi::Object::New(env);
    obj.Set("kind", Napi::String::New(env, segmentKindToString(meta.kind)));
    obj.Set("version", Napi::Number::New(env, meta.version));
    obj.Set("startHeading", Napi::Number::New(env, meta.startHeading));
    obj.Set("curvature", Napi::Number::New(env, meta.curvature));
    obj.Set("arcLength", Napi::Number::New(env, meta.arcLength));
    obj.Set("curvatureStart", Napi::Number::New(env, meta.curvatureStart));
    obj.Set("curvatureEnd", Napi::Number::New(env, meta.curvatureEnd));
    obj.Set("segmentLength", Napi::Number::New(env, meta.segmentLength));
    return obj;
}

// Parse SegmentMetadata from a JS object (returns nullopt if invalid)
static std::optional<geo::SegmentMetadata> parseSegmentMeta(const Napi::Value& val) {
    if (val.IsNull() || val.IsUndefined() || !val.IsObject()) {
        return std::nullopt;
    }
    auto obj = val.As<Napi::Object>();
    if (!obj.Has("kind")) return std::nullopt;

    auto kindStr = obj.Get("kind").As<Napi::String>().Utf8Value();
    auto kind = stringToSegmentKind(kindStr);
    if (!kind.has_value()) return std::nullopt;

    geo::SegmentMetadata meta;
    meta.kind = *kind;
    meta.version = obj.Has("version") ? obj.Get("version").As<Napi::Number>().Int32Value() : 1;
    meta.startHeading = obj.Has("startHeading") ? obj.Get("startHeading").As<Napi::Number>().DoubleValue() : 0.0;
    meta.curvature = obj.Has("curvature") ? obj.Get("curvature").As<Napi::Number>().DoubleValue() : 0.0;
    meta.arcLength = obj.Has("arcLength") ? obj.Get("arcLength").As<Napi::Number>().DoubleValue() : 0.0;
    meta.curvatureStart = obj.Has("curvatureStart") ? obj.Get("curvatureStart").As<Napi::Number>().DoubleValue() : 0.0;
    meta.curvatureEnd = obj.Has("curvatureEnd") ? obj.Get("curvatureEnd").As<Napi::Number>().DoubleValue() : 0.0;
    meta.segmentLength = obj.Has("segmentLength") ? obj.Get("segmentLength").As<Napi::Number>().DoubleValue() : 0.0;
    return meta;
}

// Parse a JS road object { id, name, width, laneCount, points: [...] }
static Road parseRoad(const Napi::Object& obj) {
    Road road;
    road.id = obj.Get("id").As<Napi::String>().Utf8Value();
    road.name = obj.Has("name") ? obj.Get("name").As<Napi::String>().Utf8Value() : "Road";
    road.width = obj.Has("width") ? obj.Get("width").As<Napi::Number>().DoubleValue() : 8.0;
    road.laneCount = obj.Has("laneCount") ? obj.Get("laneCount").As<Napi::Number>().Int32Value() : 2;
    road.formatVersion = obj.Has("formatVersion") ? obj.Get("formatVersion").As<Napi::Number>().Int32Value() : 1;

    auto ptsArr = obj.Get("points").As<Napi::Array>();
    for (uint32_t i = 0; i < ptsArr.Length(); i++) {
        auto ptObj = ptsArr.Get(i).As<Napi::Object>();
        ControlPoint cp;
        cp.position.x = ptObj.Get("x").As<Napi::Number>().DoubleValue();
        cp.position.y = ptObj.Get("y").As<Napi::Number>().DoubleValue();
        cp.z = ptObj.Has("z") ? ptObj.Get("z").As<Napi::Number>().DoubleValue() : 0.0;
        cp.type = ptObj.Has("type") ? ptObj.Get("type").As<Napi::String>().Utf8Value() : "corner";
        if (ptObj.Has("handleIn") && !ptObj.Get("handleIn").IsNull()) {
            auto h = ptObj.Get("handleIn").As<Napi::Object>();
            cp.handleIn = {h.Get("x").As<Napi::Number>().DoubleValue(),
                           h.Get("y").As<Napi::Number>().DoubleValue()};
            cp.hasHandleIn = true;
        }
        if (ptObj.Has("handleOut") && !ptObj.Get("handleOut").IsNull()) {
            auto h = ptObj.Get("handleOut").As<Napi::Object>();
            cp.handleOut = {h.Get("x").As<Napi::Number>().DoubleValue(),
                            h.Get("y").As<Napi::Number>().DoubleValue()};
            cp.hasHandleOut = true;
        }
        // Parse optional segment metadata (Phase 1.8.3d)
        if (ptObj.Has("segmentMeta") && !ptObj.Get("segmentMeta").IsNull()) {
            cp.segmentMeta = parseSegmentMeta(ptObj.Get("segmentMeta"));
        }
        road.points.push_back(std::move(cp));
    }
    return road;
}

// Serialize a Point2D to a JS object
static Napi::Object pointToJs(Napi::Env env, const Point2D& p) {
    auto obj = Napi::Object::New(env);
    obj.Set("x", Napi::Number::New(env, p.x));
    obj.Set("y", Napi::Number::New(env, p.y));
    return obj;
}

// Serialize a vector of Point2D to a JS array
static Napi::Array pointsToJs(Napi::Env env, const std::vector<Point2D>& pts) {
    auto arr = Napi::Array::New(env, pts.size());
    for (size_t i = 0; i < pts.size(); i++) {
        arr.Set(i, pointToJs(env, pts[i]));
    }
    return arr;
}

// Serialize a CircleArc to a JS object
static Napi::Object arcToJs(Napi::Env env, const CircleArc& arc) {
    auto obj = Napi::Object::New(env);
    obj.Set("center", pointToJs(env, arc.center));
    obj.Set("radius", Napi::Number::New(env, arc.radius));
    obj.Set("startAngle", Napi::Number::New(env, arc.startAngle));
    obj.Set("endAngle", Napi::Number::New(env, arc.endAngle));
    obj.Set("sweep", Napi::Number::New(env, arc.sweep));
    obj.Set("points", pointsToJs(env, arc.points));
    obj.Set("tangentIn", pointToJs(env, arc.tangentIn));
    obj.Set("tangentOut", pointToJs(env, arc.tangentOut));
    return obj;
}

// Serialize a GeneratedIntersection to a JS object
static Napi::Object intersectionToJs(Napi::Env env, const GeneratedIntersection& ix) {
    auto obj = Napi::Object::New(env);
    obj.Set("center", pointToJs(env, ix.center));
    obj.Set("polygon", pointsToJs(env, ix.polygon));

    // Approaches
    auto approaches = Napi::Array::New(env, ix.approaches.size());
    for (size_t i = 0; i < ix.approaches.size(); i++) {
        auto aObj = Napi::Object::New(env);
        aObj.Set("roadId", Napi::String::New(env, ix.approaches[i].roadId));
        aObj.Set("direction", Napi::String::New(env, ix.approaches[i].direction));
        aObj.Set("centerline", pointsToJs(env, ix.approaches[i].centerline));
        aObj.Set("width", Napi::Number::New(env, ix.approaches[i].width));
        aObj.Set("laneCount", Napi::Number::New(env, ix.approaches[i].laneCount));
        aObj.Set("z", Napi::Number::New(env, ix.approaches[i].z));
        approaches.Set(i, aObj);
    }
    obj.Set("approaches", approaches);

    // Lane connections
    auto connections = Napi::Array::New(env, ix.laneConnections.size());
    for (size_t i = 0; i < ix.laneConnections.size(); i++) {
        auto cObj = Napi::Object::New(env);
        cObj.Set("fromApproach", Napi::String::New(env, ix.laneConnections[i].fromApproach));
        cObj.Set("toApproach", Napi::String::New(env, ix.laneConnections[i].toApproach));
        cObj.Set("type", Napi::String::New(env, ix.laneConnections[i].type));
        cObj.Set("path", pointsToJs(env, ix.laneConnections[i].path));
        connections.Set(i, cObj);
    }
    obj.Set("laneConnections", connections);

    // Stop lines
    auto stopLines = Napi::Array::New(env, ix.stopLines.size());
    for (size_t i = 0; i < ix.stopLines.size(); i++) {
        auto sObj = Napi::Object::New(env);
        sObj.Set("approach", Napi::String::New(env, ix.stopLines[i].approach));
        sObj.Set("p1", pointToJs(env, ix.stopLines[i].p1));
        sObj.Set("p2", pointToJs(env, ix.stopLines[i].p2));
        stopLines.Set(i, sObj);
    }
    obj.Set("stopLines", stopLines);

    // Crosswalks
    auto crosswalks = Napi::Array::New(env, ix.crosswalks.size());
    for (size_t i = 0; i < ix.crosswalks.size(); i++) {
        auto cObj = Napi::Object::New(env);
        cObj.Set("approach", Napi::String::New(env, ix.crosswalks[i].approach));
        cObj.Set("corners", pointsToJs(env, ix.crosswalks[i].corners));
        crosswalks.Set(i, cObj);
    }
    obj.Set("crosswalks", crosswalks);

    // ─── Construction debug data ───────────────────────────
    obj.Set("cornerRadius", Napi::Number::New(env, ix.cornerRadius));
    obj.Set("trimDistance1", Napi::Number::New(env, ix.trimDistance1));
    obj.Set("trimDistance2", Napi::Number::New(env, ix.trimDistance2));
    obj.Set("intersectionAngle", Napi::Number::New(env, ix.intersectionAngle));

    // Boundary intersections
    obj.Set("boundaryIntersections", pointsToJs(env, ix.boundaryIntersections));

    // Trim lines
    auto trimLines = Napi::Array::New(env, ix.trimLines.size());
    for (size_t i = 0; i < ix.trimLines.size(); i++) {
        auto tObj = Napi::Object::New(env);
        tObj.Set("leftEnd", pointToJs(env, ix.trimLines[i].leftEnd));
        tObj.Set("rightEnd", pointToJs(env, ix.trimLines[i].rightEnd));
        tObj.Set("centerPt", pointToJs(env, ix.trimLines[i].centerPt));
        tObj.Set("approachIdx", Napi::Number::New(env, ix.trimLines[i].approachIdx));
        trimLines.Set(i, tObj);
    }
    obj.Set("trimLines", trimLines);

    // Fillet corners
    auto corners = Napi::Array::New(env, ix.corners.size());
    for (size_t i = 0; i < ix.corners.size(); i++) {
        const auto& c = ix.corners[i];
        auto cObj = Napi::Object::New(env);
        cObj.Set("boundaryIntersection", pointToJs(env, c.boundaryIntersection));
        cObj.Set("tangentIn", pointToJs(env, c.tangentIn));
        cObj.Set("tangentOut", pointToJs(env, c.tangentOut));
        cObj.Set("arcCenter", pointToJs(env, c.arcCenter));
        cObj.Set("radius", Napi::Number::New(env, c.radius));
        cObj.Set("arcPoints", pointsToJs(env, c.arcPoints));
        cObj.Set("approachInIdx", Napi::Number::New(env, c.approachInIdx));
        cObj.Set("approachOutIdx", Napi::Number::New(env, c.approachOutIdx));
        corners.Set(i, cObj);
    }
    obj.Set("corners", corners);

    return obj;
}

// Serialize a Road to a JS object (for tool creation results)
static Napi::Object roadToJs(Napi::Env env, const Road& road) {
    auto obj = Napi::Object::New(env);
    obj.Set("id", Napi::String::New(env, road.id));
    obj.Set("name", Napi::String::New(env, road.name));
    obj.Set("width", Napi::Number::New(env, road.width));
    obj.Set("laneCount", Napi::Number::New(env, road.laneCount));
    obj.Set("formatVersion", Napi::Number::New(env, road.formatVersion));

    auto ptsArr = Napi::Array::New(env, road.points.size());
    for (size_t i = 0; i < road.points.size(); i++) {
        auto ptObj = Napi::Object::New(env);
        ptObj.Set("x", Napi::Number::New(env, road.points[i].position.x));
        ptObj.Set("y", Napi::Number::New(env, road.points[i].position.y));
        ptObj.Set("z", Napi::Number::New(env, road.points[i].z));
        ptObj.Set("type", Napi::String::New(env, road.points[i].type));
        ptObj.Set("id", Napi::String::New(env, road.points[i].id));
        if (road.points[i].hasHandleIn) {
            auto h = Napi::Object::New(env);
            h.Set("x", Napi::Number::New(env, road.points[i].handleIn.x));
            h.Set("y", Napi::Number::New(env, road.points[i].handleIn.y));
            ptObj.Set("handleIn", h);
        } else {
            ptObj.Set("handleIn", env.Null());
        }
        if (road.points[i].hasHandleOut) {
            auto h = Napi::Object::New(env);
            h.Set("x", Napi::Number::New(env, road.points[i].handleOut.x));
            h.Set("y", Napi::Number::New(env, road.points[i].handleOut.y));
            ptObj.Set("handleOut", h);
        } else {
            ptObj.Set("handleOut", env.Null());
        }
        // Serialize segment metadata if present (Phase 1.8.3d)
        if (road.points[i].segmentMeta.has_value()) {
            ptObj.Set("segmentMeta", segmentMetaToJs(env, *road.points[i].segmentMeta));
        } else {
            ptObj.Set("segmentMeta", env.Null());
        }
        ptsArr.Set(i, ptObj);
    }
    obj.Set("points", ptsArr);
    return obj;
}

// Parse a Point2D from a JS object { x, y }
static Point2D parsePoint(const Napi::Value& val) {
    auto obj = val.As<Napi::Object>();
    return {obj.Get("x").As<Napi::Number>().DoubleValue(),
            obj.Get("y").As<Napi::Number>().DoubleValue()};
}

// Parse an array of Point2D from a JS array
static std::vector<Point2D> parsePoints(const Napi::Value& val) {
    std::vector<Point2D> pts;
    auto arr = val.As<Napi::Array>();
    pts.reserve(arr.Length());
    for (uint32_t i = 0; i < arr.Length(); i++) {
        pts.push_back(parsePoint(arr.Get(i)));
    }
    return pts;
}

// Parse RoadToolParams from a JS object (optional)
static RoadToolParams parseToolParams(const Napi::Value& val) {
    RoadToolParams params;
    if (val.IsObject()) {
        auto obj = val.As<Napi::Object>();
        if (obj.Has("width")) params.width = obj.Get("width").As<Napi::Number>().DoubleValue();
        if (obj.Has("laneCount")) params.laneCount = obj.Get("laneCount").As<Napi::Number>().Int32Value();
        if (obj.Has("profileName")) params.profileName = obj.Get("profileName").As<Napi::String>().Utf8Value();
        if (obj.Has("z")) params.z = obj.Get("z").As<Napi::Number>().DoubleValue();
    }
    return params;
}

// ─── N-API functions ───────────────────────────────────────

// roadGetVersion() → string
static Napi::Value RoadGetVersion(const Napi::CallbackInfo& info) {
    ROAD_LOG("roadGetVersion() called");
    return Napi::String::New(info.Env(), "2.0.0-road-engine");
}

// roadGenerateIntersection(road1, road2, refLat, refLon) → GeneratedIntersection
static Napi::Value RoadGenerateIntersection(const Napi::CallbackInfo& info) {
    ROAD_LOG("roadGenerateIntersection() CALLED — C++ engine is running");
    Napi::Env env = info.Env();
    if (info.Length() < 4 || !info[0].IsObject() || !info[1].IsObject()) {
        Napi::TypeError::New(env, "Expected (road1, road2, refLat, refLon)").ThrowAsJavaScriptException();
        return env.Null();
    }

    Road road1 = parseRoad(info[0].As<Napi::Object>());
    Road road2 = parseRoad(info[1].As<Napi::Object>());
    double refLat = info[2].As<Napi::Number>().DoubleValue();
    double refLon = info[3].As<Napi::Number>().DoubleValue();

    {
        std::ostringstream oss;
        oss << "road1: " << road1.id << " pts: " << road1.points.size()
            << " road2: " << road2.id << " pts: " << road2.points.size();
        ROAD_LOG(oss.str());
    }

    GeneratedIntersection ix = generateIntersection(road1, road2, refLat, refLon);

    {
        std::ostringstream oss;
        oss << "result center: (" << ix.center.x << "," << ix.center.y << ")"
            << " polygon pts: " << ix.polygon.size()
            << " approaches: " << ix.approaches.size()
            << " lane connections: " << ix.laneConnections.size();
        ROAD_LOG(oss.str());
    }

    return intersectionToJs(env, ix);
}

// roadComputeCircleArc(startPoint, startDirection, endPoint, segments?) → CircleArc
static Napi::Value RoadComputeCircleArc(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 3) {
        Napi::TypeError::New(env, "Expected (startPoint, startDirection, endPoint)").ThrowAsJavaScriptException();
        return env.Null();
    }

    auto spObj = info[0].As<Napi::Object>();
    Point2D startPoint = {spObj.Get("x").As<Napi::Number>().DoubleValue(),
                          spObj.Get("y").As<Napi::Number>().DoubleValue()};

    auto sdObj = info[1].As<Napi::Object>();
    Vec2 startDirection = {sdObj.Get("x").As<Napi::Number>().DoubleValue(),
                           sdObj.Get("y").As<Napi::Number>().DoubleValue()};

    auto epObj = info[2].As<Napi::Object>();
    Point2D endPoint = {epObj.Get("x").As<Napi::Number>().DoubleValue(),
                        epObj.Get("y").As<Napi::Number>().DoubleValue()};

    int segments = (info.Length() >= 4 && info[3].IsNumber())
        ? info[3].As<Napi::Number>().Int32Value() : 32;

    CircleArc arc = computeCircleArc(startPoint, startDirection, endPoint, segments);
    return arcToJs(env, arc);
}

// roadSampleCenterline(road, numSamples?) → Point2D[]
static Napi::Value RoadSampleCenterline(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsObject()) {
        Napi::TypeError::New(env, "Expected (road)").ThrowAsJavaScriptException();
        return env.Null();
    }

    Road road = parseRoad(info[0].As<Napi::Object>());
    int numSamples = (info.Length() >= 2 && info[1].IsNumber())
        ? info[1].As<Napi::Number>().Int32Value() : 24;

    auto samples = road.sampleCenterline(numSamples);
    return pointsToJs(env, samples);
}

// roadGeoToLocal(lat, lon, refLat, refLon) → {x, y}
static Napi::Value RoadGeoToLocal(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 4) {
        Napi::TypeError::New(env, "Expected (lat, lon, refLat, refLon)").ThrowAsJavaScriptException();
        return env.Null();
    }

    double lat = info[0].As<Napi::Number>().DoubleValue();
    double lon = info[1].As<Napi::Number>().DoubleValue();
    double refLat = info[2].As<Napi::Number>().DoubleValue();
    double refLon = info[3].As<Napi::Number>().DoubleValue();

    Point2D p = geoToLocal(lat, lon, refLat, refLon);
    return pointToJs(env, p);
}

// roadLocalToGeo(x, y, refLat, refLon) → {lat, lon}
static Napi::Value RoadLocalToGeo(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 4) {
        Napi::TypeError::New(env, "Expected (x, y, refLat, refLon)").ThrowAsJavaScriptException();
        return env.Null();
    }

    double x = info[0].As<Napi::Number>().DoubleValue();
    double y = info[1].As<Napi::Number>().DoubleValue();
    double refLat = info[2].As<Napi::Number>().DoubleValue();
    double refLon = info[3].As<Napi::Number>().DoubleValue();

    Point2D p = localToGeo(x, y, refLat, refLon);
    auto obj = Napi::Object::New(env);
    obj.Set("lat", Napi::Number::New(env, p.x));
    obj.Set("lon", Napi::Number::New(env, p.y));
    return obj;
}

// roadComputeClothoid(startPoint, startDirection, endPoint, endDirection, initialA?, segments?) → ClothoidResult
static Napi::Value RoadComputeClothoid(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 4) {
        Napi::TypeError::New(env, "Expected (startPoint, startDirection, endPoint, endDirection)").ThrowAsJavaScriptException();
        return env.Null();
    }

    auto spObj = info[0].As<Napi::Object>();
    Point2D startPoint = {spObj.Get("x").As<Napi::Number>().DoubleValue(),
                          spObj.Get("y").As<Napi::Number>().DoubleValue()};

    auto sdObj = info[1].As<Napi::Object>();
    Vec2 startDirection = {sdObj.Get("x").As<Napi::Number>().DoubleValue(),
                           sdObj.Get("y").As<Napi::Number>().DoubleValue()};

    auto epObj = info[2].As<Napi::Object>();
    Point2D endPoint = {epObj.Get("x").As<Napi::Number>().DoubleValue(),
                        epObj.Get("y").As<Napi::Number>().DoubleValue()};

    auto edObj = info[3].As<Napi::Object>();
    Vec2 endDirection = {edObj.Get("x").As<Napi::Number>().DoubleValue(),
                         edObj.Get("y").As<Napi::Number>().DoubleValue()};

    double initialA = (info.Length() >= 5 && info[4].IsNumber())
        ? info[4].As<Napi::Number>().DoubleValue() : 50.0;
    int segments = (info.Length() >= 6 && info[5].IsNumber())
        ? info[5].As<Napi::Number>().Int32Value() : 64;

    ClothoidResult result = fitClothoid(startPoint, startDirection,
                                         endPoint, endDirection, initialA, segments);

    auto obj = Napi::Object::New(env);
    obj.Set("points", pointsToJs(env, result.points));
    obj.Set("tangentIn", pointToJs(env, result.tangentIn));
    obj.Set("tangentOut", pointToJs(env, result.tangentOut));
    obj.Set("totalAngle", Napi::Number::New(env, result.totalAngle));
    obj.Set("A", Napi::Number::New(env, result.params.A));
    obj.Set("L", Napi::Number::New(env, result.params.L));
    obj.Set("isLeftTurn", Napi::Boolean::New(env, result.params.isLeftTurn));
    return obj;
}

// Serialize MeshData to a JS object
static Napi::Object meshToJs(Napi::Env env, const MeshData& mesh) {
    auto obj = Napi::Object::New(env);

    // Vertices as Float32Array
    auto verts = Napi::Float32Array::New(env, mesh.vertices.size());
    for (size_t i = 0; i < mesh.vertices.size(); i++) {
        verts[i] = static_cast<float>(mesh.vertices[i]);
    }
    obj.Set("vertices", verts);

    // Normals as Float32Array
    auto norms = Napi::Float32Array::New(env, mesh.normals.size());
    for (size_t i = 0; i < mesh.normals.size(); i++) {
        norms[i] = static_cast<float>(mesh.normals[i]);
    }
    obj.Set("normals", norms);

    // UVs as Float32Array
    auto uvs = Napi::Float32Array::New(env, mesh.uvs.size());
    for (size_t i = 0; i < mesh.uvs.size(); i++) {
        uvs[i] = static_cast<float>(mesh.uvs[i]);
    }
    obj.Set("uvs", uvs);

    // Indices as Uint32Array
    auto indices = Napi::Uint32Array::New(env, mesh.indices.size());
    for (size_t i = 0; i < mesh.indices.size(); i++) {
        indices[i] = mesh.indices[i];
    }
    obj.Set("indices", indices);

    // Metadata
    obj.Set("vertexCount", Napi::Number::New(env, mesh.vertices.size() / 3));
    obj.Set("indexCount", Napi::Number::New(env, mesh.indices.size()));
    obj.Set("triangleCount", Napi::Number::New(env, mesh.indices.size() / 3));

    return obj;
}

// roadGenerateRoadMesh(road, numSamples?) → MeshData
static Napi::Value RoadGenerateRoadMesh(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsObject()) {
        Napi::TypeError::New(env, "Expected (road)").ThrowAsJavaScriptException();
        return env.Null();
    }

    Road road = parseRoad(info[0].As<Napi::Object>());
    int numSamples = (info.Length() >= 2 && info[1].IsNumber())
        ? info[1].As<Napi::Number>().Int32Value() : 32;

    MeshData mesh = generateRoadMesh(road, numSamples);
    return meshToJs(env, mesh);
}

// roadGenerateIntersectionMesh(intersection, z?) → MeshData
static Napi::Value RoadGenerateIntersectionMesh(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsObject()) {
        Napi::TypeError::New(env, "Expected (intersection)").ThrowAsJavaScriptException();
        return env.Null();
    }

    auto ixObj = info[0].As<Napi::Object>();
    GeneratedIntersection ix;

    // Parse intersection from JS
    auto centerObj = ixObj.Get("center").As<Napi::Object>();
    ix.center = {centerObj.Get("x").As<Napi::Number>().DoubleValue(),
                 centerObj.Get("y").As<Napi::Number>().DoubleValue()};

    auto polyArr = ixObj.Get("polygon").As<Napi::Array>();
    for (uint32_t i = 0; i < polyArr.Length(); i++) {
        auto p = polyArr.Get(i).As<Napi::Object>();
        ix.polygon.push_back({p.Get("x").As<Napi::Number>().DoubleValue(),
                              p.Get("y").As<Napi::Number>().DoubleValue()});
    }

    double z = (info.Length() >= 2 && info[1].IsNumber())
        ? info[1].As<Napi::Number>().DoubleValue() : 0.0;

    MeshData mesh = generateIntersectionMesh(ix, z);
    return meshToJs(env, mesh);
}

// roadExportOpenDrive(roads[], refLat, refLon) → string (XML)
static Napi::Value RoadExportOpenDrive(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 3 || !info[0].IsArray()) {
        Napi::TypeError::New(env, "Expected (roads: array, refLat, refLon)").ThrowAsJavaScriptException();
        return env.Null();
    }

    auto roadsArr = info[0].As<Napi::Array>();
    double refLat = info[1].As<Napi::Number>().DoubleValue();
    double refLon = info[2].As<Napi::Number>().DoubleValue();

    std::vector<Road> roads;
    for (uint32_t i = 0; i < roadsArr.Length(); i++) {
        roads.push_back(parseRoad(roadsArr.Get(i).As<Napi::Object>()));
    }

    std::string xml = exportOpenDrive(roads, refLat, refLon);
    return Napi::String::New(env, xml);
}

// ─── Init function ─────────────────────────────────────────
// ─── Road Tool Creation Functions (SCANeR-style) ──────────

// roadCreateSegment(startX, startY, endX, endY, params?) → Road
static Napi::Value RoadCreateSegment(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 4) {
        Napi::TypeError::New(env, "Expected (startX, startY, endX, endY, params?)").ThrowAsJavaScriptException();
        return env.Null();
    }
    Point2D start{info[0].As<Napi::Number>().DoubleValue(), info[1].As<Napi::Number>().DoubleValue()};
    Point2D end{info[2].As<Napi::Number>().DoubleValue(), info[3].As<Napi::Number>().DoubleValue()};
    RoadToolParams params;
    if (info.Length() >= 5) params = parseToolParams(info[4]);
    Road road = createSegment(start, end, params);
    return roadToJs(env, road);
}

// roadCreateCircleArc(startX, startY, dirX, dirY, endX, endY, numCPs?, params?) → Road
static Napi::Value RoadCreateCircleArc(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 6) {
        Napi::TypeError::New(env, "Expected (startX, startY, dirX, dirY, endX, endY, ...)").ThrowAsJavaScriptException();
        return env.Null();
    }
    Point2D start{info[0].As<Napi::Number>().DoubleValue(), info[1].As<Napi::Number>().DoubleValue()};
    Point2D dir{info[2].As<Napi::Number>().DoubleValue(), info[3].As<Napi::Number>().DoubleValue()};
    Point2D end{info[4].As<Napi::Number>().DoubleValue(), info[5].As<Napi::Number>().DoubleValue()};
    int numCPs = 8;
    if (info.Length() >= 7 && info[6].IsNumber()) numCPs = info[6].As<Napi::Number>().Int32Value();
    RoadToolParams params;
    if (info.Length() >= 8) params = parseToolParams(info[7]);
    Road road = createCircleArc(start, dir, end, numCPs, params);
    return roadToJs(env, road);
}

// roadCreateClothoidArc(startX, startY, dirX, dirY, endX, endY, endDirX, endDirY, numCPs?, params?) → Road
static Napi::Value RoadCreateClothoidArc(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 8) {
        Napi::TypeError::New(env, "Expected (startX, startY, dirX, dirY, endX, endY, endDirX, endDirY, ...)").ThrowAsJavaScriptException();
        return env.Null();
    }
    Point2D start{info[0].As<Napi::Number>().DoubleValue(), info[1].As<Napi::Number>().DoubleValue()};
    Point2D dir{info[2].As<Napi::Number>().DoubleValue(), info[3].As<Napi::Number>().DoubleValue()};
    Point2D end{info[4].As<Napi::Number>().DoubleValue(), info[5].As<Napi::Number>().DoubleValue()};
    Point2D endDir{info[6].As<Napi::Number>().DoubleValue(), info[7].As<Napi::Number>().DoubleValue()};
    int numCPs = 8;
    if (info.Length() >= 9 && info[8].IsNumber()) numCPs = info[8].As<Napi::Number>().Int32Value();
    RoadToolParams params;
    if (info.Length() >= 10) params = parseToolParams(info[9]);
    Road road = createClothoidArc(start, dir, end, endDir, numCPs, params);
    return roadToJs(env, road);
}

// roadCreatePolyline(points[], filletRadius?, filletSegments?, params?) → Road
static Napi::Value RoadCreatePolyline(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsArray()) {
        Napi::TypeError::New(env, "Expected (points[], filletRadius?, ...)").ThrowAsJavaScriptException();
        return env.Null();
    }
    auto pts = parsePoints(info[0]);
    double filletR = 0.0;
    if (info.Length() >= 2 && info[1].IsNumber()) filletR = info[1].As<Napi::Number>().DoubleValue();
    int filletSegs = 6;
    if (info.Length() >= 3 && info[2].IsNumber()) filletSegs = info[2].As<Napi::Number>().Int32Value();
    RoadToolParams params;
    if (info.Length() >= 4) params = parseToolParams(info[3]);
    Road road = createPolyline(pts, filletR, filletSegs, params);
    return roadToJs(env, road);
}

// roadCreateBezier(startX, startY, handleOutX, handleOutY, endX, endY, handleInX, handleInY, params?) → Road
static Napi::Value RoadCreateBezier(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 8) {
        Napi::TypeError::New(env, "Expected (startX, startY, handleOutX, handleOutY, endX, endY, handleInX, handleInY, params?)").ThrowAsJavaScriptException();
        return env.Null();
    }
    Point2D start{info[0].As<Napi::Number>().DoubleValue(), info[1].As<Napi::Number>().DoubleValue()};
    Point2D handleOut{info[2].As<Napi::Number>().DoubleValue(), info[3].As<Napi::Number>().DoubleValue()};
    Point2D end{info[4].As<Napi::Number>().DoubleValue(), info[5].As<Napi::Number>().DoubleValue()};
    Point2D handleIn{info[6].As<Napi::Number>().DoubleValue(), info[7].As<Napi::Number>().DoubleValue()};
    RoadToolParams params;
    if (info.Length() >= 9) params = parseToolParams(info[8]);
    Road road = createBezier(start, handleOut, end, handleIn, params);
    return roadToJs(env, road);
}

// roadCreateClothoidSpline(points[], startTangentX, startTangentY, endTangentX, endTangentY, segsPerSpan?, params?) → Road
static Napi::Value RoadCreateClothoidSpline(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 5 || !info[0].IsArray()) {
        Napi::TypeError::New(env, "Expected (points[], startTangentX, startTangentY, endTangentX, endTangentY, ...)").ThrowAsJavaScriptException();
        return env.Null();
    }
    auto pts = parsePoints(info[0]);
    Point2D startTan{info[1].As<Napi::Number>().DoubleValue(), info[2].As<Napi::Number>().DoubleValue()};
    Point2D endTan{info[3].As<Napi::Number>().DoubleValue(), info[4].As<Napi::Number>().DoubleValue()};
    int segsPerSpan = 8;
    if (info.Length() >= 6 && info[5].IsNumber()) segsPerSpan = info[5].As<Napi::Number>().Int32Value();
    RoadToolParams params;
    if (info.Length() >= 7) params = parseToolParams(info[6]);
    Road road = createClothoidSpline(pts, startTan, endTan, segsPerSpan, params);
    return roadToJs(env, road);
}

// ═══════════════════════════════════════════════════════════
// Phase 1.9 — Bridge Integration for RoadV2
// ═══════════════════════════════════════════════════════════
//
// The bridge now supports the RoadV2 pipeline internally. The TS side
// doesn't need to know which adapter path is used — the bridge dispatches
// based on formatVersion:
//   formatVersion >= 2 → roadToV2() (exact path)
//   formatVersion < 2  → roadToV2Legacy() (legacy compatibility)
//
// New IPC methods:
//   roadSampleCenterlineV2(road, numSamples?) → Point2D[]
//     Uses roadToV2Auto() internally, samples the RoadV2 centerline.
//     This is the preferred sampling method for new code.
//
//   roadConvertFromV2(road) → Road
//     Converts a legacy Road to RoadV2 and back (round-trip).
//     Useful for testing and for future migration scenarios.
//

// roadSampleCenterlineV2(road, numSamples?) → Point2D[]
// Uses roadToV2Auto() to convert, then samples the RoadV2 centerline.
static Napi::Value RoadSampleCenterlineV2(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsObject()) {
        Napi::TypeError::New(env, "Expected (road)").ThrowAsJavaScriptException();
        return env.Null();
    }

    Road road = parseRoad(info[0].As<Napi::Object>());
    int numSamples = (info.Length() >= 2 && info[1].IsNumber())
        ? info[1].As<Napi::Number>().Int32Value() : 24;

    // Convert to RoadV2 using auto-dispatch (formatVersion-aware)
    AdapterReport report;
    RoadV2 v2 = roadToV2Auto(road, report);

    if (v2.numSegments() == 0) {
        return Napi::Array::New(env, 0);
    }

    // Sample the RoadV2 centerline at uniform arc-length intervals
    double totalLen = v2.totalLength();
    auto samples = Napi::Array::New(env, numSamples);
    for (int i = 0; i < numSamples; i++) {
        double s = totalLen * static_cast<double>(i) / (numSamples - 1);
        Point2D p = v2.geometry().positionAt(s);
        auto ptObj = Napi::Object::New(env);
        ptObj.Set("x", Napi::Number::New(env, p.x));
        ptObj.Set("y", Napi::Number::New(env, p.y));
        samples.Set(i, ptObj);
    }
    return samples;
}

// roadGetAdapterReport(road) → { exact, exactSegments, legacySegments, ... }
// Returns diagnostics from the roadToV2Auto() conversion.
static Napi::Value RoadGetAdapterReport(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsObject()) {
        Napi::TypeError::New(env, "Expected (road)").ThrowAsJavaScriptException();
        return env.Null();
    }

    Road road = parseRoad(info[0].As<Napi::Object>());
    AdapterReport report;
    RoadV2 v2 = roadToV2Auto(road, report);

    auto obj = Napi::Object::New(env);
    obj.Set("exact", Napi::Boolean::New(env, report.exact));
    obj.Set("exactSegments", Napi::Number::New(env, report.exactSegments));
    obj.Set("legacySegments", Napi::Number::New(env, report.legacySegments));
    obj.Set("unsupportedSegments", Napi::Number::New(env, report.unsupportedSegments));
    obj.Set("lineSegments", Napi::Number::New(env, report.lineSegments));
    obj.Set("bezierSegments", Napi::Number::New(env, report.bezierSegments));
    obj.Set("arcSegments", Napi::Number::New(env, report.arcSegments));
    obj.Set("spiralSegments", Napi::Number::New(env, report.spiralSegments));
    obj.Set("numSegments", Napi::Number::New(env, v2.numSegments()));
    obj.Set("totalLength", Napi::Number::New(env, v2.totalLength()));

    auto warnings = Napi::Array::New(env, report.warnings.size());
    for (size_t i = 0; i < report.warnings.size(); i++) {
        warnings.Set(i, Napi::String::New(env, report.warnings[i]));
    }
    obj.Set("warnings", warnings);

    return obj;
}

// roadConvertFromV2(road) → Road
// Round-trip: Road → roadToV2Auto() → RoadV2 → roadFromV2() → Road
// Useful for testing and migration.
static Napi::Value RoadConvertFromV2(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsObject()) {
        Napi::TypeError::New(env, "Expected (road)").ThrowAsJavaScriptException();
        return env.Null();
    }

    Road road = parseRoad(info[0].As<Napi::Object>());

    // Forward: Road → RoadV2
    AdapterReport fwdReport;
    RoadV2 v2 = roadToV2Auto(road, fwdReport);

    // Reverse: RoadV2 → Road
    ReverseAdapterReport revReport;
    Road restored = roadFromV2(v2, revReport);

    return roadToJs(env, restored);
}

// ─── Phase 2.8 — RoadBuildResult: full pipeline in one call ───

// Serialize a MeshSection to a JS object with typed arrays
static Napi::Object meshSectionToJs(Napi::Env env, const geo::MeshSection& section) {
    auto obj = Napi::Object::New(env);
    obj.Set("material", Napi::String::New(env, section.materialName));
    obj.Set("materialType", Napi::Number::New(env, static_cast<int>(section.material)));
    obj.Set("vertexCount", Napi::Number::New(env, section.vertexCount()));
    obj.Set("indexCount", Napi::Number::New(env, section.indexCount()));
    obj.Set("triangleCount", Napi::Number::New(env, section.triangleCount()));

    // Positions: Float32Array (x, y, z interleaved)
    int vCount = section.vertexCount();
    auto positions = Napi::Float32Array::New(env, vCount * 3);
    auto normals = Napi::Float32Array::New(env, vCount * 3);
    auto uvs = Napi::Float32Array::New(env, vCount * 2);
    for (int i = 0; i < vCount; i++) {
        positions[i * 3]     = static_cast<float>(section.vertices[i].position.x);
        positions[i * 3 + 1] = static_cast<float>(section.vertices[i].position.y);
        positions[i * 3 + 2] = static_cast<float>(section.vertices[i].position.z);
        normals[i * 3]     = static_cast<float>(section.vertices[i].normal.x);
        normals[i * 3 + 1] = static_cast<float>(section.vertices[i].normal.y);
        normals[i * 3 + 2] = static_cast<float>(section.vertices[i].normal.z);
        uvs[i * 2]     = static_cast<float>(section.vertices[i].uv.x);
        uvs[i * 2 + 1] = static_cast<float>(section.vertices[i].uv.y);
    }
    obj.Set("positions", positions);
    obj.Set("normals", normals);
    obj.Set("uvs", uvs);

    // Indices: Uint32Array
    int iCount = section.indexCount();
    auto indices = Napi::Uint32Array::New(env, iCount);
    for (int i = 0; i < iCount; i++) {
        indices[i] = section.indices[i];
    }
    obj.Set("indices", indices);

    return obj;
}

// Serialize a SamplePoint to a JS object
static Napi::Object samplePointToJs(Napi::Env env, const geo::SamplePoint& sp) {
    auto obj = Napi::Object::New(env);
    auto pos = Napi::Object::New(env);
    pos.Set("x", Napi::Number::New(env, sp.position.x));
    pos.Set("y", Napi::Number::New(env, sp.position.y));
    obj.Set("position", pos);
    obj.Set("s", Napi::Number::New(env, sp.s));
    obj.Set("heading", Napi::Number::New(env, sp.heading));
    obj.Set("laneOffset", Napi::Number::New(env, sp.laneOffset));
    return obj;
}

// Serialize a LaneCenterline to a JS object
static Napi::Object centerlineToJs(Napi::Env env, const geo::LaneCenterline& cl) {
    auto obj = Napi::Object::New(env);
    obj.Set("laneId", Napi::Number::New(env, cl.laneId));
    obj.Set("type", Napi::Number::New(env, static_cast<int>(cl.type)));
    obj.Set("startS", Napi::Number::New(env, cl.startS));
    obj.Set("endS", Napi::Number::New(env, cl.endS));
    obj.Set("length", Napi::Number::New(env, cl.length));
    obj.Set("numSamples", Napi::Number::New(env, cl.numSamples()));

    auto samples = Napi::Array::New(env, cl.samples.size());
    for (size_t i = 0; i < cl.samples.size(); i++) {
        samples.Set(i, samplePointToJs(env, cl.samples[i]));
    }
    obj.Set("samples", samples);
    return obj;
}

// Serialize a LaneBoundary to a JS object
static Napi::Object boundaryToJs(Napi::Env env, const geo::LaneBoundary& b) {
    auto obj = Napi::Object::New(env);
    obj.Set("innerLaneId", Napi::Number::New(env, b.innerLaneId));
    obj.Set("outerLaneId", Napi::Number::New(env, b.outerLaneId));
    obj.Set("isRoadEdge", Napi::Boolean::New(env, b.isRoadEdge));
    obj.Set("startS", Napi::Number::New(env, b.startS));
    obj.Set("endS", Napi::Number::New(env, b.endS));
    obj.Set("length", Napi::Number::New(env, b.length));
    obj.Set("markType", Napi::Number::New(env, static_cast<int>(b.markType)));
    obj.Set("markColor", Napi::String::New(env, b.markColor));
    obj.Set("markWidth", Napi::Number::New(env, b.markWidth));
    obj.Set("numSamples", Napi::Number::New(env, b.numSamples()));

    auto samples = Napi::Array::New(env, b.samples.size());
    for (size_t i = 0; i < b.samples.size(); i++) {
        samples.Set(i, samplePointToJs(env, b.samples[i]));
    }
    obj.Set("samples", samples);
    return obj;
}

// Serialize a RoadMarkPolyline to a JS object
static Napi::Object markPolylineToJs(Napi::Env env, const geo::RoadMarkPolyline& m) {
    auto obj = Napi::Object::New(env);
    obj.Set("innerLaneId", Napi::Number::New(env, m.innerLaneId));
    obj.Set("outerLaneId", Napi::Number::New(env, m.outerLaneId));
    obj.Set("isRoadEdge", Napi::Boolean::New(env, m.isRoadEdge));
    obj.Set("isCenterLine", Napi::Boolean::New(env, m.isCenterLine));
    obj.Set("startS", Napi::Number::New(env, m.startS));
    obj.Set("endS", Napi::Number::New(env, m.endS));
    obj.Set("length", Napi::Number::New(env, m.length));
    obj.Set("color", Napi::String::New(env, m.style.color));
    obj.Set("width", Napi::Number::New(env, m.style.width));
    obj.Set("markType", Napi::Number::New(env, static_cast<int>(m.style.type)));
    obj.Set("numSamples", Napi::Number::New(env, m.numSamples()));

    auto samples = Napi::Array::New(env, m.samples.size());
    for (size_t i = 0; i < m.samples.size(); i++) {
        samples.Set(i, samplePointToJs(env, m.samples[i]));
    }
    obj.Set("samples", samples);
    return obj;
}

// roadBuildRoad(road) → RoadBuildResult
// Full pipeline: Road → RoadV2 → LaneNetwork → RoadMarkNetwork → RoadMesh
// Returns everything in one call so the editor doesn't need multiple IPC round-trips.
static Napi::Value RoadBuildRoad(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsObject()) {
        Napi::TypeError::New(env, "Expected (road)").ThrowAsJavaScriptException();
        return env.Null();
    }

    Road road = parseRoad(info[0].As<Napi::Object>());

    // Step 1: Road → RoadV2
    AdapterReport adapterReport;
    RoadV2 v2 = roadToV2Auto(road, adapterReport);

    // Step 2: RoadV2 → LaneNetwork
    LaneNetwork laneNetwork = generateLaneNetwork(v2);

    // Step 3: LaneNetwork → RoadMarkNetwork
    RoadMarkNetwork markNetwork = generateRoadMarks(laneNetwork);

    // Step 4: LaneNetwork + RoadMarkNetwork → RoadMesh
    RoadMesh mesh = generateFullRoadMesh(laneNetwork, markNetwork);

    // Serialize result
    auto result = Napi::Object::New(env);

    // ─── Mesh sections (typed arrays for Babylon.js) ───
    auto meshSections = Napi::Array::New(env, mesh.sections.size());
    for (size_t i = 0; i < mesh.sections.size(); i++) {
        meshSections.Set(i, meshSectionToJs(env, mesh.sections[i]));
    }
    result.Set("meshSections", meshSections);
    result.Set("totalVertices", Napi::Number::New(env, mesh.totalVertices()));
    result.Set("totalTriangles", Napi::Number::New(env, mesh.totalTriangles()));

    // ─── Lane network (for debug overlays, AI, OpenDRIVE) ───
    auto lanes = Napi::Object::New(env);
    lanes.Set("roadId", Napi::String::New(env, laneNetwork.roadId));
    lanes.Set("totalLength", Napi::Number::New(env, laneNetwork.totalLength));
    lanes.Set("numLaneSections", Napi::Number::New(env, laneNetwork.numLaneSections));
    lanes.Set("numCenterlines", Napi::Number::New(env, laneNetwork.numCenterlines()));
    lanes.Set("numBoundaries", Napi::Number::New(env, laneNetwork.numBoundaries()));

    auto centerlines = Napi::Array::New(env, laneNetwork.centerlines.size());
    for (size_t i = 0; i < laneNetwork.centerlines.size(); i++) {
        centerlines.Set(i, centerlineToJs(env, laneNetwork.centerlines[i]));
    }
    lanes.Set("centerlines", centerlines);

    auto boundaries = Napi::Array::New(env, laneNetwork.boundaries.size());
    for (size_t i = 0; i < laneNetwork.boundaries.size(); i++) {
        boundaries.Set(i, boundaryToJs(env, laneNetwork.boundaries[i]));
    }
    lanes.Set("boundaries", boundaries);

    result.Set("lanes", lanes);

    // ─── Road mark network (for marking rendering) ───
    auto markings = Napi::Object::New(env);
    markings.Set("roadId", Napi::String::New(env, markNetwork.roadId));
    markings.Set("numMarkings", Napi::Number::New(env, markNetwork.numMarkings()));

    auto markArr = Napi::Array::New(env, markNetwork.markings.size());
    for (size_t i = 0; i < markNetwork.markings.size(); i++) {
        markArr.Set(i, markPolylineToJs(env, markNetwork.markings[i]));
    }
    markings.Set("markings", markArr);

    result.Set("markings", markings);

    // ─── Adapter report ───
    auto adapter = Napi::Object::New(env);
    adapter.Set("exact", Napi::Boolean::New(env, adapterReport.exact));
    adapter.Set("exactSegments", Napi::Number::New(env, adapterReport.exactSegments));
    adapter.Set("legacySegments", Napi::Number::New(env, adapterReport.legacySegments));
    adapter.Set("unsupportedSegments", Napi::Number::New(env, adapterReport.unsupportedSegments));
    adapter.Set("numSegments", Napi::Number::New(env, v2.numSegments()));
    adapter.Set("totalLength", Napi::Number::New(env, v2.totalLength()));

    auto warnings = Napi::Array::New(env, adapterReport.warnings.size());
    for (size_t i = 0; i < adapterReport.warnings.size(); i++) {
        warnings.Set(i, Napi::String::New(env, adapterReport.warnings[i]));
    }
    adapter.Set("warnings", warnings);

    result.Set("adapter", adapter);

    ROAD_LOG("buildRoad() → " + std::to_string(mesh.totalVertices()) + " verts, " +
             std::to_string(mesh.totalTriangles()) + " tris, " +
             std::to_string(laneNetwork.numCenterlines()) + " centerlines, " +
             std::to_string(markNetwork.numMarkings()) + " markings");

    return result;
}

// ═══════════════════════════════════════════════════════════
// Phase 3 — Road Graph + Junction Builder
// ═══════════════════════════════════════════════════════════

// roadBuildRoadGraph(roads[], intersections[]) → RoadGraph
// Builds the road network topology from all roads and intersections.
static Napi::Value RoadBuildRoadGraph(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 2 || !info[0].IsArray() || !info[1].IsArray()) {
        Napi::TypeError::New(env, "Expected (roads[], intersections[])").ThrowAsJavaScriptException();
        return env.Null();
    }

    auto roadsArr = info[0].As<Napi::Array>();
    auto intersectionsArr = info[1].As<Napi::Array>();

    // Parse roads
    std::vector<Road> roads;
    for (uint32_t i = 0; i < roadsArr.Length(); i++) {
        if (roadsArr.Get(i).IsObject()) {
            roads.push_back(parseRoad(roadsArr.Get(i).As<Napi::Object>()));
        }
    }

    // Parse intersections (GeneratedIntersection)
    std::vector<GeneratedIntersection> intersections;
    for (uint32_t i = 0; i < intersectionsArr.Length(); i++) {
        if (!intersectionsArr.Get(i).IsObject()) continue;
        auto ixObj = intersectionsArr.Get(i).As<Napi::Object>();
        GeneratedIntersection gen;
        if (ixObj.Has("center")) {
            auto c = ixObj.Get("center").As<Napi::Object>();
            gen.center.x = c.Get("x").As<Napi::Number>().DoubleValue();
            gen.center.y = c.Get("y").As<Napi::Number>().DoubleValue();
        }
        if (ixObj.Has("polygon")) {
            auto poly = ixObj.Get("polygon").As<Napi::Array>();
            for (uint32_t j = 0; j < poly.Length(); j++) {
                auto p = poly.Get(j).As<Napi::Object>();
                gen.polygon.push_back({ p.Get("x").As<Napi::Number>().DoubleValue(),
                                        p.Get("y").As<Napi::Number>().DoubleValue() });
            }
        }
        if (ixObj.Has("approaches")) {
            auto apps = ixObj.Get("approaches").As<Napi::Array>();
            for (uint32_t j = 0; j < apps.Length(); j++) {
                auto a = apps.Get(j).As<Napi::Object>();
                ApproachRoad app;
                if (a.Has("roadId")) app.roadId = a.Get("roadId").As<Napi::String>().Utf8Value();
                if (a.Has("width"))  app.width  = a.Get("width").As<Napi::Number>().DoubleValue();
                if (a.Has("laneCount")) app.laneCount = a.Get("laneCount").As<Napi::Number>().Int32Value();
                if (a.Has("z"))     app.z      = a.Get("z").As<Napi::Number>().DoubleValue();
                gen.approaches.push_back(std::move(app));
            }
        }
        intersections.push_back(std::move(gen));
    }

    // Convert roads to RoadV2 for the graph builder
    std::vector<RoadV2> v2Roads;
    for (const auto& road : roads) {
        AdapterReport report;
        v2Roads.push_back(roadToV2Auto(road, report));
    }

    // Build the road graph
    RoadGraph graph = buildFromRoads(v2Roads, intersections);

    // Serialize
    auto result = Napi::Object::New(env);
    result.Set("numNodes", Napi::Number::New(env, graph.numNodes()));
    result.Set("numEdges", Napi::Number::New(env, graph.numEdges()));

    auto nodes = Napi::Array::New(env, graph.nodes.size());
    for (size_t i = 0; i < graph.nodes.size(); i++) {
        auto node = Napi::Object::New(env);
        node.Set("id", Napi::String::New(env, graph.nodes[i].id));
        auto pos = Napi::Object::New(env);
        pos.Set("x", Napi::Number::New(env, graph.nodes[i].position.x));
        pos.Set("y", Napi::Number::New(env, graph.nodes[i].position.y));
        node.Set("position", pos);
        node.Set("z", Napi::Number::New(env, graph.nodes[i].z));
        node.Set("type", Napi::Number::New(env, static_cast<int>(graph.nodes[i].type)));
        node.Set("typeName", Napi::String::New(env, graph.nodes[i].typeName()));

        auto connRoads = Napi::Array::New(env, graph.nodes[i].connectedRoadIds.size());
        for (size_t j = 0; j < graph.nodes[i].connectedRoadIds.size(); j++) {
            connRoads.Set(j, Napi::String::New(env, graph.nodes[i].connectedRoadIds[j]));
        }
        node.Set("connectedRoadIds", connRoads);
        nodes.Set(i, node);
    }
    result.Set("nodes", nodes);

    auto edges = Napi::Array::New(env, graph.edges.size());
    for (size_t i = 0; i < graph.edges.size(); i++) {
        auto edge = Napi::Object::New(env);
        edge.Set("id", Napi::String::New(env, graph.edges[i].id));
        edge.Set("fromNodeId", Napi::String::New(env, graph.edges[i].fromNodeId));
        edge.Set("toNodeId", Napi::String::New(env, graph.edges[i].toNodeId));
        edge.Set("roadId", Napi::String::New(env, graph.edges[i].roadId));
        edge.Set("length", Napi::Number::New(env, graph.edges[i].length));
        edge.Set("laneCount", Napi::Number::New(env, graph.edges[i].laneCount));
        edge.Set("width", Napi::Number::New(env, graph.edges[i].width));
        edge.Set("isOneWay", Napi::Boolean::New(env, graph.edges[i].isOneWay));
        edges.Set(i, edge);
    }
    result.Set("edges", edges);

    ROAD_LOG("buildRoadGraph() → " + std::to_string(graph.numNodes()) + " nodes, " +
             std::to_string(graph.numEdges()) + " edges");

    return result;
}

// roadBuildJunction(junctionNodeId, roadGraph, laneNetworks) → JunctionResult
// Builds junction geometry from lane connections.
static Napi::Value RoadBuildJunction(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 3 || !info[0].IsString() || !info[1].IsObject() || !info[2].IsObject()) {
        Napi::TypeError::New(env, "Expected (junctionNodeId, roadGraph, laneNetworks)").ThrowAsJavaScriptException();
        return env.Null();
    }

    std::string junctionId = info[0].As<Napi::String>().Utf8Value();
    auto graphObj = info[1].As<Napi::Object>();
    auto laneNetsObj = info[2].As<Napi::Object>();

    // Reconstruct RoadGraph (minimal — just need the junction node)
    RoadGraph graph;
    if (graphObj.Has("nodes")) {
        auto nodes = graphObj.Get("nodes").As<Napi::Array>();
        for (uint32_t i = 0; i < nodes.Length(); i++) {
            auto n = nodes.Get(i).As<Napi::Object>();
            RoadNode node;
            if (n.Has("id")) node.id = n.Get("id").As<Napi::String>().Utf8Value();
            if (n.Has("position")) {
                auto p = n.Get("position").As<Napi::Object>();
                node.position.x = p.Get("x").As<Napi::Number>().DoubleValue();
                node.position.y = p.Get("y").As<Napi::Number>().DoubleValue();
            }
            if (n.Has("z")) node.z = n.Get("z").As<Napi::Number>().DoubleValue();
            if (n.Has("type")) node.type = static_cast<RoadNodeType>(n.Get("type").As<Napi::Number>().Int32Value());
            if (n.Has("connectedRoadIds")) {
                auto cr = n.Get("connectedRoadIds").As<Napi::Array>();
                for (uint32_t j = 0; j < cr.Length(); j++) {
                    node.connectedRoadIds.push_back(cr.Get(j).As<Napi::String>().Utf8Value());
                }
            }
            graph.nodes.push_back(std::move(node));
        }
    }
    if (graphObj.Has("edges")) {
        auto edges = graphObj.Get("edges").As<Napi::Array>();
        for (uint32_t i = 0; i < edges.Length(); i++) {
            auto e = edges.Get(i).As<Napi::Object>();
            RoadEdge edge;
            if (e.Has("id")) edge.id = e.Get("id").As<Napi::String>().Utf8Value();
            if (e.Has("fromNodeId")) edge.fromNodeId = e.Get("fromNodeId").As<Napi::String>().Utf8Value();
            if (e.Has("toNodeId")) edge.toNodeId = e.Get("toNodeId").As<Napi::String>().Utf8Value();
            if (e.Has("roadId")) edge.roadId = e.Get("roadId").As<Napi::String>().Utf8Value();
            if (e.Has("length")) edge.length = e.Get("length").As<Napi::Number>().DoubleValue();
            if (e.Has("laneCount")) edge.laneCount = e.Get("laneCount").As<Napi::Number>().Int32Value();
            if (e.Has("width")) edge.width = e.Get("width").As<Napi::Number>().DoubleValue();
            if (e.Has("isOneWay")) edge.isOneWay = e.Get("isOneWay").As<Napi::Boolean>().Value();
            graph.edges.push_back(std::move(edge));
        }
    }

    // Find the junction node
    const RoadNode* junction = graph.findNode(junctionId);
    if (!junction || !junction->isJunction()) {
        ROAD_LOG("buildJunction() — junction not found or not a junction: " + junctionId);
        return env.Null();
    }

    // Reconstruct LaneNetworks from the laneNetworks map object
    std::map<std::string, LaneNetwork> laneNets;
    auto keys = laneNetsObj.GetPropertyNames();
    for (uint32_t i = 0; i < keys.Length(); i++) {
        std::string roadId = keys.Get(i).As<Napi::String>().Utf8Value();
        auto netObj = laneNetsObj.Get(roadId).As<Napi::Object>();

        LaneNetwork net;
        net.roadId = roadId;
        if (netObj.Has("totalLength")) net.totalLength = netObj.Get("totalLength").As<Napi::Number>().DoubleValue();
        if (netObj.Has("numLaneSections")) net.numLaneSections = netObj.Get("numLaneSections").As<Napi::Number>().Int32Value();

        // Parse centerlines
        if (netObj.Has("centerlines")) {
            auto cls = netObj.Get("centerlines").As<Napi::Array>();
            for (uint32_t j = 0; j < cls.Length(); j++) {
                auto cl = cls.Get(j).As<Napi::Object>();
                LaneCenterline centerline;
                if (cl.Has("laneId")) centerline.laneId = cl.Get("laneId").As<Napi::Number>().Int32Value();
                if (cl.Has("startS")) centerline.startS = cl.Get("startS").As<Napi::Number>().DoubleValue();
                if (cl.Has("endS")) centerline.endS = cl.Get("endS").As<Napi::Number>().DoubleValue();
                if (cl.Has("length")) centerline.length = cl.Get("length").As<Napi::Number>().DoubleValue();
                if (cl.Has("samples")) {
                    auto samples = cl.Get("samples").As<Napi::Array>();
                    for (uint32_t k = 0; k < samples.Length(); k++) {
                        auto s = samples.Get(k).As<Napi::Object>();
                        SamplePoint sp;
                        if (s.Has("position")) {
                            auto p = s.Get("position").As<Napi::Object>();
                            sp.position.x = p.Get("x").As<Napi::Number>().DoubleValue();
                            sp.position.y = p.Get("y").As<Napi::Number>().DoubleValue();
                        }
                        if (s.Has("s")) sp.s = s.Get("s").As<Napi::Number>().DoubleValue();
                        if (s.Has("heading")) sp.heading = s.Get("heading").As<Napi::Number>().DoubleValue();
                        if (s.Has("laneOffset")) sp.laneOffset = s.Get("laneOffset").As<Napi::Number>().DoubleValue();
                        centerline.samples.push_back(sp);
                    }
                }
                net.centerlines.push_back(std::move(centerline));
            }
        }

        // Parse boundaries
        if (netObj.Has("boundaries")) {
            auto bs = netObj.Get("boundaries").As<Napi::Array>();
            for (uint32_t j = 0; j < bs.Length(); j++) {
                auto b = bs.Get(j).As<Napi::Object>();
                LaneBoundary boundary;
                if (b.Has("innerLaneId")) boundary.innerLaneId = b.Get("innerLaneId").As<Napi::Number>().Int32Value();
                if (b.Has("outerLaneId")) boundary.outerLaneId = b.Get("outerLaneId").As<Napi::Number>().Int32Value();
                if (b.Has("isRoadEdge")) boundary.isRoadEdge = b.Get("isRoadEdge").As<Napi::Boolean>().Value();
                if (b.Has("startS")) boundary.startS = b.Get("startS").As<Napi::Number>().DoubleValue();
                if (b.Has("endS")) boundary.endS = b.Get("endS").As<Napi::Number>().DoubleValue();
                if (b.Has("length")) boundary.length = b.Get("length").As<Napi::Number>().DoubleValue();
                if (b.Has("markColor")) boundary.markColor = b.Get("markColor").As<Napi::String>().Utf8Value();
                if (b.Has("markWidth")) boundary.markWidth = b.Get("markWidth").As<Napi::Number>().DoubleValue();
                if (b.Has("samples")) {
                    auto samples = b.Get("samples").As<Napi::Array>();
                    for (uint32_t k = 0; k < samples.Length(); k++) {
                        auto s = samples.Get(k).As<Napi::Object>();
                        SamplePoint sp;
                        if (s.Has("position")) {
                            auto p = s.Get("position").As<Napi::Object>();
                            sp.position.x = p.Get("x").As<Napi::Number>().DoubleValue();
                            sp.position.y = p.Get("y").As<Napi::Number>().DoubleValue();
                        }
                        if (s.Has("s")) sp.s = s.Get("s").As<Napi::Number>().DoubleValue();
                        if (s.Has("heading")) sp.heading = s.Get("heading").As<Napi::Number>().DoubleValue();
                        if (s.Has("laneOffset")) sp.laneOffset = s.Get("laneOffset").As<Napi::Number>().DoubleValue();
                        boundary.samples.push_back(sp);
                    }
                }
                net.boundaries.push_back(std::move(boundary));
            }
        }

        laneNets[roadId] = std::move(net);
    }

    // Build lane graph
    LaneGraph laneGraph = LaneGraphBuilder::build(*junction, graph, laneNets);

    // Build junction geometry
    JunctionResult junctionResult = JunctionBuilder::build(*junction, graph, laneGraph, laneNets);

    // Serialize
    auto result = Napi::Object::New(env);
    result.Set("junctionId", Napi::String::New(env, junctionResult.junctionId));
    result.Set("numLaneConnections", Napi::Number::New(env, junctionResult.numLaneConnections));
    result.Set("numApproaches", Napi::Number::New(env, junctionResult.numApproaches));

    auto center = Napi::Object::New(env);
    center.Set("x", Napi::Number::New(env, junctionResult.center.x));
    center.Set("y", Napi::Number::New(env, junctionResult.center.y));
    result.Set("center", center);

    // Polygon
    auto poly = Napi::Array::New(env, junctionResult.polygon.size());
    for (size_t i = 0; i < junctionResult.polygon.size(); i++) {
        auto p = Napi::Object::New(env);
        p.Set("x", Napi::Number::New(env, junctionResult.polygon[i].x));
        p.Set("y", Napi::Number::New(env, junctionResult.polygon[i].y));
        poly.Set(i, p);
    }
    result.Set("polygon", poly);

    // Lane stripes
    auto stripes = Napi::Array::New(env, junctionResult.laneStripes.size());
    for (size_t i = 0; i < junctionResult.laneStripes.size(); i++) {
        auto s = Napi::Object::New(env);
        const auto& stripe = junctionResult.laneStripes[i];
        s.Set("type", Napi::Number::New(env, static_cast<int>(stripe.type)));
        s.Set("color", Napi::String::New(env, stripe.color));

        auto samples = Napi::Array::New(env, stripe.samples.size());
        for (size_t j = 0; j < stripe.samples.size(); j++) {
            auto p = Napi::Object::New(env);
            p.Set("x", Napi::Number::New(env, stripe.samples[j].x));
            p.Set("y", Napi::Number::New(env, stripe.samples[j].y));
            samples.Set(j, p);
        }
        s.Set("samples", samples);
        stripes.Set(i, s);
    }
    result.Set("laneStripes", stripes);

    // Asphalt mesh
    auto asphalt = Napi::Object::New(env);
    {
        const auto& m = junctionResult.asphaltMesh;
        auto positions = Napi::Float32Array::New(env, m.positions.size());
        for (size_t i = 0; i < m.positions.size(); i++) positions[i] = m.positions[i];
        asphalt.Set("positions", positions);

        auto normals = Napi::Float32Array::New(env, m.normals.size());
        for (size_t i = 0; i < m.normals.size(); i++) normals[i] = m.normals[i];
        asphalt.Set("normals", normals);

        auto uvs = Napi::Float32Array::New(env, m.uvs.size());
        for (size_t i = 0; i < m.uvs.size(); i++) uvs[i] = m.uvs[i];
        asphalt.Set("uvs", uvs);

        auto indices = Napi::Uint32Array::New(env, m.indices.size());
        for (size_t i = 0; i < m.indices.size(); i++) indices[i] = m.indices[i];
        asphalt.Set("indices", indices);

        asphalt.Set("vertexCount", Napi::Number::New(env, m.vertexCount));
        asphalt.Set("triangleCount", Napi::Number::New(env, m.triangleCount));
    }
    result.Set("asphaltMesh", asphalt);

    // Marking mesh
    auto marking = Napi::Object::New(env);
    {
        const auto& m = junctionResult.markingMesh;
        auto positions = Napi::Float32Array::New(env, m.positions.size());
        for (size_t i = 0; i < m.positions.size(); i++) positions[i] = m.positions[i];
        marking.Set("positions", positions);

        auto normals = Napi::Float32Array::New(env, m.normals.size());
        for (size_t i = 0; i < m.normals.size(); i++) normals[i] = m.normals[i];
        marking.Set("normals", normals);

        auto uvs = Napi::Float32Array::New(env, m.uvs.size());
        for (size_t i = 0; i < m.uvs.size(); i++) uvs[i] = m.uvs[i];
        marking.Set("uvs", uvs);

        auto indices = Napi::Uint32Array::New(env, m.indices.size());
        for (size_t i = 0; i < m.indices.size(); i++) indices[i] = m.indices[i];
        marking.Set("indices", indices);

        marking.Set("vertexCount", Napi::Number::New(env, m.vertexCount));
        marking.Set("triangleCount", Napi::Number::New(env, m.triangleCount));
    }
    result.Set("markingMesh", marking);

    ROAD_LOG("buildJunction() → " + std::to_string(junctionResult.numLaneConnections) +
             " connections, " + std::to_string(junctionResult.asphaltMesh.triangleCount) + " asphalt tris, " +
             std::to_string(junctionResult.markingMesh.triangleCount) + " marking tris");

    return result;
}

Napi::Object InitRoadBridge(Napi::Env env, Napi::Object exports) {
    exports.Set("roadGetVersion", Napi::Function::New(env, RoadGetVersion));
    exports.Set("roadGenerateIntersection", Napi::Function::New(env, RoadGenerateIntersection));
    exports.Set("roadComputeCircleArc", Napi::Function::New(env, RoadComputeCircleArc));
    exports.Set("roadComputeClothoid", Napi::Function::New(env, RoadComputeClothoid));
    exports.Set("roadSampleCenterline", Napi::Function::New(env, RoadSampleCenterline));
    exports.Set("roadGeoToLocal", Napi::Function::New(env, RoadGeoToLocal));
    exports.Set("roadLocalToGeo", Napi::Function::New(env, RoadLocalToGeo));
    exports.Set("roadGenerateRoadMesh", Napi::Function::New(env, RoadGenerateRoadMesh));
    exports.Set("roadGenerateIntersectionMesh", Napi::Function::New(env, RoadGenerateIntersectionMesh));
    exports.Set("roadExportOpenDrive", Napi::Function::New(env, RoadExportOpenDrive));
    // Road creation tools (SCANeR-style)
    exports.Set("roadCreateSegment", Napi::Function::New(env, RoadCreateSegment));
    exports.Set("roadCreateCircleArc", Napi::Function::New(env, RoadCreateCircleArc));
    exports.Set("roadCreateClothoidArc", Napi::Function::New(env, RoadCreateClothoidArc));
    exports.Set("roadCreatePolyline", Napi::Function::New(env, RoadCreatePolyline));
    exports.Set("roadCreateBezier", Napi::Function::New(env, RoadCreateBezier));
    exports.Set("roadCreateClothoidSpline", Napi::Function::New(env, RoadCreateClothoidSpline));
    // Phase 1.9 — RoadV2 bridge integration
    exports.Set("roadSampleCenterlineV2", Napi::Function::New(env, RoadSampleCenterlineV2));
    exports.Set("roadGetAdapterReport", Napi::Function::New(env, RoadGetAdapterReport));
    exports.Set("roadConvertFromV2", Napi::Function::New(env, RoadConvertFromV2));
    // Phase 2.8 — Full pipeline API
    exports.Set("roadBuildRoad", Napi::Function::New(env, RoadBuildRoad));
    // Phase 3 — Road Graph + Junction Builder
    exports.Set("roadBuildRoadGraph", Napi::Function::New(env, RoadBuildRoadGraph));
    exports.Set("roadBuildJunction", Napi::Function::New(env, RoadBuildJunction));
    return exports;
}

} // namespace geo
