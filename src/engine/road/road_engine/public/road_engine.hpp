#pragma once

// ═══════════════════════════════════════════════════════════
// RoadEngine — Public API Header
// ═══════════════════════════════════════════════════════════
//
// @file road_engine/public/road_engine.hpp
// @brief Public API for the RoadEngine C++ geometry library
//
// This header exposes all stable, documented types and functions
// for consumers of the RoadEngine library. Internal implementation
// details are located in the road_engine/internal/ directory.
//
// @section Versioning
// The library uses semantic versioning. See version macros below.
// Breaking changes to the public API increment the major version.
//
// @section Usage
// Include this header to access all public types:
//   #include <road_engine/road_engine.hpp>
//
// For CMake consumers, link against the road_engine::core target:
//   target_link_libraries(myapp PRIVATE road_engine::core)
//
// @section Requirements
// - C++20 or later
// - Standard library only (no external dependencies for core types)

#ifndef ROAD_ENGINE_HPP
#define ROAD_ENGINE_HPP

// ═══════════════════════════════════════════════════════════
// Version Information
// ═══════════════════════════════════════════════════════════

#define ROAD_ENGINE_VERSION_MAJOR 1
#define ROAD_ENGINE_VERSION_MINOR 0
#define ROAD_ENGINE_VERSION_PATCH 0

// Stringified version for programmatic access
#define ROAD_ENGINE_VERSION_STRING "1.0.0"

// ═══════════════════════════════════════════════════════════
// Forward Declarations — Geometry Types
// ═══════════════════════════════════════════════════════════

namespace geo {

// Forward declarations for geometry primitives
struct Point2D;
struct Point3D;
using Vec2 = Point2D;
using Vec3 = Point3D;
struct BoundingBox2D;
struct Line;
struct Segment;

// Geometry segment hierarchy (abstract base and concrete types)
class GeometrySegment;
class LineSegment;
class ArcSegment;
class SpiralSegment;
class BezierSegment;

// Segment type discriminator
enum class GeometryType;

// Segment sequence view (non-owning)
class SegmentSequence;

} // namespace geo

// ═══════════════════════════════════════════════════════════
// Forward Declarations — Road Model Types
// ═══════════════════════════════════════════════════════════

namespace geo {

// Segment kind enumeration (for metadata)
enum class SegmentKind;

// Segment metadata for exact arc/spiral reconstruction
struct SegmentMetadata;

// Control point with bezier handles
struct ControlPoint;

// Legacy road model (ControlPoint-based)
struct Road;

// New segment-based road model
class RoadV2;

// Road tool parameters for road creation
struct RoadToolParams;

// Road tool type enumeration
enum class RoadToolType;

} // namespace geo

// ═══════════════════════════════════════════════════════════
// Forward Declarations — Mesh Types
// ═══════════════════════════════════════════════════════════

namespace geo {

// Raw mesh data for 3D rendering
struct MeshData;

// Road mesh generation result (mesh + metadata)
struct RoadMeshResult;

// Mesh generation parameters
struct MeshGenParams;

} // namespace geo

// ═══════════════════════════════════════════════════════════
// Forward Declarations — Intersection Types
// ═══════════════════════════════════════════════════════════

namespace geo {

// Generated intersection result
struct GeneratedIntersection;

} // namespace geo

// ═══════════════════════════════════════════════════════════
// Forward Declarations — Adapter Types
// ═══════════════════════════════════════════════════════════

namespace geo {

// Adapter report for roadToV2 diagnostics
struct AdapterReport;

// Reverse adapter report for roadFromV2 diagnostics
struct ReverseAdapterReport;

} // namespace geo

// ═══════════════════════════════════════════════════════════
// Public API Namespace
// ═══════════════════════════════════════════════════════════

namespace road_engine {

// Version information accessors
inline constexpr int versionMajor() { return ROAD_ENGINE_VERSION_MAJOR; }
inline constexpr int versionMinor() { return ROAD_ENGINE_VERSION_MINOR; }
inline constexpr int versionPatch() { return ROAD_ENGINE_VERSION_PATCH; }
inline const char* versionString() { return ROAD_ENGINE_VERSION_STRING; }

} // namespace road_engine

// ═══════════════════════════════════════════════════════════
// Public API Includes
// ═══════════════════════════════════════════════════════════
//
// These includes pull in the actual type definitions and function
// implementations. Consumers only need to include this header.

