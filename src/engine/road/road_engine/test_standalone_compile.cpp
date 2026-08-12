// Standalone compilation test for RoadEngine public API
// Verifies that all public headers compile without UI framework dependencies
// Compile: g++ -std=c++20 -I. -c test_standalone_compile.cpp
// Or MSVC: cl /std:c++20 /I. /c test_standalone_compile.cpp

#include "public/road_engine.hpp"

// Verify version macros are defined
static_assert(ROAD_ENGINE_VERSION_MAJOR >= 1, "Major version must be >= 1");
static_assert(ROAD_ENGINE_VERSION_MINOR >= 0, "Minor version must be >= 0");
static_assert(ROAD_ENGINE_VERSION_PATCH >= 0, "Patch version must be >= 0");

// Verify types are accessible
static_assert(sizeof(geo::Point2D) > 0, "Point2D must be defined");
static_assert(sizeof(geo::Vec2) > 0, "Vec2 must be defined");

int main() {
    // Verify version function is callable
    const char* ver = road_engine::versionString();
    (void)ver;
    
    // Verify we can construct a Point2D
    geo::Point2D p{1.0, 2.0};
    (void)p;
    
    return 0;
}
