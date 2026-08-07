// Road Engine Standalone Addon Entry Point
// This is a separate N-API module that only includes the road geometry engine.
// It does not depend on the terrain/datasource/pipeline libraries.

#include <napi.h>

namespace geo {
    Napi::Object InitRoadBridge(Napi::Env env, Napi::Object exports);
}

Napi::Object InitRoadEngine(Napi::Env env, Napi::Object exports) {
    exports.Set("getVersion", Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
        return Napi::String::New(info.Env(), "1.0.0-road-engine-standalone");
    }));
    geo::InitRoadBridge(env, exports);
    return exports;
}

NODE_API_MODULE(road_engine_native, InitRoadEngine)
