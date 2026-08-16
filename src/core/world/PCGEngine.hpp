#pragma once

// ============================================================
// PCGEngine — Procedural Content Generation engine
// ============================================================
//
// Evaluates PCG graphs to generate points for scattering
// vegetation, buildings, rocks, etc.
//

#include "WorldTypes.hpp"
#include "World.hpp"
#include <random>
#include <cmath>
#include <functional>

namespace world {

// ============================================================
// PCGContext — Context passed through graph evaluation
// ============================================================

struct PCGContext {
    const World* world = nullptr;
    std::mt19937 rng;
    int seed = 42;

    // Terrain sampling function (set by renderer)
    std::function<float(float, float)> sampleHeight;
    std::function<float(float, float)> sampleSlope;
    std::function<float(float, float)> sampleMask;

    PCGContext() : rng(42) {}
    explicit PCGContext(int s) : rng(s), seed(s) {}

    void setSeed(int s) { seed = s; rng.seed(s); }
    float random01() { return std::uniform_real_distribution<float>(0, 1)(rng); }
    float randomRange(float min, float max) {
        return std::uniform_real_distribution<float>(min, max)(rng);
    }
};

// ============================================================
// PCGEngine — Evaluates PCG graphs
// ============================================================

class PCGEngine {
public:
    // Evaluate a PCG graph and return generated points
    static QList<PCGPoint> evaluate(const PCGGraph& graph, PCGContext& ctx) {
        if (graph.nodes.isEmpty()) return {};

        // Topological sort
        QList<QString> order;
        if (!topologicalSort(graph, order)) return {};  // has cycles

        // Evaluate each node in order, storing results
        QMap<QString, QList<PCGPoint>> results;

        for (const auto& nodeId : order) {
            const PCGNode* node = nullptr;
            for (const auto& n : graph.nodes)
                if (n.id == nodeId) { node = &n; break; }
            if (!node || !node->enabled) continue;

            // Gather input points
            QList<PCGPoint> inputPoints;
            for (const auto& inId : node->inputNodeIds) {
                if (results.contains(inId))
                    inputPoints.append(results[inId]);
            }

            // Evaluate node
            QList<PCGPoint> output = evaluateNode(*node, inputPoints, ctx, graph);
            results[nodeId] = output;
        }

        // Return output from the last node (or find an output node)
        for (int i = graph.nodes.size() - 1; i >= 0; i--) {
            if (graph.nodes[i].enabled && isOutputNode(graph.nodes[i].type)) {
                if (results.contains(graph.nodes[i].id))
                    return results[graph.nodes[i].id];
            }
        }

        // Fallback: return last result
        if (!results.isEmpty())
            return results.last();
        return {};
    }

    // Check if a node type is an output node
    static bool isOutputNode(PCGNodeType type) {
        return type == PCGNodeType::StaticMeshOutput ||
               type == PCGNodeType::ActorOutput ||
               type == PCGNodeType::InstanceOutput ||
               type == PCGNodeType::SplineOutput ||
               type == PCGNodeType::CollectionOutput ||
               type == PCGNodeType::TerrainMaskOutput;
    }

private:
    // Topological sort using Kahn's algorithm
    static bool topologicalSort(const PCGGraph& graph, QList<QString>& order) {
        QMap<QString, int> inDegree;
        QMap<QString, QList<QString>> dependents;

        for (const auto& n : graph.nodes) {
            inDegree[n.id] = 0;
        }
        for (const auto& n : graph.nodes) {
            for (const auto& in : n.inputNodeIds) {
                if (inDegree.contains(in)) {
                    inDegree[n.id]++;
                    dependents[in].append(n.id);
                }
            }
        }

        QList<QString> queue;
        for (auto it = inDegree.begin(); it != inDegree.end(); ++it)
            if (it.value() == 0) queue.append(it.key());

        while (!queue.isEmpty()) {
            QString current = queue.takeFirst();
            order.append(current);
            if (dependents.contains(current)) {
                for (const auto& dep : dependents[current]) {
                    inDegree[dep]--;
                    if (inDegree[dep] == 0) queue.append(dep);
                }
            }
        }

        return order.size() == graph.nodes.size();
    }

