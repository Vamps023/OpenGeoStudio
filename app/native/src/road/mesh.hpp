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

// ─── Triangulate a simple polygon (ear clipping) ───────────
inline std::vector<uint32_t> triangulatePolygon(const std::vector<Point2D>& polygon) {
    std::vector<uint32_t> indices;

    if (polygon.size() < 3) return indices;

    // Index list of remaining vertices
    std::vector<uint32_t> remaining;
    remaining.reserve(polygon.size());
    for (uint32_t i = 0; i < polygon.size(); i++) {
        remaining.push_back(i);
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
                // Add triangle
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
// Creates a triangle strip mesh for a road surface.
// Returns vertices (x,y,z), normals (nx,ny,nz), UVs (u,v), and indices.
inline MeshData generateRoadMesh(const Road& road, int numSamples = 32) {
    MeshData mesh;

    // Sample centerline with elevation
    auto cl3D = road.sampleCenterline3D(numSamples);
    if (cl3D.size() < 2) return mesh;

    // Sample centerline in 2D for tangent computation
    auto cl2D = road.sampleCenterline(numSamples);
    double halfWidth = road.width / 2.0;

    // Generate vertices: for each centerline point, create left and right edge vertices
    for (size_t i = 0; i < cl3D.size(); i++) {
        Vec2 tangent = tangentAt(cl2D, i);
        Vec2 normal = tangent.perp();

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

        // Normals (pointing up)
        for (int j = 0; j < 2; j++) {
            mesh.normals.push_back(0);
            mesh.normals.push_back(0);
            mesh.normals.push_back(1);
        }

        // UVs (u along width, v along length)
        double v = static_cast<double>(i) / (cl3D.size() - 1);
        mesh.uvs.push_back(0); mesh.uvs.push_back(v);
        mesh.uvs.push_back(1); mesh.uvs.push_back(v);
    }

    // Generate indices (triangle strip → triangles)
    for (size_t i = 0; i < cl3D.size() - 1; i++) {
        uint32_t i0 = static_cast<uint32_t>(i * 2);
        uint32_t i1 = static_cast<uint32_t>(i * 2 + 1);
        uint32_t i2 = static_cast<uint32_t>((i + 1) * 2);
        uint32_t i3 = static_cast<uint32_t>((i + 1) * 2 + 1);

        // Two triangles per quad
        mesh.indices.push_back(i0);
        mesh.indices.push_back(i2);
        mesh.indices.push_back(i1);

        mesh.indices.push_back(i1);
        mesh.indices.push_back(i2);
        mesh.indices.push_back(i3);
    }

    return mesh;
}

// ─── Generate intersection mesh (triangulated polygon) ─────
inline MeshData generateIntersectionMesh(const GeneratedIntersection& ix, double z = 0) {
    MeshData mesh;

    if (ix.polygon.size() < 3) return mesh;

    // Vertices: one per polygon vertex
    for (const auto& p : ix.polygon) {
        mesh.vertices.push_back(p.x);
        mesh.vertices.push_back(p.y);
        mesh.vertices.push_back(z);
        mesh.normals.push_back(0);
        mesh.normals.push_back(0);
        mesh.normals.push_back(1);
        mesh.uvs.push_back(p.x / 10);  // simple UV
        mesh.uvs.push_back(p.y / 10);
    }

    // Triangulate
    mesh.indices = triangulatePolygon(ix.polygon);

    return mesh;
}

} // namespace geo