#include "road_error.hpp"
#include "geometry.hpp"
#include "geometry_segment.hpp"
#include "road.hpp"
#include "road_v2.hpp"
#include "mesh.hpp"
#include "road_tools.hpp"

// nlohmann::json is used by parseRoad/serializeRoad declarations
#include <nlohmann/json.hpp>

// ═══════════════════════════════════════════════════════════
// Public API Function Declarations
// ═══════════════════════════════════════════════════════════
//
// @section RoadCreationFunctions Road Creation Functions
// Functions for creating roads using different geometric primitives.

namespace geo {

/**
 * @brief Creates a circular arc road segment.
 *
 * Creates a circular arc with tangent continuity at the start point.
 * The arc starts tangent to startDirection and passes through endPoint.
 *
 * @param start Starting point of the arc
 * @param startDirection Tangent direction at the start (unit vector)
 * @param end Ending point of the arc
 * @param numControlPoints Number of control points to generate (default 8)
 * @param params Road parameters (width, laneCount, etc.)
 * @return Road A new road containing the circular arc
 *
 * @ingroup RoadCreation
 */
Road createCircleArc(
    const Point2D& start,
    const Point2D& startDirection,
    const Point2D& end,
    int numControlPoints,
    const RoadToolParams& params
);

/**
 * @brief Creates a clothoid (Euler spiral) road segment.
 *
 * Creates an Euler spiral transition curve with G2 continuity.
 * The clothoid starts tangent to startDirection and ends tangent to endDirection.
 *
 * @param start Starting point of the spiral
 * @param startDirection Tangent direction at the start (unit vector)
 * @param end Ending point of the spiral
 * @param endDirection Tangent direction at the end (unit vector)
 * @param numControlPoints Number of control points to generate (default 8)
 * @param params Road parameters (width, laneCount, etc.)
 * @return Road A new road containing the clothoid arc
 *
 * @ingroup RoadCreation
 */
Road createClothoidArc(
    const Point2D& start,
    const Point2D& startDirection,
    const Point2D& end,
    const Point2D& endDirection,
    int numControlPoints,
    const RoadToolParams& params
);

/**
 * @brief Creates a cubic Bezier curve road segment.
 *
 * Creates a cubic Bezier curve with user-controlled handles.
 * Provides smooth, aesthetically pleasing curves without G2 continuity.
 *
 * @param start Starting point of the curve
 * @param handleOut Absolute position of the start handle (start + offset)
 * @param end Ending point of the curve
 * @param handleIn Absolute position of the end handle (end + offset)
 * @param params Road parameters (width, laneCount, etc.)
 * @return Road A new road containing the Bezier curve
 *
 * @ingroup RoadCreation
 */
Road createBezierArc(
    const Point2D& start,
    const Point2D& handleOut,
    const Point2D& end,
    const Point2D& handleIn,
    const RoadToolParams& params = {}
);

} // namespace geo

// ═══════════════════════════════════════════════════════════
// Sampling Functions
// ═══════════════════════════════════════════════════════════
//
// @section SamplingFunctions Sampling Functions
// Functions for sampling road centerlines at various resolutions.

namespace geo {

/**
 * @brief Samples a road centerline at uniformly distributed points.
 *
 * Samples the centerline using arc-length based distribution,
 * ensuring uniform sample density regardless of segment curvature.
 *
 * @param road The road to sample
 * @param numSamples Number of sample points (default 24)
 * @return std::vector<Point2D> Vector of sampled points
 *
 * @ingroup Sampling
 */
std::vector<Point2D> sampleCenterline(const Road& road, int numSamples = 24);

/**
 * @brief Samples a road centerline in 3D with elevation.
 *
 * Samples the centerline with interpolated elevation data.
 *
 * @param road The road to sample
 * @param numSamples Number of sample points (default 24)
 * @return std::vector<Point3D> Vector of 3D sampled points
 *
 * @ingroup Sampling
 */
std::vector<Point3D> sampleCenterline3D(const Road& road, int numSamples = 24);

/**
 * @brief Evaluates position and heading at a specific arc length.
 *
 * Returns the position and heading at the given distance along the road.
 *
 * @param road The road to query
 * @param s Arc length distance from road start (meters)
 * @param x Output: x coordinate at distance s
 * @param y Output: y coordinate at distance s
 * @param heading Output: heading (radians) at distance s
 *
 * @ingroup Sampling
 */
void sampleAtLength(const Road& road, double s, double& x, double& y, double& heading);

/**
 * @brief Gets the left edge polyline of a road.
 *
 * @param road The road to query
 * @param numSamples Number of sample points (default 24)
 * @return std::vector<Point2D> Left edge points
 *
 * @ingroup Sampling
 */
std::vector<Point2D> sampleLeftEdge(const Road& road, int numSamples = 24);

/**
 * @brief Gets the right edge polyline of a road.
 *
 * @param road The road to query
 * @param numSamples Number of sample points (default 24)
 * @return std::vector<Point2D> Right edge points
 *
 * @ingroup Sampling
 */
std::vector<Point2D> sampleRightEdge(const Road& road, int numSamples = 24);

} // namespace geo

