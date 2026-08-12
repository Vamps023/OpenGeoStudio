#pragma once

// ═══════════════════════════════════════════════════════════
// Road Mesh Generator — Phase 2.7: Renderable Geometry
// ═══════════════════════════════════════════════════════════
//
// @file road_mesh_generator.hpp
// @brief Generates renderable mesh from LaneNetwork and RoadMarkNetwork.
//        No lane math, no sampling, no marking logic — just tessellation.
//
// @section Architecture
//
//   lane_engine.hpp          = Data model
//   lane_geometry.hpp        = Evaluation
//   lane_sampling.hpp        = Sampling
//   lane_network.hpp         = Persistent lane network
//   road_mark_generator.hpp  = Mark generation
//   road_mesh_generator.hpp  = Mesh generation (THIS FILE)
//
// @section Responsibility
// Converts already-sampled lane geometry into triangle strips.
// Each lane is tessellated independently as a strip, making:
//   - Per-lane material assignment easy
//   - Lane editing updates efficient (only re-tessellate changed lane)
//   - LOD straightforward (simplify per-lane)
//
// @section Mesh Split by Material
// RoadMesh contains multiple MeshSections, one per material:
//   - Asphalt (pavement)
//   - White markings
//   - Yellow markings
//   - (Future: curb, shoulder, sidewalk, guard rail)
//
// @section UV Strategy
// Pavement:
//   U = s (arc-length along reference line, in meters)
//   V = lateral offset from center (right-positive, in meters)
// Markings:
//   U = distance along marking (in meters)
//   V = lateral position across marking width (0 to 1)
//
// @section Winding Order
// Triangles are CCW when viewed from above (positive Z = up).
// Normal is (0, 0, 1) for flat roads.
//
// @section No Renderer Dependency
// Output is generic MeshVertex/MeshSection/RoadMesh.
// Can be converted to Babylon.js, Three.js, glTF, Unreal, Unity, etc.
//
// @section API Freeze
// NOT YET FROZEN. Will be frozen at Phase 2 Complete.

