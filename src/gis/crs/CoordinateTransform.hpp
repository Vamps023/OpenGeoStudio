#pragma once

// ============================================================
// CoordinateTransform.hpp — PROJ-backed coordinate transformation
//
// Transforms points and bounding boxes between CRSDefinitions.
// Uses PROJ's coordinate operation API with area-of-use
// selection for best-accuracy transformations.
//
// Key features:
//   - Single point transform (x, y, optional z)
//   - Batch point transform
//   - Bounding box transform
//   - Area-of-use based operation selection
//   - Vertical component handling
// ============================================================

#include "CRS.hpp"
#include <vector>
#include <string>
#include <memory>
#include <mutex>
#include <optional>

using ProjContext = void;
using ProjObject = void;

namespace gis {

// A 2D/3D point in a CRS
struct GeoPoint {
    double x = 0.0;   // longitude or easting
    double y = 0.0;   // latitude or northing
    double z = 0.0;   // elevation (optional)
    bool hasZ = false;

    GeoPoint() = default;
    GeoPoint(double x_, double y_) : x(x_), y(y_) {}
    GeoPoint(double x_, double y_, double z_) : x(x_), y(y_), z(z_), hasZ(true) {}
};

// Transform result with accuracy info
struct TransformResult {
    bool success = false;
    GeoPoint point;
    double accuracy = -1.0;       // accuracy in metres (-1 = unknown)
    std::string operationName;    // e.g. "UTM zone 43N (≈1m)"
    std::string errorMessage;
};

// ============================================================
// CoordinateTransform — PROJ-backed
// ============================================================
class CoordinateTransform {
public:
    CoordinateTransform();
    CoordinateTransform(const CRSDefinition& source,
                        const CRSDefinition& target);
    CoordinateTransform(const CRSDefinition& source,
                        const CRSDefinition& target,
                        const BoundingBox& areaOfInterest);
    ~CoordinateTransform();

    // Non-copyable (owns PROJ resources)
    CoordinateTransform(const CoordinateTransform&) = delete;
    CoordinateTransform& operator=(const CoordinateTransform&) = delete;

    CoordinateTransform(CoordinateTransform&& other) noexcept;
    CoordinateTransform& operator=(CoordinateTransform&& other) noexcept;

    // --------------------------------------------------------
    // State
    // --------------------------------------------------------

    bool isValid() const;
    bool isShortCircuited() const;  // source == target

    const CRSDefinition& source() const { return m_source; }
    const CRSDefinition& target() const { return m_target; }

    // Get the operation name (if available)
    std::string operationName() const;

    // Get the accuracy (if available)
    double accuracy() const;

    // --------------------------------------------------------
    // Transform operations
    // --------------------------------------------------------

    // Transform a single point
    TransformResult transform(const GeoPoint& point) const;

    // Transform a single point (convenience for 2D)
    TransformResult transform(double x, double y) const;

    // Transform a single point (convenience for 3D)
    TransformResult transform(double x, double y, double z) const;

    // Transform multiple points in batch
    std::vector<TransformResult> transformBatch(
        const std::vector<GeoPoint>& points) const;

    // Transform a bounding box (returns the bounding box of
    // the transformed corners + optionally sampled edge points)
    BoundingBox transformBounds(const BoundingBox& bounds,
                                 int sampleCount = 8) const;

private:
    void initialize();
    void cleanup();

    CRSDefinition m_source;
    CRSDefinition m_target;
    BoundingBox m_areaOfInterest;
    bool m_hasAreaOfInterest = false;

    // PROJ objects (lazily initialized)
    ProjContext* m_ctx = nullptr;
    ProjObject* m_pj = nullptr;
    mutable std::mutex m_mutex;
    bool m_valid = false;
    double m_accuracy = -1.0;
    std::string m_operationName;
};

// ============================================================
// Convenience free functions
// =================================================-----------

// Quick transform a single point between two CRS
TransformResult transformPoint(
    const CRSDefinition& source,
    const CRSDefinition& target,
    const GeoPoint& point);

// Quick transform a single point with area of interest
TransformResult transformPoint(
    const CRSDefinition& source,
    const CRSDefinition& target,
    const BoundingBox& areaOfInterest,
    const GeoPoint& point);

// Quick transform bounds
BoundingBox transformBounds(
    const CRSDefinition& source,
    const CRSDefinition& target,
    const BoundingBox& bounds);

} // namespace gis