    // Evaluate a single node
    static QList<PCGPoint> evaluateNode(const PCGNode& node,
                                         const QList<PCGPoint>& inputs,
                                         PCGContext& ctx,
                                         const PCGGraph& graph) {
        switch (node.type) {
        // Input nodes — generate initial points
        case PCGNodeType::WorldInput:
            return generateWorldPoints(node, ctx);
        case PCGNodeType::TerrainInput:
            return generateTerrainPoints(node, ctx);
        case PCGNodeType::SplineInput:
            return generateSplinePoints(node, ctx, graph);
        case PCGNodeType::PointInput:
            return generateCustomPoints(node, ctx);

        // Sampling nodes — pass through with attributes
        case PCGNodeType::TerrainHeight:
            return sampleTerrainHeight(inputs, ctx);
        case PCGNodeType::SlopeSample:
            return sampleSlope(inputs, ctx);
        case PCGNodeType::MaskSample:
            return sampleMask(inputs, ctx);
        case PCGNodeType::DistanceSample:
            return sampleDistance(inputs, ctx, node);

        // Generation nodes
        case PCGNodeType::ScatterPoints:
            return scatterPoints(node, ctx);
        case PCGNodeType::GridPoints:
            return gridPoints(node, ctx);
        case PCGNodeType::SurfaceSampling:
            return surfaceSampling(node, ctx);

        // Filtering nodes
        case PCGNodeType::DensityFilter:
            return densityFilter(inputs, node);
        case PCGNodeType::HeightFilter:
            return heightFilter(inputs, node);
        case PCGNodeType::SlopeFilter:
            return slopeFilter(inputs, node);
        case PCGNodeType::MaskFilter:
            return maskFilter(inputs, node, ctx);
        case PCGNodeType::RandomFilter:
            return randomFilter(inputs, node, ctx);

        // Transform nodes
        case PCGNodeType::RandomRotation:
            return randomRotation(inputs, node, ctx);
        case PCGNodeType::RandomScale:
            return randomScale(inputs, node, ctx);
        case PCGNodeType::AlignToSurface:
            return alignToSurface(inputs, ctx);
        case PCGNodeType::OffsetTransform:
            return offsetTransform(inputs, node);

        // Output nodes — pass through
        case PCGNodeType::StaticMeshOutput:
        case PCGNodeType::ActorOutput:
        case PCGNodeType::InstanceOutput:
        case PCGNodeType::SplineOutput:
        case PCGNodeType::CollectionOutput:
        case PCGNodeType::TerrainMaskOutput:
            return inputs;  // pass through

        default:
            return inputs;
        }
    }

    // ============================================================
    // Input node implementations
    // ============================================================

    static QList<PCGPoint> generateWorldPoints(const PCGNode& node, PCGContext& ctx) {
        QList<PCGPoint> points;
        if (!ctx.world) return points;

        float areaSize = node.properties.value("areaSize", "4000").toFloat();
        float density = node.properties.value("density", "0.01").toFloat();
        int count = int(areaSize * areaSize * density / 1000);

        for (int i = 0; i < count; i++) {
            PCGPoint p;
            p.x = ctx.randomRange(-areaSize / 2, areaSize / 2);
            p.z = ctx.randomRange(-areaSize / 2, areaSize / 2);
            if (ctx.sampleHeight)
                p.y = ctx.sampleHeight(p.x, p.z);
            p.density = 1.0f;
            points.append(p);
        }
        return points;
    }

    static QList<PCGPoint> generateTerrainPoints(const PCGNode& node, PCGContext& ctx) {
        QList<PCGPoint> points;
        if (!ctx.world) return points;

        float size = ctx.world->settings.terrainSize;
        float step = node.properties.value("step", "10").toFloat();

        for (float x = -size / 2; x <= size / 2; x += step) {
            for (float z = -size / 2; z <= size / 2; z += step) {
                PCGPoint p;
                p.x = x; p.z = z;
                if (ctx.sampleHeight)
                    p.y = ctx.sampleHeight(x, z);
                p.density = 1.0f;
                points.append(p);
            }
        }
        return points;
    }

    static QList<PCGPoint> generateSplinePoints(const PCGNode& node, PCGContext& ctx,
                                                 const PCGGraph& graph) {
        // This would sample along a spline — for now return empty
        Q_UNUSED(node); Q_UNUSED(ctx); Q_UNUSED(graph);
        return {};
    }