#include "geometry.hpp"
#include "lane_network.hpp"
#include "road_mark_generator.hpp"
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace geo {

// ═══════════════════════════════════════════════════════════
// MaterialType — Identifies the material of a mesh section
// ═══════════════════════════════════════════════════════════
//
enum class MaterialType : uint8_t {
    Asphalt,            // Road pavement
    WhiteMarking,       // White lane marking
    YellowMarking,      // Yellow lane marking
    Curb,               // Curb stone (future)
    Shoulder,           // Gravel shoulder (future)
    Sidewalk,           // Sidewalk (future)
    Grass,              // Grass median (future)
    Unknown             // Fallback
};

// Convert material type to string (for debugging / serialization)
inline const char* materialTypeName(MaterialType t) {
    switch (t) {
        case MaterialType::Asphalt:       return "asphalt";
        case MaterialType::WhiteMarking:  return "white_marking";
        case MaterialType::YellowMarking: return "yellow_marking";
        case MaterialType::Curb:          return "curb";
        case MaterialType::Shoulder:      return "shoulder";
        case MaterialType::Sidewalk:      return "sidewalk";
        case MaterialType::Grass:         return "grass";
        default:                          return "unknown";
    }
}

// ═══════════════════════════════════════════════════════════
// MeshVertex — A single vertex in the mesh
// ═══════════════════════════════════════════════════════════
//
struct MeshVertex {
    Point3D position;       // world-space (x, y, z)
    Vec3 normal;            // unit normal vector
    Vec2 uv;                // texture coordinates

    MeshVertex() = default;
    MeshVertex(const Point3D& pos, const Vec3& norm, const Vec2& tex)
        : position(pos), normal(norm), uv(tex) {}
};

// ═══════════════════════════════════════════════════════════
// MeshSection — A section of mesh with one material
// ═══════════════════════════════════════════════════════════
//
struct MeshSection {
    MaterialType material = MaterialType::Unknown;
    std::string materialName;                   // human-readable
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;

    int vertexCount() const { return static_cast<int>(vertices.size()); }
    int indexCount() const { return static_cast<int>(indices.size()); }
    int triangleCount() const { return indexCount() / 3; }

    // Merge another section into this one (must be same material)
    void merge(const MeshSection& other) {
        uint32_t offset = static_cast<uint32_t>(vertices.size());
        vertices.insert(vertices.end(), other.vertices.begin(), other.vertices.end());
        for (uint32_t idx : other.indices) {
            indices.push_back(idx + offset);
        }
    }
};

// ═══════════════════════════════════════════════════════════
// RoadMesh — Complete road mesh, split by material
// ═══════════════════════════════════════════════════════════
//
struct RoadMesh {
    std::vector<MeshSection> sections;

    int numSections() const { return static_cast<int>(sections.size()); }
    int totalVertices() const {
        int total = 0;
        for (const auto& s : sections) total += s.vertexCount();
        return total;
    }
    int totalTriangles() const {
        int total = 0;
        for (const auto& s : sections) total += s.triangleCount();
        return total;
    }
    int totalIndices() const {
        int total = 0;
        for (const auto& s : sections) total += s.indexCount();
        return total;
    }

    // Find section by material type
    MeshSection* findSection(MaterialType type) {
        for (auto& s : sections) {
            if (s.material == type) return &s;
        }
        return nullptr;
    }
    const MeshSection* findSection(MaterialType type) const {
        for (const auto& s : sections) {
            if (s.material == type) return &s;
        }
        return nullptr;
    }

    // Get or create a section for a material
    MeshSection& getOrCreateSection(MaterialType type) {
        auto* s = findSection(type);
        if (s) return *s;
        MeshSection newSection;
        newSection.material = type;
        newSection.materialName = materialTypeName(type);
        sections.push_back(std::move(newSection));
        return sections.back();
    }
};

// ═══════════════════════════════════════════════════════════
// Mesh generation parameters
// ═══════════════════════════════════════════════════════════
//
struct MeshGenParams {
    double zHeight = 0.0;           // Z height for flat roads
    double uvScale = 1.0;           // UV scale factor (1m = 1 UV unit)
    bool generateNormals = true;    // compute normals (always (0,0,1) for flat)
    bool generateMarkings = true;   // also generate marking mesh
    double markingElevation = 0.01; // markings slightly above pavement
    double dashEpsilon = 0.001;     // minimum dash length to render

    MeshGenParams() = default;
};

// ═══════════════════════════════════════════════════════════
// Internal: Generate triangle strip for a single lane
// ═══════════════════════════════════════════════════════════
//
// Creates a triangle strip between the inner and outer boundaries
// of a lane. Uses the boundary samples from LaneNetwork.
//
namespace detail {

inline MeshSection generateLaneStrip(
    const LaneCenterline& centerline,
    const LaneNetwork& network,
    const MeshGenParams& params
) {
    MeshSection section;
    section.material = MaterialType::Asphalt;
    section.materialName = "asphalt";

    if (centerline.numSamples() < 2) return section;

    // Find inner and outer boundaries for this lane
    // Inner: boundary between this lane and the lane closer to center
    // Outer: boundary between this lane and the lane farther from center

    // For lane ID > 0 (right side): inner = boundary(innerLaneId-1, this), outer = boundary(this, this+1 or edge)
    // For lane ID < 0 (left side): inner = boundary(innerLaneId+1, this), outer = boundary(this, this-1 or edge)
    // For lane ID = 0 (center): both inner and outer are at offset 0

    // We need the boundary samples. Find them in the network.
    // The boundaries in LaneNetwork have samples at the same s-positions.

    // Strategy: use the centerline samples and compute left/right edges
    // from the SamplePoint's laneOffset and the lane width.
    // But we already have boundary samples in LaneNetwork.
    // For simplicity and correctness, we'll sample the boundaries directly.

    // Actually, the cleanest approach: for each centerline sample point,
    // we know the s and laneOffset. The inner edge is at laneOffset - width/2,
    // and the outer edge is at laneOffset + width/2.
    // But we don't have the width per-sample.
    //
    // Better: use the boundary samples from LaneNetwork.
    // Each lane has an inner and outer boundary.
    // For lane 1 (right): inner = boundary(0, 1), outer = boundary(1, 2) or boundary(1, 0) if edge
    // For lane -1 (left): inner = boundary(0, -1), outer = boundary(-1, -2) or boundary(-1, 0) if edge

    // Find inner and outer boundary samples
    // We'll use the centerline samples as the "spine" and offset
    // using the normal at each point.

    // Simplest correct approach: generate vertices from centerline samples
    // by offsetting left and right by half the lane width.
    // The lane width can be computed from the boundary offsets.

    // Actually, the most robust approach: use the boundary samples directly.
    // Find the inner and outer boundary for this lane, and create
    // a triangle strip between them.

    // Find boundaries that match this lane
    const std::vector<SamplePoint>* innerBoundary = nullptr;
    const std::vector<SamplePoint>* outerBoundary = nullptr;

    for (const auto& b : network.boundaries) {
        // Check if this boundary is relevant to our lane
        if (centerline.laneId > 0) {
            // Right side: inner = boundary with laneId-1 (or 0), outer = boundary with laneId+1 (or edge)
            if (b.innerLaneId == centerline.laneId - 1 ||
                (centerline.laneId == 1 && b.innerLaneId == 0)) {
                if (b.outerLaneId == centerline.laneId) {
                    innerBoundary = &b.samples;
                }
            }
            if (b.innerLaneId == centerline.laneId) {
                outerBoundary = &b.samples;
            }
        } else if (centerline.laneId < 0) {
            // Left side: inner = boundary with laneId+1 (or 0), outer = boundary with laneId-1 (or edge)
            if (b.innerLaneId == centerline.laneId + 1 ||
                (centerline.laneId == -1 && b.innerLaneId == 0)) {
                if (b.outerLaneId == centerline.laneId) {
                    innerBoundary = &b.samples;
                }
            }
            if (b.innerLaneId == centerline.laneId) {
                outerBoundary = &b.samples;
            }
        } else {
            // Center lane (id=0): both edges at offset 0
            // Use centerline itself for both
            innerBoundary = &centerline.samples;
            outerBoundary = &centerline.samples;
            break;
        }
    }

    // Fallback: if we can't find boundaries, use centerline with normal offset
    if (!innerBoundary || !outerBoundary) {
        // Use centerline samples and offset by lane width
        // This shouldn't happen in well-formed networks
        return section;
    }

    // For center lane, both boundaries are the same — skip (zero width)
    if (centerline.laneId == 0) {
        return section;
    }

    // Match inner and outer boundary samples by index
    // (They should have the same number of samples since they were
    //  sampled with the same params over the same s-range)
    int numInner = static_cast<int>(innerBoundary->size());
    int numOuter = static_cast<int>(outerBoundary->size());
    int numSamples = std::min(numInner, numOuter);

    if (numSamples < 2) return section;

    // Generate vertices: alternating inner, outer
    Vec3 upNormal(0, 0, 1);
    double z = params.zHeight;

    for (int i = 0; i < numSamples; i++) {
        const SamplePoint& inner = (*innerBoundary)[i];
        const SamplePoint& outer = (*outerBoundary)[i];

        // Inner vertex
        MeshVertex vInner;
        vInner.position = Point3D(inner.position.x, inner.position.y, z);
        vInner.normal = upNormal;
        // UV: U = s, V = lateral offset (inner edge)
        vInner.uv = Vec2(inner.s * params.uvScale, inner.laneOffset * params.uvScale);
        section.vertices.push_back(vInner);

        // Outer vertex
        MeshVertex vOuter;
        vOuter.position = Point3D(outer.position.x, outer.position.y, z);
        vOuter.normal = upNormal;
        // UV: U = s, V = lateral offset (outer edge)
        vOuter.uv = Vec2(outer.s * params.uvScale, outer.laneOffset * params.uvScale);
        section.vertices.push_back(vOuter);
    }

    // Generate indices: triangle strip → triangles
    // For N sample points, we have 2N vertices (inner0, outer0, inner1, outer1, ...)
    // Triangles: (inner_i, outer_i, inner_{i+1}) and (outer_i, outer_{i+1}, inner_{i+1})
    // Winding: CCW from above (Z-up)
    // For right lanes (positive ID), inner is on the left (higher y for straight road)
    // For left lanes (negative ID), inner is on the right
    // We need consistent CCW winding regardless of lane side.

    // For a right lane on a straight road going east:
    //   inner (y=0) is to the LEFT of outer (y=-3.5)
    //   Looking from above: inner is above outer in screen space
    //   Triangle (inner, outer, inner+1) → going left, right, forward = CW from above
    //   We need CCW, so swap: (inner, inner+1, outer) or (inner, outer, outer+1)
    //
    // Actually, let's think in terms of the road direction:
    //   For right lanes: inner edge has smaller |offset|, outer has larger
    //   Walking along the road: inner is on the left, outer on the right
    //   CCW from above: inner → outer → inner+1 is CW
    //   So we use: inner → inner+1 → outer (CCW)
    //   And: outer → inner+1 → outer+1 (CCW)

    for (int i = 0; i < numSamples - 1; i++) {
        uint32_t i0 = i * 2;       // inner_i
        uint32_t i1 = i * 2 + 1;   // outer_i
        uint32_t i2 = (i + 1) * 2;     // inner_{i+1}
        uint32_t i3 = (i + 1) * 2 + 1; // outer_{i+1}

        if (centerline.laneId > 0) {
            // Right lane: inner on left (higher y), outer on right (lower y)
            // CCW from above (Z-up): (inner_i, outer_i, inner_{i+1}) and (outer_i, outer_{i+1}, inner_{i+1})
            section.indices.push_back(i0);
            section.indices.push_back(i1);
            section.indices.push_back(i2);

            section.indices.push_back(i1);
            section.indices.push_back(i3);
            section.indices.push_back(i2);
        } else {
            // Left lane: inner on right (lower y), outer on left (higher y)
            // CCW from above: (inner_i, inner_{i+1}, outer_i) and (outer_i, inner_{i+1}, outer_{i+1})
            section.indices.push_back(i0);
            section.indices.push_back(i2);
            section.indices.push_back(i1);

            section.indices.push_back(i1);
            section.indices.push_back(i2);
            section.indices.push_back(i3);
        }
    }

    return section;
}

// ═══════════════════════════════════════════════════════════
// Internal: Generate marking mesh (solid line)
// ═══════════════════════════════════════════════════════════
//
inline MeshSection generateSolidMarkingMesh(
    const RoadMarkPolyline& mark,
    const MeshGenParams& params
) {
    MeshSection section;

    // Determine material from color
    if (mark.style.color == "yellow") {
        section.material = MaterialType::YellowMarking;
    } else {
        section.material = MaterialType::WhiteMarking;
    }
    section.materialName = materialTypeName(section.material);

    if (mark.numSamples() < 2) return section;

    double halfWidth = mark.style.width / 2.0;
    double z = params.zHeight + params.markingElevation;
    Vec3 upNormal(0, 0, 1);

    for (int i = 0; i < mark.numSamples(); i++) {
        const SamplePoint& sp = mark.samples[i];

        // Compute perpendicular direction from heading
        // Normal = (−sin(heading), cos(heading)) = left
        double nx = -std::sin(sp.heading);
        double ny = std::cos(sp.heading);

        Point2D leftPt(sp.position.x + nx * halfWidth,
                       sp.position.y + ny * halfWidth);
        Point2D rightPt(sp.position.x - nx * halfWidth,
                        sp.position.y - ny * halfWidth);

        // Left vertex
        MeshVertex vLeft;
        vLeft.position = Point3D(leftPt.x, leftPt.y, z);
        vLeft.normal = upNormal;
        // UV: U = distance along marking, V = 0 (left edge)
        vLeft.uv = Vec2(sp.s * params.uvScale, 0.0);
        section.vertices.push_back(vLeft);

        // Right vertex
        MeshVertex vRight;
        vRight.position = Point3D(rightPt.x, rightPt.y, z);
        vRight.normal = upNormal;
        // UV: U = distance along marking, V = 1 (right edge)
        vRight.uv = Vec2(sp.s * params.uvScale, 1.0);
        section.vertices.push_back(vRight);
    }

    // Triangle indices (CCW from above)
    for (int i = 0; i < mark.numSamples() - 1; i++) {
        uint32_t l0 = i * 2;
        uint32_t r0 = i * 2 + 1;
        uint32_t l1 = (i + 1) * 2;
        uint32_t r1 = (i + 1) * 2 + 1;

        // CCW: (left_i, left_{i+1}, right_i) and (right_i, left_{i+1}, right_{i+1})
        section.indices.push_back(l0);
        section.indices.push_back(l1);
        section.indices.push_back(r0);

        section.indices.push_back(r0);
        section.indices.push_back(l1);
        section.indices.push_back(r1);
    }

    return section;
}

// ═══════════════════════════════════════════════════════════
// Internal: Generate dashed marking mesh
// ═══════════════════════════════════════════════════════════
//
// For each dash, interpolates the exact position and heading at
// the dash start and end points, then generates a quad.
//
inline MeshSection generateDashedMarkingMesh(
    const RoadMarkPolyline& mark,
    const MeshGenParams& params
) {
    MeshSection section;

    if (mark.style.color == "yellow") {
        section.material = MaterialType::YellowMarking;
    } else {
        section.material = MaterialType::WhiteMarking;
    }
    section.materialName = materialTypeName(section.material);

    if (mark.numSamples() < 2) return section;

    // Get dash segments
    auto dashSegments = generateDashedSegments(mark);
    if (dashSegments.empty()) return section;

    double halfWidth = mark.style.width / 2.0;
    double z = params.zHeight + params.markingElevation;
    Vec3 upNormal(0, 0, 1);

    // Compute cumulative distances along the polyline
    std::vector<double> cumDist(mark.numSamples());
    cumDist[0] = 0.0;
    for (int i = 1; i < mark.numSamples(); i++) {
        double dx = mark.samples[i].position.x - mark.samples[i - 1].position.x;
        double dy = mark.samples[i].position.y - mark.samples[i - 1].position.y;
        cumDist[i] = cumDist[i - 1] + std::hypot(dx, dy);
    }

    // Helper: interpolate position and heading at a given distance along the polyline
    auto interpAt = [&](double dist) -> std::pair<Point2D, double> {
        if (dist <= 0.0) return {mark.samples[0].position, mark.samples[0].heading};
        if (dist >= cumDist.back()) {
            return {mark.samples.back().position, mark.samples.back().heading};
        }
        // Binary search for the segment
        int lo = 0, hi = mark.numSamples() - 1;
        while (lo < hi - 1) {
            int mid = (lo + hi) / 2;
            if (cumDist[mid] <= dist) lo = mid;
            else hi = mid;
        }
        double segLen = cumDist[hi] - cumDist[lo];
        double t = segLen > 0.0 ? (dist - cumDist[lo]) / segLen : 0.0;
        Point2D pos(
            mark.samples[lo].position.x * (1 - t) + mark.samples[hi].position.x * t,
            mark.samples[lo].position.y * (1 - t) + mark.samples[hi].position.y * t
        );
        double heading = mark.samples[lo].heading * (1 - t) + mark.samples[hi].heading * t;
        return {pos, heading};
    };

    // For each dash, generate a quad (4 vertices, 2 triangles)
    for (const auto& seg : dashSegments) {
        double dashStart = seg.first;
        double dashEnd = seg.second;

        if (dashEnd - dashStart < params.dashEpsilon) continue;

        // Interpolate position and heading at dash start and end
        auto [startPos, startHeading] = interpAt(dashStart);
        auto [endPos, endHeading] = interpAt(dashEnd);

        // Compute normals at start and end
        double snx = -std::sin(startHeading);
        double sny = std::cos(startHeading);
        double enx = -std::sin(endHeading);
        double eny = std::cos(endHeading);

        uint32_t baseVertex = static_cast<uint32_t>(section.vertices.size());

        // 4 vertices: start-left, start-right, end-left, end-right
        MeshVertex vSL;
        vSL.position = Point3D(startPos.x + snx * halfWidth,
                               startPos.y + sny * halfWidth, z);
        vSL.normal = upNormal;
        vSL.uv = Vec2(dashStart * params.uvScale, 0.0);
        section.vertices.push_back(vSL);

        MeshVertex vSR;
        vSR.position = Point3D(startPos.x - snx * halfWidth,
                               startPos.y - sny * halfWidth, z);
        vSR.normal = upNormal;
        vSR.uv = Vec2(dashStart * params.uvScale, 1.0);
        section.vertices.push_back(vSR);

        MeshVertex vEL;
        vEL.position = Point3D(endPos.x + enx * halfWidth,
                               endPos.y + eny * halfWidth, z);
        vEL.normal = upNormal;
        vEL.uv = Vec2(dashEnd * params.uvScale, 0.0);
        section.vertices.push_back(vEL);

        MeshVertex vER;
        vER.position = Point3D(endPos.x - enx * halfWidth,
                               endPos.y - eny * halfWidth, z);
        vER.normal = upNormal;
        vER.uv = Vec2(dashEnd * params.uvScale, 1.0);
        section.vertices.push_back(vER);

        // 2 triangles (CCW from above)
        // (start-left, end-left, start-right) and (start-right, end-left, end-right)
        section.indices.push_back(baseVertex + 0);  // SL
        section.indices.push_back(baseVertex + 2);  // EL
        section.indices.push_back(baseVertex + 1);  // SR

        section.indices.push_back(baseVertex + 1);  // SR
        section.indices.push_back(baseVertex + 2);  // EL
        section.indices.push_back(baseVertex + 3);  // ER
    }

    return section;
}

} // namespace detail

// ═══════════════════════════════════════════════════════════
// generateRoadMesh — Build RoadMesh from LaneNetwork
// ═══════════════════════════════════════════════════════════
//
// Generates pavement mesh by tessellating each lane into a
// triangle strip. Lanes are processed independently.
//
// @param network  The lane network to generate mesh from
// @param params   Mesh generation parameters
// @return RoadMesh with asphalt sections (one per lane, merged)
//
inline RoadMesh generateRoadMesh(
    const LaneNetwork& network,
    const MeshGenParams& params = {}
) {
    RoadMesh mesh;

    // Generate pavement: one triangle strip per lane
    // Only create the asphalt section if there are drivable lanes
    bool hasAsphalt = false;
    MeshSection asphaltSection;
    asphaltSection.material = MaterialType::Asphalt;
    asphaltSection.materialName = "asphalt";

    for (const auto& centerline : network.centerlines) {
        // Skip center lane (zero width, no pavement)
        if (centerline.laneId == 0) continue;
        // Skip non-drivable lanes for pavement (shoulders, etc. get different material)
        if (!centerline.isDrivable()) continue;

        MeshSection laneStrip = detail::generateLaneStrip(centerline, network, params);
        if (!laneStrip.vertices.empty()) {
            asphaltSection.merge(laneStrip);
            hasAsphalt = true;
        }
    }

    if (hasAsphalt) {
        mesh.sections.push_back(std::move(asphaltSection));
    }

    return mesh;
}

// ═══════════════════════════════════════════════════════════
// generateMarkingMesh — Build marking mesh from RoadMarkNetwork
// ═══════════════════════════════════════════════════════════
//
// Generates marking meshes (solid and dashed) as separate
// mesh sections split by color (white/yellow).
//
// @param marks  The road mark network
// @param params Mesh generation parameters
// @return RoadMesh with white and yellow marking sections
//
inline RoadMesh generateMarkingMesh(
    const RoadMarkNetwork& marks,
    const MeshGenParams& params = {}
) {
    RoadMesh mesh;

    for (const auto& mark : marks.markings) {
        MeshSection markSection;

        if (mark.style.isDashed()) {
            markSection = detail::generateDashedMarkingMesh(mark, params);
        } else if (mark.style.isSolid()) {
            markSection = detail::generateSolidMarkingMesh(mark, params);
        } else {
            continue;  // skip unknown types
        }

        if (markSection.vertices.empty()) continue;

        // Merge into the appropriate color section
        MaterialType matType = markSection.material;
        MeshSection& targetSection = mesh.getOrCreateSection(matType);
        targetSection.merge(markSection);
    }

    return mesh;
}

// ═══════════════════════════════════════════════════════════
// generateFullRoadMesh — Build complete road mesh (pavement + markings)
// ═══════════════════════════════════════════════════════════
//
// Convenience function: generates both pavement and marking meshes
// and combines them into a single RoadMesh.
//
// @param network  The lane network
// @param marks    The road mark network
// @param params   Mesh generation parameters
// @return RoadMesh with all sections (asphalt, white markings, yellow markings)
//
inline RoadMesh generateFullRoadMesh(
    const LaneNetwork& network,
    const RoadMarkNetwork& marks,
    const MeshGenParams& params = {}
) {
    RoadMesh mesh = generateRoadMesh(network, params);

    if (params.generateMarkings) {
        RoadMesh markMesh = generateMarkingMesh(marks, params);
        for (auto& sec : markMesh.sections) {
            // Merge into existing section or add new
            MeshSection& target = mesh.getOrCreateSection(sec.material);
            target.merge(sec);
        }
    }

    return mesh;
}

// ═══════════════════════════════════════════════════════════
// Direct Mesh from Centerline (Lanemaker-style)
// ═══════════════════════════════════════════════════════════
//
// Bypasses the V2 adapter and lane network pipeline entirely.
// Takes a list of centerline points and road width, offsets them
// left/right by halfWidth to get lane boundaries, then builds
// a strip mesh directly — exactly like lanemaker's get_lane_mesh().
//
// This is used as a fallback when the V2 adapter fails to produce
// a valid SegmentSequence (e.g., for lanemaker-tool roads with
// segment-arc-segment composition).
//
// Also generates edge markings (solid white lines) at both edges.
//
inline RoadMesh generateMeshFromCenterline(
    const std::vector<Point2D>& centerline,
    double roadWidth,
    int laneCount = 2,
    const MeshGenParams& params = {}
) {
    RoadMesh mesh;
    if (centerline.size() < 2) return mesh;

    double halfWidth = roadWidth / 2.0;
    double z = params.zHeight;
    Vec3 upNormal(0, 0, 1);

    // ── Step 1: Compute normals at each point ──
    // Normal = perpendicular to tangent (left = 90° CCW)
    int N = static_cast<int>(centerline.size());
    std::vector<Vec2> normals(N);
    std::vector<double> cumDist(N, 0.0);

    for (int i = 0; i < N; i++) {
        Vec2 tangent;
        if (i == 0) {
            tangent = { centerline[1].x - centerline[0].x,
                        centerline[1].y - centerline[0].y };
        } else if (i == N - 1) {
            tangent = { centerline[N-1].x - centerline[N-2].x,
                        centerline[N-1].y - centerline[N-2].y };
        } else {
            tangent = { centerline[i+1].x - centerline[i-1].x,
                        centerline[i+1].y - centerline[i-1].y };
        }
        double len = tangent.norm();
        if (len > 1e-9) {
            tangent = { tangent.x / len, tangent.y / len };
        } else {
            tangent = { 1.0, 0.0 };
        }
        // Normal = 90° CCW = (-ty, tx) = left direction
        normals[i] = { -tangent.y, tangent.x };

        if (i > 0) {
            cumDist[i] = cumDist[i-1] + centerline[i].distanceTo(centerline[i-1]);
        }
    }

    // ── Step 2: Generate asphalt strip mesh ──
    // For each lane, create a strip between inner and outer border
    double laneWidth = roadWidth / std::max(laneCount, 1);
    MeshSection& asphalt = mesh.getOrCreateSection(MaterialType::Asphalt);

    for (int lane = 0; lane < laneCount; lane++) {
        // Lane edges: from -halfWidth + lane*laneWidth to -halfWidth + (lane+1)*laneWidth
        double innerOffset = -halfWidth + lane * laneWidth;
        double outerOffset = -halfWidth + (lane + 1) * laneWidth;

        uint32_t baseIdx = static_cast<uint32_t>(asphalt.vertices.size());

        for (int i = 0; i < N; i++) {
            // Inner vertex (closer to center)
            Point2D innerPt = { centerline[i].x + normals[i].x * innerOffset,
                                centerline[i].y + normals[i].y * innerOffset };
            MeshVertex vInner;
            vInner.position = Point3D(innerPt.x, innerPt.y, z);
            vInner.normal = upNormal;
            vInner.uv = Vec2(cumDist[i], innerOffset + halfWidth);
            asphalt.vertices.push_back(vInner);

            // Outer vertex (farther from center)
            Point2D outerPt = { centerline[i].x + normals[i].x * outerOffset,
                                centerline[i].y + normals[i].y * outerOffset };
            MeshVertex vOuter;
            vOuter.position = Point3D(outerPt.x, outerPt.y, z);
            vOuter.normal = upNormal;
            vOuter.uv = Vec2(cumDist[i], outerOffset + halfWidth);
            asphalt.vertices.push_back(vOuter);
        }

        // Triangle strip indices (CCW from above)
        for (int i = 0; i < N - 1; i++) {
            uint32_t i0 = baseIdx + i * 2;
            uint32_t i1 = baseIdx + i * 2 + 1;
            uint32_t i2 = baseIdx + (i + 1) * 2;
            uint32_t i3 = baseIdx + (i + 1) * 2 + 1;

            asphalt.indices.push_back(i0);
            asphalt.indices.push_back(i2);
            asphalt.indices.push_back(i1);

            asphalt.indices.push_back(i1);
            asphalt.indices.push_back(i2);
            asphalt.indices.push_back(i3);
        }
    }

    // ── Step 3: Generate edge markings (solid white lines) ──
    if (params.generateMarkings) {
        double markWidth = 0.15;  // 15cm road edge marking
        double markHalf = markWidth / 2.0;
        double markZ = z + params.markingElevation;
        MeshSection& marking = mesh.getOrCreateSection(MaterialType::WhiteMarking);

        // Left edge and right edge
        double edgeOffsets[] = { halfWidth - markHalf, -(halfWidth - markHalf) };
        for (double edgeOffset : edgeOffsets) {
            uint32_t baseIdx = static_cast<uint32_t>(marking.vertices.size());

            for (int i = 0; i < N; i++) {
                Point2D leftPt = { centerline[i].x + normals[i].x * (edgeOffset + markHalf),
                                   centerline[i].y + normals[i].y * (edgeOffset + markHalf) };
                Point2D rightPt = { centerline[i].x + normals[i].x * (edgeOffset - markHalf),
                                    centerline[i].y + normals[i].y * (edgeOffset - markHalf) };

                MeshVertex vL;
                vL.position = Point3D(leftPt.x, leftPt.y, markZ);
                vL.normal = upNormal;
                vL.uv = Vec2(cumDist[i], 0.0);
                marking.vertices.push_back(vL);

                MeshVertex vR;
                vR.position = Point3D(rightPt.x, rightPt.y, markZ);
                vR.normal = upNormal;
                vR.uv = Vec2(cumDist[i], 1.0);
                marking.vertices.push_back(vR);
            }

            for (int i = 0; i < N - 1; i++) {
                uint32_t l0 = baseIdx + i * 2;
                uint32_t r0 = baseIdx + i * 2 + 1;
                uint32_t l1 = baseIdx + (i + 1) * 2;
                uint32_t r1 = baseIdx + (i + 1) * 2 + 1;

                marking.indices.push_back(l0);
                marking.indices.push_back(l1);
                marking.indices.push_back(r0);

                marking.indices.push_back(r0);
                marking.indices.push_back(l1);
                marking.indices.push_back(r1);
            }
        }

        // Center dashed line (if 2+ lanes)
        if (laneCount >= 2) {
            double dashLen = 3.0, gapLen = 6.0;
            double centerOffset = 0.0;  // center of road
            MeshSection& centerMark = mesh.getOrCreateSection(MaterialType::YellowMarking);

            double totalLen = cumDist.back();
            double pos = 0.0;
            bool isDash = true;

            // Helper: interpolate position and normal at arc-length d
            auto interpPt = [&](double d) -> std::pair<Point2D, Vec2> {
                int lo = 0;
                for (int i = 1; i < N; i++) {
                    if (cumDist[i] >= d) { lo = i - 1; break; }
                    lo = i;
                }
                int hi = std::min(lo + 1, N - 1);
                double segL = cumDist[hi] - cumDist[lo];
                double t = segL > 0 ? (d - cumDist[lo]) / segL : 0;
                Point2D p = { centerline[lo].x * (1-t) + centerline[hi].x * t,
                              centerline[lo].y * (1-t) + centerline[hi].y * t };
                Vec2 n = { normals[lo].x * (1-t) + normals[hi].x * t,
                           normals[lo].y * (1-t) + normals[hi].y * t };
                return { p, n };
            };

            // Helper: generate a dashed line at a given lateral offset
            auto generateDashed = [&](MeshSection& section, double offset, double width) {
                double halfW = width / 2.0;
                double p = 0.0;
                bool dash = true;
                while (p < totalLen) {
                    double end = p + (dash ? dashLen : gapLen);
                    if (end > totalLen) end = totalLen;
                    if (dash && end - p > 0.1) {
                        uint32_t baseIdx = static_cast<uint32_t>(section.vertices.size());
                        auto [p0, n0] = interpPt(p);
                        auto [p1, n1] = interpPt(end);
                        Point2D corners[4] = {
                            { p0.x + n0.x * (offset + halfW), p0.y + n0.y * (offset + halfW) },
                            { p0.x + n0.x * (offset - halfW), p0.y + n0.y * (offset - halfW) },
                            { p1.x + n1.x * (offset + halfW), p1.y + n1.y * (offset + halfW) },
                            { p1.x + n1.x * (offset - halfW), p1.y + n1.y * (offset - halfW) },
                        };
                        for (int c = 0; c < 4; c++) {
                            MeshVertex v;
                            v.position = Point3D(corners[c].x, corners[c].y, markZ);
                            v.normal = upNormal;
                            v.uv = Vec2(c < 2 ? 0.0 : 1.0, c % 2 == 0 ? 0.0 : 1.0);
                            section.vertices.push_back(v);
                        }
                        section.indices.push_back(baseIdx + 0);
                        section.indices.push_back(baseIdx + 2);
                        section.indices.push_back(baseIdx + 1);
                        section.indices.push_back(baseIdx + 1);
                        section.indices.push_back(baseIdx + 2);
                        section.indices.push_back(baseIdx + 3);
                    }
                    p = end;
                    dash = !dash;
                }
            };

            // Center yellow dashed line
            generateDashed(centerMark, centerOffset, 0.15);

            // Lane divider dashed white lines (for roads with more than 2 lanes)
            if (laneCount > 2) {
                MeshSection& dividerMark = mesh.getOrCreateSection(MaterialType::WhiteMarking);
                int rightLanes = laneCount / 2;
                int leftLanes = laneCount - rightLanes;
                // Dividers on right side (positive offsets from center)
                for (int lane = 1; lane < rightLanes; lane++) {
                    double offset = lane * laneWidth;
                    generateDashed(dividerMark, offset, 0.12);
                }
                // Dividers on left side (negative offsets from center)
                for (int lane = 1; lane < leftLanes; lane++) {
                    double offset = -lane * laneWidth;
                    generateDashed(dividerMark, offset, 0.12);
                }
            }
        }
    }

    return mesh;
}

} // namespace geo
