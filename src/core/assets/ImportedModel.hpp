#pragma once

// ============================================================
// ImportedModel — Assimp-based model import for the 3D Studio
//
// Parses FBX / glTF / glb / OBJ / STL / DAE / PLY / 3DS files
// into pure geometry + material data. Rendering-free: the OGRE
// side (mesh + HLMS datablocks) is built in studio3d from this
// structure, keeping src/core independent of the engine.
//
// Conventions applied here so consumers can use data as-is:
//   - right-handed, Y-up, metres
//   - CCW front faces
//   - UVs with origin at the top-left (v grows downward)
// ============================================================

#include <QString>
#include <QList>
#include <vector>
#include <cstdint>

namespace assets {

// PBR-ish description of one imported material slot.
struct ImportedMaterial {
    QString name;
    float baseColorR = 0.8f, baseColorG = 0.8f, baseColorB = 0.8f;
    float roughness = 0.5f;
    float metalness = 0.0f;
    QString albedoTexture;   // absolute path (external or extracted); empty = none
    QString normalTexture;
};

// One draw call: a triangle soup sharing a material.
struct ImportedSubMesh {
    QString name;
    int materialIndex = -1;
    std::vector<float> positions;   // xyz per vertex, metres
    std::vector<float> normals;     // xyz per vertex; empty when missing
    std::vector<float> uvs;         // uv per vertex; empty when missing
    std::vector<uint32_t> indices;
};

struct ImportedModel {
    QString sourcePath;
    float appliedScale = 1.0f;      // unit conversion actually applied
    QList<ImportedSubMesh> subMeshes;
    QList<ImportedMaterial> materials;

    bool success = false;
    QString errorMessage;
};

// Extension check for files the 3D Studio can import (vs OGRE-native .mesh).
bool isImportableModelFile(const QString& path);

// Parse a model file. scaleOverride <= 0 picks the suggested unit
// conversion (e.g. FBX files authored in centimetres get 0.01).
ImportedModel importModel(const QString& path, float scaleOverride = 0.0f);

} // namespace assets
