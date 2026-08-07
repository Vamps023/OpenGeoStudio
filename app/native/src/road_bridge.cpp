#include <napi.h>
#include "road/geometry.hpp"
#include "road/road.hpp"
#include "road/arc.hpp"
#include "road/intersection.hpp"
#include "road/clothoid.hpp"
#include <sstream>

namespace geo {

// ─── Helpers: JS ↔ C++ conversion ──────────────────────────

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

    return obj;
}

// ─── N-API functions ───────────────────────────────────────

// roadGetVersion() → string
static Napi::Value RoadGetVersion(const Napi::CallbackInfo& info) {
    return Napi::String::New(info.Env(), "1.0.0-road-engine");
}

// roadGenerateIntersection(road1, road2, refLat, refLon) → GeneratedIntersection
static Napi::Value RoadGenerateIntersection(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 4 || !info[0].IsObject() || !info[1].IsObject()) {
        Napi::TypeError::New(env, "Expected (road1, road2, refLat, refLon)").ThrowAsJavaScriptException();
        return env.Null();
    }

    Road road1 = parseRoad(info[0].As<Napi::Object>());
    Road road2 = parseRoad(info[1].As<Napi::Object>());
    double refLat = info[2].As<Napi::Number>().DoubleValue();
    double refLon = info[3].As<Napi::Number>().DoubleValue();

    GeneratedIntersection ix = generateIntersection(road1, road2, refLat, refLon);
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

// ─── Init function ─────────────────────────────────────────
Napi::Object InitRoadBridge(Napi::Env env, Napi::Object exports) {
    exports.Set("roadGetVersion", Napi::Function::New(env, RoadGetVersion));
    exports.Set("roadGenerateIntersection", Napi::Function::New(env, RoadGenerateIntersection));
    exports.Set("roadComputeCircleArc", Napi::Function::New(env, RoadComputeCircleArc));
    exports.Set("roadComputeClothoid", Napi::Function::New(env, RoadComputeClothoid));
    exports.Set("roadSampleCenterline", Napi::Function::New(env, RoadSampleCenterline));
    exports.Set("roadGeoToLocal", Napi::Function::New(env, RoadGeoToLocal));
    exports.Set("roadLocalToGeo", Napi::Function::New(env, RoadLocalToGeo));
    return exports;
}

} // namespace geo