    static QList<PCGPoint> generateCustomPoints(const PCGNode& node, PCGContext& ctx) {
        Q_UNUSED(ctx);
        QList<PCGPoint> points;
        int count = node.properties.value("count", "100").toInt();
        for (int i = 0; i < count; i++) {
            PCGPoint p;
            p.density = 1.0f;
            points.append(p);
        }
        return points;
    }

    // ============================================================
    // Sampling node implementations
    // ============================================================

    static QList<PCGPoint> sampleTerrainHeight(const QList<PCGPoint>& inputs, PCGContext& ctx) {
        QList<PCGPoint> result = inputs;
        for (auto& p : result) {
            if (ctx.sampleHeight) {
                p.y = ctx.sampleHeight(p.x, p.z);
                p.attributes["height"] = p.y;
            }
        }
        return result;
    }

    static QList<PCGPoint> sampleSlope(const QList<PCGPoint>& inputs, PCGContext& ctx) {
        QList<PCGPoint> result = inputs;
        for (auto& p : result) {
            if (ctx.sampleSlope) {
                float slope = ctx.sampleSlope(p.x, p.z);
                p.attributes["slope"] = slope;
            }
        }
        return result;
    }

    static QList<PCGPoint> sampleMask(const QList<PCGPoint>& inputs, PCGContext& ctx) {
        QList<PCGPoint> result = inputs;
        for (auto& p : result) {
            if (ctx.sampleMask) {
                float mask = ctx.sampleMask(p.x, p.z);
                p.attributes["mask"] = mask;
            }
        }
        return result;
    }

    static QList<PCGPoint> sampleDistance(const QList<PCGPoint>& inputs, PCGContext& ctx,
                                           const PCGNode& node) {
        QList<PCGPoint> result = inputs;
        float targetX = node.properties.value("targetX", "0").toFloat();
        float targetZ = node.properties.value("targetZ", "0").toFloat();
        for (auto& p : result) {
            float dx = p.x - targetX, dz = p.z - targetZ;
            p.attributes["distance"] = std::sqrt(dx * dx + dz * dz);
        }
        return result;
    }

    // ============================================================
    // Generation node implementations
    // ============================================================

    static QList<PCGPoint> scatterPoints(const PCGNode& node, PCGContext& ctx) {
        QList<PCGPoint> points;
        float areaSize = node.properties.value("areaSize", "4000").toFloat();
        float density = node.properties.value("density", "0.1").toFloat();
        int count = int(areaSize * areaSize * density / 1000);

        for (int i = 0; i < count; i++) {
            PCGPoint p;
            p.x = ctx.randomRange(-areaSize / 2, areaSize / 2);
            p.z = ctx.randomRange(-areaSize / 2, areaSize / 2);
            if (ctx.sampleHeight)
                p.y = ctx.sampleHeight(p.x, p.z);
            p.density = 1.0f;
            p.rotY = ctx.randomRange(0, 360);
            float scaleMin = node.properties.value("scaleMin", "1").toFloat();
            float scaleMax = node.properties.value("scaleMax", "1").toFloat();
            float s = ctx.randomRange(scaleMin, scaleMax);
            p.scaleX = p.scaleY = p.scaleZ = s;
            points.append(p);
        }
        return points;
    }

    static QList<PCGPoint> gridPoints(const PCGNode& node, PCGContext& ctx) {
        QList<PCGPoint> points;
        float areaSize = node.properties.value("areaSize", "4000").toFloat();
        float step = node.properties.value("step", "50").toFloat();

        for (float x = -areaSize / 2; x <= areaSize / 2; x += step) {
            for (float z = -areaSize / 2; z <= areaSize / 2; z += step) {
                PCGPoint p;
                p.x = x; p.z = z;
                if (ctx.sampleHeight)
                    p.y = ctx.sampleHeight(x, z);
                p.density = 1.0f;
                points.append(p);
            }
        }
        return points;
    }

    static QList<PCGPoint> surfaceSampling(const PCGNode& node, PCGContext& ctx) {
        // Same as scatter but with surface alignment
        QList<PCGPoint> points = scatterPoints(node, ctx);
        for (auto& p : points) {
            p.rotX = 0;  // aligned to surface normal (simplified)
        }
        return points;
    }

    // ============================================================
    // Filter node implementations
    // ============================================================