// ═══════════════════════════════════════════════════════════
// Mesh Generation Functions
// ═══════════════════════════════════════════════════════════
//
// @section MeshFunctions Mesh Generation Functions
// Functions for generating renderable triangle meshes from roads.

namespace geo {

/**
 * @brief Generates a road mesh (triangle strip along centerline).
 *
 * Creates a triangle strip mesh for a road surface with miter-joint
 * edge offsets for correct curve handling and arc-length UV mapping.
 *
 * @param road The road to generate mesh for
 * @param numSamples Number of samples along the centerline (default 32)
 * @return MeshData Raw mesh data (vertices, normals, UVs, indices)
 *
 * @ingroup MeshGeneration
 */
MeshData generateRoadMesh(const Road& road, int numSamples);

/**
 * @brief Generates a mesh for an intersection polygon.
 *
 * Triangulates an intersection polygon using ear-clipping algorithm.
 *
 * @param ix The generated intersection
 * @param z Z-height for the mesh (default 0)
 * @return MeshData Raw mesh data for the intersection
 *
 * @ingroup MeshGeneration
 */
MeshData generateIntersectionMesh(const GeneratedIntersection& ix, double z);

/**
 * @brief Triangulates a simple polygon.
 *
 * Uses ear-clipping algorithm with automatic winding order correction.
 *
 * @param polygon Input polygon vertices (must be simple, at least 3 points)
 * @return std::vector<uint32_t> Triangle indices
 *
 * @ingroup MeshGeneration
 */
std::vector<uint32_t> triangulatePolygon(const std::vector<Point2D>& polygon);

/**
 * @brief Generates lane boundary polylines for a road.
 *
 * Returns a list of polylines, one per lane boundary.
 *
 * @param road The road to generate boundaries for
 * @param numSamples Number of samples along centerline (default 32)
 * @return std::vector<std::vector<Point2D>> Lane boundary polylines
 *
 * @ingroup MeshGeneration
 */
std::vector<std::vector<Point2D>> generateLaneBoundaries(const Road& road, int numSamples);

} // namespace geo

// ═══════════════════════════════════════════════════════════
// I/O Functions (Serialization)
// ═══════════════════════════════════════════════════════════
//
// @section IOFunctions I/O Functions
// Functions for serializing and deserializing roads to/from JSON.

namespace geo {

/**
 * @brief Parses a road from JSON format.
 *
 * Deserializes a road from the OpenGeoStudio JSON wire format.
 * Supports both formatVersion=1 (legacy) and formatVersion=2.
 *
 * @param json JSON object containing road data
 * @return Road The deserialized road
 * @throws std::runtime_error if parsing fails
 *
 * @ingroup Serialization
 */
Road parseRoad(const nlohmann::json& json);

/**
 * @brief Serializes a road to JSON format.
 *
 * Serializes a road to the OpenGeoStudio JSON wire format.
 * Output always uses formatVersion=2 with SegmentMetadata.
 *
 * @param road The road to serialize
 * @return nlohmann::json JSON representation of the road
 *
 * @ingroup Serialization
 */
nlohmann::json serializeRoad(const Road& road);

/**
 * @brief Exports roads to OpenDRIVE XML format.
 *
 * Converts roads to OpenDRIVE 1.6 format for use with
 * driving simulators (CARLA, Vires VTD, SCANeR, etc.).
 *
 * @param roads Vector of roads to export
 * @param refLat Reference latitude for coordinate conversion
 * @param refLon Reference longitude for coordinate conversion
 * @return std::string OpenDRIVE XML string
 *
 * @ingroup Serialization
 */
std::string exportOpenDrive(
    const std::vector<Road>& roads,
    double refLat = 0.0,
    double refLon = 0.0
);

} // namespace geo

