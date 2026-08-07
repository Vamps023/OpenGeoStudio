#include <napi.h>
#include "road/geometry.hpp"
#include "road/road.hpp"
#include "road/arc.hpp"
#include "road/intersection.hpp"
#include "road/clothoid.hpp"
#include "road/mesh.hpp"
#include "road/opendrive.hpp"
#include "road/road_tools.hpp"
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
    return Napi::String::New(info.Env(), "1.0.0-road-engine");
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
    return exports;
}

} // namespace geo
