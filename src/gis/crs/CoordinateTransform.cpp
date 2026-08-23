// ============================================================
// CoordinateTransform.cpp — PROJ-backed coordinate transformation
// ============================================================

#include "CoordinateTransform.hpp"
#include "CRSManager.hpp"

#include <proj.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace gis {

// ============================================================
// Constructor / Destructor
// ============================================================
CoordinateTransform::CoordinateTransform() = default;

CoordinateTransform::CoordinateTransform(const CRSDefinition& source,
                                         const CRSDefinition& target)
    : m_source(source), m_target(target) {
    initialize();
}

CoordinateTransform::CoordinateTransform(const CRSDefinition& source,
                                         const CRSDefinition& target,
                                         const BoundingBox& areaOfInterest)
    : m_source(source), m_target(target),
      m_areaOfInterest(areaOfInterest),
      m_hasAreaOfInterest(true) {
    initialize();
}

CoordinateTransform::~CoordinateTransform() {
    cleanup();
}

CoordinateTransform::CoordinateTransform(CoordinateTransform&& other) noexcept
    : m_source(std::move(other.m_source)),
      m_target(std::move(other.m_target)),
      m_areaOfInterest(std::move(other.m_areaOfInterest)),
      m_hasAreaOfInterest(other.m_hasAreaOfInterest),
      m_ctx(other.m_ctx),
      m_pj(other.m_pj),
      m_valid(other.m_valid),
      m_accuracy(other.m_accuracy),
      m_operationName(std::move(other.m_operationName)) {
    other.m_ctx = nullptr;
    other.m_pj = nullptr;
    other.m_valid = false;
}

CoordinateTransform& CoordinateTransform::operator=(CoordinateTransform&& other) noexcept {
    if (this != &other) {
        cleanup();
        m_source = std::move(other.m_source);
        m_target = std::move(other.m_target);
        m_areaOfInterest = std::move(other.m_areaOfInterest);
        m_hasAreaOfInterest = other.m_hasAreaOfInterest;
        m_ctx = other.m_ctx;
        m_pj = other.m_pj;
        m_valid = other.m_valid;
        m_accuracy = other.m_accuracy;
        m_operationName = std::move(other.m_operationName);
        other.m_ctx = nullptr;
        other.m_pj = nullptr;
        other.m_valid = false;
    }
    return *this;
}

// ============================================================
// Initialize PROJ transformation
// ============================================================
void CoordinateTransform::initialize() {
    if (!m_source.isValid() || !m_target.isValid()) {
        m_valid = false;
        return;
    }

    // Short-circuit: same CRS
    if (m_source == m_target) {
        m_valid = true;
        return;
    }

    m_ctx = reinterpret_cast<ProjContext*>(proj_context_create());
    if (!m_ctx) {
        m_valid = false;
        return;
    }

    PJ_CONTEXT* ctx = reinterpret_cast<PJ_CONTEXT*>(m_ctx);

    // Create the coordinate operation
    // Use proj_create_crs_to_crs_from_pj for area-of-use selection
    PJ* sourcePj = proj_create_from_database(ctx,
        m_source.authority.c_str(),
        std::to_string(m_source.code).c_str(),
        PJ_CATEGORY_CRS, false, nullptr);

    if (!sourcePj) {
        proj_context_destroy(ctx);
        m_ctx = nullptr;
        m_valid = false;
        return;
    }

    PJ* targetPj = proj_create_from_database(ctx,
        m_target.authority.c_str(),
        std::to_string(m_target.code).c_str(),
        PJ_CATEGORY_CRS, false, nullptr);

    if (!targetPj) {
        proj_destroy(sourcePj);
        proj_context_destroy(ctx);
        m_ctx = nullptr;
        m_valid = false;
        return;
    }

    // Create the transformation with optional area of interest
    PJ_AREA* area = nullptr;
    if (m_hasAreaOfInterest && m_areaOfInterest.isValid()) {
        area = proj_area_create();
        proj_area_set_bbox(area,
            m_areaOfInterest.west,
            m_areaOfInterest.south,
            m_areaOfInterest.east,
            m_areaOfInterest.north);
    }

    m_pj = reinterpret_cast<ProjObject*>(
        proj_create_crs_to_crs_from_pj(ctx, sourcePj, targetPj, area, nullptr));

    if (area) proj_area_destroy(area);
    proj_destroy(sourcePj);
    proj_destroy(targetPj);

    if (!m_pj) {
        proj_context_destroy(ctx);
        m_ctx = nullptr;
        m_valid = false;
        return;
    }

    // Normalize for visualization: ensures lon/lat order for geographic CRS
    // instead of the native axis order (which may be lat/lon for EPSG:4326)
    PJ* normalizedPj = proj_normalize_for_visualization(ctx,
        reinterpret_cast<PJ*>(m_pj));
    if (normalizedPj) {
        proj_destroy(reinterpret_cast<PJ*>(m_pj));
        m_pj = reinterpret_cast<ProjObject*>(normalizedPj);
    }

    // Get operation info (accuracy, name) from the transformation PJ
    PJ* pjObj = reinterpret_cast<PJ*>(m_pj);
    const char* name = proj_get_name(pjObj);
    if (name) m_operationName = name;

    // Get accuracy if available
    double acc = proj_coordoperation_get_accuracy(ctx, pjObj);
    if (acc >= 0.0) {
        m_accuracy = acc;
    }

    m_valid = true;
}

