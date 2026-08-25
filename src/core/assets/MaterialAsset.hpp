#pragma once

// ============================================================
// MaterialAsset — reusable .ogsmat material files
// ============================================================
//
// Small JSON document describing one PBR material slot:
//   { "name": "...", "baseColor": [r,g,b],
//     "roughness": ..., "metalness": ...,
//     "albedoTexture": "...", "normalTexture": "..." }
//
// Texture references may be absolute or relative; relative refs
// resolve against the .ogsmat file's own folder (resolveTexture),
// so assets stay portable inside a project tree.
//
// An empty name after loadFromFile() means the file could not be
// read — callers use that as the error signal.

#include <QString>

namespace assets {

struct MaterialAsset {
    QString name;
    float baseColorR = 0.8f, baseColorG = 0.8f, baseColorB = 0.8f;
    float roughness = 0.5f;
    float metalness = 0.0f;
    QString albedoTexture;   // relative to the .ogsmat, or absolute
    QString normalTexture;

    // Write this asset as JSON. Returns false on I/O failure.
    bool saveToFile(const QString& path) const;

    // Parse a .ogsmat. On failure returns a default asset with an
    // empty name. A JSON body without a name falls back to the
    // file's base name ("brick.ogsmat" -> "brick").
    static MaterialAsset loadFromFile(const QString& path);

    // Resolve one texture reference against the folder containing
    // the .ogsmat at matPath. Absolute refs pass through; empty
    // refs return empty.
    QString resolveTexture(const QString& matPath, const QString& ref) const;
};

} // namespace assets
