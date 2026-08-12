#pragma once

// ═══════════════════════════════════════════════════════════
// Mesh Generator — Tessellation for 3D rendering
// ═══════════════════════════════════════════════════════════
//
// Generates vertex/index buffers for road surfaces and intersections.
// The output is a flat array of floats (vertices, normals, UVs) and
// uint32 indices that can be directly uploaded to Babylon.js/WebGL.
//
// Design principles:
// - Never creates Babylon meshes directly
// - Returns raw mesh data (vertices, indices, normals, UVs)
// - Triangulates polygons using ear-clipping
// - Generates road mesh as a strip along the centerline

#include "geometry.hpp"
#include "road.hpp"
#include <cstdint>

namespace geo {

// ─── Compute signed area of a polygon (shoelace formula) ───
// Positive = CCW, negative = CW
inline double polygonSignedArea(const std::vector<Point2D>& polygon) {
    double area = 0;
    for (size_t i = 0; i < polygon.size(); i++) {
        size_t j = (i + 1) % polygon.size();
        area += polygon[i].x * polygon[j].y - polygon[j].x * polygon[i].y;
    }
    return area / 2.0;
}

// ─── Triangulate a simple polygon (ear clipping) ───────────
// Normalizes winding order to CCW before triangulating.
// Handles both CCW and CW input polygons.
inline std::vector<uint32_t> triangulatePolygon(const std::vector<Point2D>& polygon) {
    std::vector<uint32_t> indices;

    if (polygon.size() < 3) return indices;

    // Normalize winding order to CCW
    // If signed area is negative, the polygon is CW — reverse it
    double signedArea = polygonSignedArea(polygon);
    bool isCCW = signedArea > 0;

    // Index list of remaining vertices
    std::vector<uint32_t> remaining;
    remaining.reserve(polygon.size());
    if (isCCW) {
        for (uint32_t i = 0; i < polygon.size(); i++) {
            remaining.push_back(i);
        }
    } else {
        // Reverse order for CW polygons
        for (uint32_t i = static_cast<uint32_t>(polygon.size()); i > 0; i--) {
            remaining.push_back(i - 1);
        }
    }

    // Ear clipping algorithm
    int iterations = 0;
    int maxIterations = static_cast<int>(polygon.size()) * 3;

    while (remaining.size() > 2 && iterations < maxIterations) {
        iterations++;
        bool earFound = false;

        for (size_t i = 0; i < remaining.size(); i++) {
            size_t prev = (i == 0) ? remaining.size() - 1 : i - 1;
            size_t next = (i + 1) % remaining.size();

            const Point2D& a = polygon[remaining[prev]];
            const Point2D& b = polygon[remaining[i]];
            const Point2D& c = polygon[remaining[next]];

            // Check if this is a convex vertex (ear candidate)
            // For CCW polygon, convex vertices have positive cross product
            Vec2 ab = b - a;
            Vec2 bc = c - b;
            double cross = ab.cross(bc);

            if (cross <= 0) continue;  // reflex vertex, not an ear

            // Check if any other vertex is inside the triangle
            bool isEar = true;
            for (size_t j = 0; j < remaining.size(); j++) {
                if (j == prev || j == i || j == next) continue;
                const Point2D& p = polygon[remaining[j]];

                // Point-in-triangle test using barycentric coordinates
                Vec2 ap = p - a;
                Vec2 bp = p - b;
                double d1 = ab.cross(ap);
                double d2 = bc.cross(bp);
                Vec2 ca = a - c;
                Vec2 cp = p - c;
                double d3 = ca.cross(cp);

                if (d1 >= 0 && d2 >= 0 && d3 >= 0) {
                    isEar = false;
                    break;
                }
            }

            if (isEar) {
                // Add triangle (using original vertex indices)
                indices.push_back(remaining[prev]);
                indices.push_back(remaining[i]);
                indices.push_back(remaining[next]);

                // Remove the ear vertex
                remaining.erase(remaining.begin() + i);
                earFound = true;
                break;
            }
        }

        if (!earFound) {
            // Fallback: just add the remaining triangle fan
            for (size_t i = 1; i < remaining.size() - 1; i++) {
                indices.push_back(remaining[0]);
                indices.push_back(remaining[i]);
                indices.push_back(remaining[i + 1]);
            }
            break;
        }
    }

    return indices;
}

// ─── Generate road mesh (strip along centerline) ───────────
// Creates a triangle strip mesh for a road surface with:
// - Miter-joint edge offsets (correct for curves)
// - Arc-length UV mapping (texture tiles correctly along length)
// - Proper normals (up, with optional banking in future)
// - Lane boundary vertices (for lane marking generation)
inline MeshData generateRoadMesh(const Road& road, int numSamples = 32) {
    MeshData mesh;

    // Sample centerline with elevation
    auto cl3D = road.sampleCenterline3D(numSamples);
    if (cl3D.size() < 2) return mesh;

    // Sample centerline in 2D for tangent computation
    auto cl2D = road.sampleCenterline(numSamples);
    double halfWidth = road.width / 2.0;

    // Compute cumulative arc length for UV mapping
    std::vector<double> arcLength(cl3D.size(), 0);
    for (size_t i = 1; i < cl3D.size(); i++) {
        double dx = cl3D[i].x - cl3D[i - 1].x;
        double dy = cl3D[i].y - cl3D[i - 1].y;
        double dz = cl3D[i].z - cl3D[i - 1].z;
        arcLength[i] = arcLength[i - 1] + std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    double totalLength = arcLength.back();
    if (totalLength < EPSILON) totalLength = 1.0;  // avoid division by zero

    // UV tiling: 1 texture repeat per 10 meters of road length
    const double UV_TILE_LENGTH = 10.0;

    // Generate vertices: for each centerline point, create left and right edge vertices
    // using miter-joint offsets for correct curve handling
    for (size_t i = 0; i < cl3D.size(); i++) {
        // Compute miter-joint normal (same algorithm as offsetPolyline)
        Vec2 tanIn, tanOut;
        if (i == 0) {
            tanOut = (cl2D[1] - cl2D[0]).normalized();
            tanIn = tanOut;
        } else if (i == cl2D.size() - 1) {
            tanIn = (cl2D[i] - cl2D[i - 1]).normalized();
            tanOut = tanIn;
        } else {
            tanIn = (cl2D[i] - cl2D[i - 1]).normalized();
            tanOut = (cl2D[i + 1] - cl2D[i]).normalized();
        }

        Vec2 normIn = tanIn.perp();
        Vec2 normOut = tanOut.perp();
        Vec2 miter = normIn + normOut;
        double miterLen = miter.norm();

        Vec2 normal;
        if (miterLen < EPSILON) {
            normal = normIn;
        } else {
            miter = miter / miterLen;
            double dot = normIn.dot(miter);
            if (std::abs(dot) < EPSILON) {
                normal = normIn;
            } else {
                double miterScale = std::min(1.0 / dot, 4.0);
                normal = miter * miterScale;
            }
        }

        // Left edge vertex
        Point3D left = {
            cl3D[i].x + normal.x * halfWidth,
            cl3D[i].y + normal.y * halfWidth,
            cl3D[i].z
        };
        // Right edge vertex
        Point3D right = {
            cl3D[i].x - normal.x * halfWidth,
            cl3D[i].y - normal.y * halfWidth,
            cl3D[i].z
        };

        // Add vertices (x, y, z interleaved)
        mesh.vertices.push_back(left.x);
        mesh.vertices.push_back(left.y);
        mesh.vertices.push_back(left.z);
        mesh.vertices.push_back(right.x);
        mesh.vertices.push_back(right.y);
        mesh.vertices.push_back(right.z);

        // Normals (pointing up — future: add banking/superelevation)
        for (int j = 0; j < 2; j++) {
            mesh.normals.push_back(0);
            mesh.normals.push_back(0);
            mesh.normals.push_back(1);
        }

        // UVs: u = 0 (left) / 1 (right), v = arcLength / tileLength
        double v = arcLength[i] / UV_TILE_LENGTH;
        mesh.uvs.push_back(0); mesh.uvs.push_back(v);
        mesh.uvs.push_back(1); mesh.uvs.push_back(v);
    }

    // Generate indices (triangle strip → triangles)
    for (size_t i = 0; i < cl3D.size() - 1; i++) {
        uint32_t i0 = static_cast<uint32_t>(i * 2);
        uint32_t i1 = static_cast<uint32_t>(i * 2 + 1);
        uint32_t i2 = static_cast<uint32_t>((i + 1) * 2);
        uint32_t i3 = static_cast<uint32_t>((i + 1) * 2 + 1);

        // Two triangles per quad (CCW winding)
        mesh.indices.push_back(i0);
        mesh.indices.push_back(i2);
        mesh.indices.push_back(i1);

        mesh.indices.push_back(i1);
        mesh.indices.push_back(i2);
        mesh.indices.push_back(i3);
    }

    mesh.vertexCount = static_cast<uint32_t>(cl3D.size() * 2);
    mesh.indexCount = static_cast<uint32_t>(mesh.indices.size());
    mesh.triangleCount = mesh.indexCount / 3;

    return mesh;
}

// ─── Generate lane boundary lines for a road ───────────────
// Returns a list of polylines, one per lane boundary
inline std::vector<std::vector<Point2D>> generateLaneBoundaries(const Road& road, int numSamples = 32) {
    std::vector<std::vector<Point2D>> boundaries;
    if (road.laneCount < 2) return boundaries;

    auto cl2D = road.sampleCenterline(numSamples);
    if (cl2D.size() < 2) return boundaries;

    double laneWidth = road.width / road.laneCount;

    // Generate lane boundary at each lane division
    // For a 2-lane road: 1 boundary at center
    // For a 4-lane road: 3 boundaries at -1.5L, -0.5L, 0.5L, 1.5L
    int numBoundaries = road.laneCount - 1;
    for (int b = 0; b < numBoundaries; b++) {
        // Offset from center: ranges from -(laneCount/2 - 0.5)*laneWidth to +(laneCount/2 - 0.5)*laneWidth
        double offset = (b - (numBoundaries - 1) / 2.0) * laneWidth;
        auto boundary = offsetPolyline(cl2D, offset);
        boundaries.push_back(boundary);
    }

    return boundaries;
}

// ─── Generate intersection mesh (triangulated polygon) ─────
// Uses local UV mapping based on polygon bounding box
inline MeshData generateIntersectionMesh(const GeneratedIntersection& ix, double z = 0) {
    MeshData mesh;

    if (ix.polygon.size() < 3) return mesh;

    // Compute bounding box for local UV mapping
    double minX = ix.polygon[0].x, maxX = ix.polygon[0].x;
    double minY = ix.polygon[0].y, maxY = ix.polygon[0].y;
    for (const auto& p : ix.polygon) {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }
    double rangeX = maxX - minX;
    double rangeY = maxY - minY;
    if (rangeX < EPSILON) rangeX = 1.0;
    if (rangeY < EPSILON) rangeY = 1.0;

    // UV tiling scale (1 tile per 10 meters)
    const double UV_TILE = 10.0;

    // Vertices: one per polygon vertex
    for (const auto& p : ix.polygon) {
        mesh.vertices.push_back(p.x);
        mesh.vertices.push_back(p.y);
        mesh.vertices.push_back(z);
        mesh.normals.push_back(0);
        mesh.normals.push_back(0);
        mesh.normals.push_back(1);
        // Local UV: normalize to [0,1] then divide by tile size
        mesh.uvs.push_back((p.x - minX) / UV_TILE);
        mesh.uvs.push_back((p.y - minY) / UV_TILE);
    }

    // Triangulate (handles both CCW and CW polygons)
    mesh.indices = triangulatePolygon(ix.polygon);

    mesh.vertexCount = static_cast<uint32_t>(ix.polygon.size());
    mesh.indexCount = static_cast<uint32_t>(mesh.indices.size());
    mesh.triangleCount = mesh.indexCount / 3;

    return mesh;
}

} // namespace geo