void CoordinateTransform::cleanup() {
    if (m_pj) {
        proj_destroy(reinterpret_cast<PJ*>(m_pj));
        m_pj = nullptr;
    }
    if (m_ctx) {
        proj_context_destroy(reinterpret_cast<PJ_CONTEXT*>(m_ctx));
        m_ctx = nullptr;
    }
    m_valid = false;
}

// ============================================================
// State queries
// ============================================================
bool CoordinateTransform::isValid() const {
    return m_valid;
}

bool CoordinateTransform::isShortCircuited() const {
    return m_source == m_target;
}

std::string CoordinateTransform::operationName() const {
    return m_operationName;
}

double CoordinateTransform::accuracy() const {
    return m_accuracy;
}

// ============================================================
// Transform a single point
// ============================================================
TransformResult CoordinateTransform::transform(const GeoPoint& point) const {
    TransformResult result;

    if (!m_valid) {
        result.errorMessage = "Transform is not valid";
        return result;
    }

    // Short-circuit
    if (isShortCircuited()) {
        result.success = true;
        result.point = point;
        result.accuracy = 0.0;
        return result;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_pj || !m_ctx) {
        result.errorMessage = "PROJ objects not initialized";
        return result;
    }

    PJ* pjObj = reinterpret_cast<PJ*>(m_pj);

    // PROJ uses (lon, lat, h) order for geographic CRS
    // For projected CRS, it uses the native axis order
    // proj_trans takes a PJ_COORD which is (x, y, z, t)
    PJ_COORD input;
    input.xyzt.x = point.x;
    input.xyzt.y = point.y;
    input.xyzt.z = point.hasZ ? point.z : 0.0;
    input.xyzt.t = 0.0;

    PJ_COORD output = proj_trans(pjObj, PJ_FWD, input);

    if (proj_errno(pjObj) != 0) {
        result.errorMessage = "PROJ transform failed: " +
            std::string(proj_errno_string(proj_errno(pjObj)));
        return result;
    }

    result.success = true;
    result.point.x = output.xyzt.x;
    result.point.y = output.xyzt.y;
    result.point.z = output.xyzt.z;
    result.point.hasZ = point.hasZ;
    result.accuracy = m_accuracy;
    result.operationName = m_operationName;

    return result;
}

TransformResult CoordinateTransform::transform(double x, double y) const {
    return transform(GeoPoint(x, y));
}

TransformResult CoordinateTransform::transform(double x, double y, double z) const {
    return transform(GeoPoint(x, y, z));
}

// ============================================================
// Batch transform
// ============================================================
std::vector<TransformResult> CoordinateTransform::transformBatch(
    const std::vector<GeoPoint>& points) const {

    std::vector<TransformResult> results;
    results.reserve(points.size());

    for (const auto& p : points) {
        results.push_back(transform(p));
    }

    return results;
}

// ============================================================
// Transform bounding box
// ============================================================
BoundingBox CoordinateTransform::transformBounds(const BoundingBox& bounds,
                                                   int sampleCount) const {
    if (!m_valid || isShortCircuited()) {
        return bounds;
    }

    // Sample the edges of the bounding box and transform each point
    // to get the bounding box of the transformed region
    std::vector<GeoPoint> samples;

    // Corners
    samples.emplace_back(bounds.west, bounds.south);
    samples.emplace_back(bounds.east, bounds.south);
    samples.emplace_back(bounds.east, bounds.north);
    samples.emplace_back(bounds.west, bounds.north);

    // Edge samples
    if (sampleCount > 2) {
        double dlon = (bounds.east - bounds.west) / (sampleCount - 1);
        double dlat = (bounds.north - bounds.south) / (sampleCount - 1);

        // Top and bottom edges
        for (int i = 0; i < sampleCount; ++i) {
            double lon = bounds.west + dlon * i;
            samples.emplace_back(lon, bounds.south);
            samples.emplace_back(lon, bounds.north);
        }

        // Left and right edges
        for (int i = 1; i < sampleCount - 1; ++i) {
            double lat = bounds.south + dlat * i;
            samples.emplace_back(bounds.west, lat);
            samples.emplace_back(bounds.east, lat);
        }
    }

    auto results = transformBatch(samples);

    BoundingBox result;
    bool first = true;
    for (const auto& r : results) {
        if (!r.success) continue;
        if (first) {
            result.west = r.point.x;
            result.east = r.point.x;
            result.south = r.point.y;
            result.north = r.point.y;
            first = false;
        } else {
            result.west = std::min(result.west, r.point.x);
            result.east = std::max(result.east, r.point.x);
            result.south = std::min(result.south, r.point.y);
            result.north = std::max(result.north, r.point.y);
        }
    }

    return result;
}

// ============================================================
// Convenience free functions
// ============================================================
TransformResult transformPoint(
    const CRSDefinition& source,
    const CRSDefinition& target,
    const GeoPoint& point) {

    CoordinateTransform t(source, target);
    return t.transform(point);
}

TransformResult transformPoint(
    const CRSDefinition& source,
    const CRSDefinition& target,
    const BoundingBox& areaOfInterest,
    const GeoPoint& point) {

    CoordinateTransform t(source, target, areaOfInterest);
    return t.transform(point);
}

BoundingBox transformBounds(
    const CRSDefinition& source,
    const CRSDefinition& target,
    const BoundingBox& bounds) {

    CoordinateTransform t(source, target);
    return t.transformBounds(bounds);
}

} // namespace gis