// ═══════════════════════════════════════════════════════════
// Adapter Functions
// ═══════════════════════════════════════════════════════════
//
// @section AdapterFunctions Adapter Functions
// Functions for converting between legacy Road and RoadV2 models.

namespace geo {

/**
 * @brief Converts legacy Road to RoadV2 using exact reconstruction.
 *
 * Uses SegmentMetadata when available (arc, spiral, line).
 * Uses bezier handles when present (exact absolute control points).
 * Emits warnings for missing metadata or unknown types.
 *
 * @param legacy The legacy Road to convert
 * @return RoadV2 The new segment-based road representation
 *
 * @ingroup Adapter
 */
RoadV2 roadToV2(const Road& legacy);

/**
 * @brief Converts legacy Road to RoadV2 using exact reconstruction with diagnostics.
 *
 * @param legacy The legacy Road to convert
 * @param report Output: diagnostics about the conversion
 * @return RoadV2 The new segment-based road representation
 *
 * @ingroup Adapter
 */
RoadV2 roadToV2(const Road& legacy, AdapterReport& report);

/**
 * @brief Converts legacy Road to RoadV2 using legacy compatibility mode.
 *
 * Preserves rendered geometry without attempting to recover original
 * authoring primitive. Corner points become LineSegments,
 * handle points become BezierSegments.
 *
 * @deprecated Since version 1.0.0, will be removed in version 1.2.0.
 *             Use roadToV2() for exact reconstruction or roadToV2Auto()
 *             for automatic format-version detection.
 *
 * @param legacy The legacy Road to convert
 * @return RoadV2 The new segment-based road representation
 *
 * @ingroup Adapter
 */
[[deprecated("since version 1.0.0, will be removed in version 1.2.0. Use roadToV2() or roadToV2Auto()")]]
RoadV2 roadToV2Legacy(const Road& legacy);

/**
 * @brief Converts legacy Road to RoadV2 using legacy compatibility mode with diagnostics.
 *
 * @deprecated Since version 1.0.0, will be removed in version 1.2.0.
 *             Use roadToV2() for exact reconstruction or roadToV2Auto()
 *             for automatic format-version detection.
 *
 * @param legacy The legacy Road to convert
 * @param report Output: diagnostics about the conversion
 * @return RoadV2 The new segment-based road representation
 *
 * @ingroup Adapter
 */
[[deprecated("since version 1.0.0, will be removed in version 1.2.0. Use roadToV2() or roadToV2Auto()")]]
RoadV2 roadToV2Legacy(const Road& legacy, AdapterReport& report);

/**
 * @brief Converts legacy Road to RoadV2 with format-version auto-detection.
 *
 * Automatically selects between exact and legacy paths based on
 * Road::formatVersion (>=2 uses exact, <2 uses legacy).
 *
 * @param legacy The legacy Road to convert
 * @return RoadV2 The new segment-based road representation
 *
 * @ingroup Adapter
 */
RoadV2 roadToV2Auto(const Road& legacy);

/**
 * @brief Converts legacy Road to RoadV2 with format-version auto-detection and diagnostics.
 *
 * @param legacy The legacy Road to convert
 * @param report Output: diagnostics about the conversion
 * @return RoadV2 The new segment-based road representation
 *
 * @ingroup Adapter
 */
RoadV2 roadToV2Auto(const Road& legacy, AdapterReport& report);

/**
 * @brief Converts RoadV2 back to legacy Road representation.
 *
 * Converts RoadV2 segments back to ControlPoints with appropriate
 * metadata for exact round-trip serialization.
 *
 * @param v2 The RoadV2 to convert
 * @return Road The legacy road representation
 *
 * @ingroup Adapter
 */
Road roadFromV2(const RoadV2& v2);

/**
 * @brief Converts RoadV2 back to legacy Road with diagnostics.
 *
 * @param v2 The RoadV2 to convert
 * @param report Output: diagnostics about the conversion
 * @return Road The legacy road representation
 *
 * @ingroup Adapter
 */
Road roadFromV2(const RoadV2& v2, ReverseAdapterReport& report);

} // namespace geo

// ═══════════════════════════════════════════════════════════
// Version Information
// ═══════════════════════════════════════════════════════════

#endif // ROAD_ENGINE_HPP