    static QList<PCGPoint> densityFilter(const QList<PCGPoint>& inputs, const PCGNode& node) {
        QList<PCGPoint> result;
        float minDensity = node.properties.value("minDensity", "0.5").toFloat();
        for (const auto& p : inputs)
            if (p.density >= minDensity)
                result.append(p);
        return result;
    }

    static QList<PCGPoint> heightFilter(const QList<PCGPoint>& inputs, const PCGNode& node) {
        QList<PCGPoint> result;
        float minH = node.properties.value("minHeight", "0").toFloat();
        float maxH = node.properties.value("maxHeight", "10000").toFloat();
        for (const auto& p : inputs)
            if (p.y >= minH && p.y <= maxH)
                result.append(p);
        return result;
    }

    static QList<PCGPoint> slopeFilter(const QList<PCGPoint>& inputs, const PCGNode& node) {
        QList<PCGPoint> result;
        float maxSlope = node.properties.value("maxSlope", "45").toFloat();
        for (const auto& p : inputs) {
            float slope = p.attributes.value("slope", 0);
            if (slope <= maxSlope)
                result.append(p);
        }
        return result;
    }

    static QList<PCGPoint> maskFilter(const QList<PCGPoint>& inputs, const PCGNode& node,
                                       PCGContext& ctx) {
        QList<PCGPoint> result;
        float threshold = node.properties.value("threshold", "0.5").toFloat();
        for (const auto& p : inputs) {
            float maskVal = p.attributes.value("mask", 1.0f);
            if (ctx.sampleMask)
                maskVal = ctx.sampleMask(p.x, p.z);
            if (maskVal >= threshold)
                result.append(p);
        }
        return result;
    }

    static QList<PCGPoint> randomFilter(const QList<PCGPoint>& inputs, const PCGNode& node,
                                         PCGContext& ctx) {
        QList<PCGPoint> result;
        float probability = node.properties.value("probability", "0.5").toFloat();
        for (const auto& p : inputs)
            if (ctx.random01() < probability)
                result.append(p);
        return result;
    }

    // ============================================================
    // Transform node implementations
    // ============================================================

    static QList<PCGPoint> randomRotation(const QList<PCGPoint>& inputs, const PCGNode& node,
                                           PCGContext& ctx) {
        QList<PCGPoint> result = inputs;
        bool yAxisOnly = node.properties.value("yAxisOnly", "true") == "true";
        for (auto& p : result) {
            if (yAxisOnly) {
                p.rotY = ctx.randomRange(0, 360);
            } else {
                p.rotX = ctx.randomRange(-180, 180);
                p.rotY = ctx.randomRange(0, 360);
                p.rotZ = ctx.randomRange(-180, 180);
            }
        }
        return result;
    }

    static QList<PCGPoint> randomScale(const QList<PCGPoint>& inputs, const PCGNode& node,
                                        PCGContext& ctx) {
        QList<PCGPoint> result = inputs;
        float minS = node.properties.value("scaleMin", "0.8").toFloat();
        float maxS = node.properties.value("scaleMax", "1.2").toFloat();
        bool uniform = node.properties.value("uniform", "true") == "true";
        for (auto& p : result) {
            if (uniform) {
                float s = ctx.randomRange(minS, maxS);
                p.scaleX = p.scaleY = p.scaleZ = s;
            } else {
                p.scaleX = ctx.randomRange(minS, maxS);
                p.scaleY = ctx.randomRange(minS, maxS);
                p.scaleZ = ctx.randomRange(minS, maxS);
            }
        }
        return result;
    }

    static QList<PCGPoint> alignToSurface(const QList<PCGPoint>& inputs, PCGContext& ctx) {
        QList<PCGPoint> result = inputs;
        for (auto& p : result) {
            if (ctx.sampleHeight)
                p.y = ctx.sampleHeight(p.x, p.z);
        }
        return result;
    }

    static QList<PCGPoint> offsetTransform(const QList<PCGPoint>& inputs, const PCGNode& node) {
        QList<PCGPoint> result = inputs;
        float offsetX = node.properties.value("offsetX", "0").toFloat();
        float offsetY = node.properties.value("offsetY", "0").toFloat();
        float offsetZ = node.properties.value("offsetZ", "0").toFloat();
        for (auto& p : result) {
            p.x += offsetX; p.y += offsetY; p.z += offsetZ;
        }
        return result;
    }
};

} // namespace world
