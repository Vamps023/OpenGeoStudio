// ============================================================
// test_asset_import.cpp — Automated tests for the Assimp model
// importer (ImportedModel). Covers geometry parsing, transform
// baking, UV flipping, unit conversion, materials and texture
// resolution using files generated into a temp directory.
// ============================================================

#include "ImportedModel.hpp"
#include "MaterialAsset.hpp"

#include "../../core/world/WorldTypes.hpp"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QTextStream>
#include <iostream>
#include <cmath>

static int g_passed = 0, g_failed = 0;
static QStringList g_failures;

#define VERIFY(cond, name, msg) do { \
    if (cond) { g_passed++; std::cout << "[PASS] " << name << std::endl; } \
    else { g_failed++; g_failures.append(QString("%1: %2").arg(name).arg(msg)); \
           std::cout << "[FAIL] " << name << " — " << QString(msg).toStdString() << std::endl; } \
} while(0)

static void writeTextFile(const QString& path, const QString& content)
{
    QFile f(path);
    f.open(QIODevice::WriteOnly | QIODevice::Text);
    f.write(content.toUtf8());
    f.close();
}

static void writeTestPng(const QString& path)
{
    QImage img(4, 4, QImage::Format_RGB32);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            img.setPixel(x, y, qRgb(200, 40, 40));
    img.save(path, "PNG");
}

// One quad with explicit normals, a material reference and bottom-left
// UVs — the classic OBJ layout.
static QString makeObjDir(QString* objPathOut)
{
    QTemporaryDir dir;
    dir.setAutoRemove(false);
    const QString d = dir.path();
    writeTextFile(d + "/quad.mtl",
        "newmtl RedMat\n"
        "Kd 0.8 0.1 0.1\n"
        "Ns 96\n"
        "map_Kd tex.png\n");
    writeTextFile(d + "/quad.obj",
        "mtllib quad.mtl\n"
        "v 0 0 0\n"
        "v 2 0 0\n"
        "v 2 2 0\n"
        "v 0 2 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 1 1\n"
        "vt 0 1\n"
        "vn 0 0 1\n"
        "usemtl RedMat\n"
        "f 1/1/1 2/2/1 3/3/1\n"
        "f 1/1/1 3/3/1 4/4/1\n");
    writeTestPng(d + "/tex.png");
    if (objPathOut) *objPathOut = d + "/quad.obj";
    return d;
}

void testExtensionCheck()
{
    VERIFY(assets::isImportableModelFile("a/b/model.FBX"), "Ext: fbx case-insensitive", "FBX should be importable");
    VERIFY(assets::isImportableModelFile("car.glb") && assets::isImportableModelFile("tree.gltf"),
           "Ext: gltf family", "glTF should be importable");
    VERIFY(assets::isImportableModelFile("p.stl") && assets::isImportableModelFile("p.dae") &&
           assets::isImportableModelFile("p.ply") && assets::isImportableModelFile("p.3ds"),
           "Ext: stl/dae/ply/3ds", "these formats should be importable");
    VERIFY(!assets::isImportableModelFile("native.mesh"), "Ext: ogre mesh excluded", ".mesh goes through the OGRE loader, not Assimp");
    VERIFY(!assets::isImportableModelFile("photo.png") && !assets::isImportableModelFile("noext"),
           "Ext: non-models excluded", "textures and extensionless files are not importable models");
}

void testObjGeometryMaterialTexture()
{
    QString objPath;
    const QString dir = makeObjDir(&objPath);

    assets::ImportedModel m = assets::importModel(objPath);
    VERIFY(m.success, "OBJ: import succeeds", m.errorMessage);
    VERIFY(m.subMeshes.size() == 1, "OBJ: single submesh",
           QString("expected 1 submesh, got %1").arg(m.subMeshes.size()));

    const auto& sm = m.subMeshes[0];
    VERIFY(sm.positions.size() / 3 == 4, "OBJ: vertex count",
           QString("expected 4 verts, got %1").arg(sm.positions.size() / 3));
    VERIFY(sm.indices.size() == 6, "OBJ: two triangles",
           QString("expected 6 indices, got %1").arg(sm.indices.size()));
    VERIFY(sm.normals.size() == sm.positions.size(), "OBJ: normals carried",
           "explicit vn data should be present");
    VERIFY(std::abs(sm.normals[2] - 1.0f) < 1e-5f, "OBJ: normal +Z", "normal should be +Z");

    // OBJ UVs are bottom-left origin; the importer must flip v.
    VERIFY(sm.uvs.size() == 8, "OBJ: uvs present", "uv data missing");
    VERIFY(std::abs(sm.uvs[1] - 1.0f) < 1e-5f && std::abs(sm.uvs[7] - 0.0f) < 1e-5f,
           "OBJ: v flipped to top-left origin",
           QString("v0=%1 v3=%2 expected 1.0/0.0").arg(sm.uvs[1]).arg(sm.uvs[7]));

    // Assimp prepends a DefaultMaterial at index 0 for OBJ; resolve the
    // material through the submesh's own index like the renderer does.
    VERIFY(m.materials.size() >= 2 && sm.materialIndex >= 0 &&
           sm.materialIndex < m.materials.size(),
           "OBJ: materials parsed", "RedMat + default should exist");
    if (sm.materialIndex >= 0 && sm.materialIndex < m.materials.size()) {
        const auto& mat = m.materials[sm.materialIndex];
        VERIFY(mat.name == "RedMat", "OBJ: material name", mat.name);
        VERIFY(std::abs(mat.baseColorR - 0.8f) < 0.05f &&
               std::abs(mat.baseColorG - 0.1f) < 0.05f,
               "OBJ: Kd base color", QString("r=%1 g=%2").arg(mat.baseColorR).arg(mat.baseColorG));
        VERIFY(mat.albedoTexture.endsWith("tex.png", Qt::CaseInsensitive) &&
               QFile::exists(mat.albedoTexture),
               "OBJ: map_Kd texture resolved",
               QString("got '%1'").arg(mat.albedoTexture));
    }

    // OBJ is unitless and small — the importer should keep scale 1.0.
    VERIFY(std::abs(m.appliedScale - 1.0f) < 1e-6f, "OBJ: no unit conversion",
           QString("appliedScale=%1").arg(m.appliedScale));

    QDir(dir).removeRecursively();
}

void testScaleOverride()
{
    QString objPath;
    const QString dir = makeObjDir(&objPath);

    assets::ImportedModel m = assets::importModel(objPath, 2.0f);
    VERIFY(m.success && std::abs(m.appliedScale - 2.0f) < 1e-6f, "Scale: override applied",
           QString("appliedScale=%1").arg(m.appliedScale));
    if (m.success && m.subMeshes.size() == 1) {
        const auto& sm = m.subMeshes[0];
        // Quad spans 2 authored units → 4 after the ×2 override.
        float mnX = 1e30f, mxX = -1e30f;
        for (size_t v = 0; v + 2 < sm.positions.size(); v += 3) {
            mnX = std::min(mnX, sm.positions[v]);
            mxX = std::max(mxX, sm.positions[v]);
        }
        VERIFY(std::abs((mxX - mnX) - 4.0f) < 1e-4f, "Scale: positions scaled",
               QString("x extent=%1").arg(mxX - mnX));
    }

    QDir(dir).removeRecursively();
}

void testGeneratedNormalsAndWinding()
{
    QTemporaryDir dir;
    const QString d = dir.path();
    // Triangle soup without normals — GenSmoothNormals must fill them in.
    writeTextFile(d + "/tri.obj",
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");

    assets::ImportedModel m = assets::importModel(d + "/tri.obj");
    VERIFY(m.success, "Normals: import succeeds", m.errorMessage);
    if (m.success) {
        const auto& sm = m.subMeshes[0];
        VERIFY(sm.normals.size() == sm.positions.size(), "Normals: generated",
               "missing normals should be generated");
        // CCW winding with +Z front → generated normal points +Z.
        VERIFY(std::abs(sm.normals[2] - 1.0f) < 1e-3f, "Normals: +Z for CCW triangle",
               QString("n=%1").arg(sm.normals[2]));
    }
}

void testStlImport()
{
    QTemporaryDir dir;
    const QString d = dir.path();
    writeTextFile(d + "/tri.stl",
        "solid test\n"
        "  facet normal 0 0 1\n"
        "    outer loop\n"
        "      vertex 0 0 0\n"
        "      vertex 1 0 0\n"
        "      vertex 0 1 0\n"
        "    endloop\n"
        "  endfacet\n"
        "endsolid test\n");

    assets::ImportedModel m = assets::importModel(d + "/tri.stl");
    VERIFY(m.success, "STL: import succeeds", m.errorMessage);
    if (m.success) {
        VERIFY(m.subMeshes.size() == 1 && m.subMeshes[0].indices.size() == 3,
               "STL: one triangle", "single facet should give 3 indices");
        VERIFY(m.materials.size() >= 1, "STL: default material", "a default material slot should exist");
    }
}

void testFailurePaths()
{
    assets::ImportedModel missing = assets::importModel("Z:/definitely/not/here.obj");
    VERIFY(!missing.success, "Errors: missing file fails", "should not succeed");
    VERIFY(missing.errorMessage.contains("not found", Qt::CaseInsensitive),
           "Errors: message mentions file", missing.errorMessage);

    QTemporaryDir dir;
    writeTextFile(dir.path() + "/empty.obj", "# just a comment\nv 0 0 0\n");
    assets::ImportedModel noGeo = assets::importModel(dir.path() + "/empty.obj");
    VERIFY(!noGeo.success, "Errors: no geometry fails", "vertex-only file has no faces");
    VERIFY(noGeo.errorMessage.contains("triangle", Qt::CaseInsensitive),
           "Errors: message mentions geometry", noGeo.errorMessage);
}

void testMaterialAssetRoundTrip()
{
    QTemporaryDir dir;
    const QString path = dir.path() + "/brick.ogsmat";

    assets::MaterialAsset mat;
    mat.name = "Brick";
    mat.baseColorR = 0.7f; mat.baseColorG = 0.2f; mat.baseColorB = 0.15f;
    mat.roughness = 0.85f;
    mat.metalness = 0.05f;
    mat.albedoTexture = "../Textures/brick_alb.png";
    mat.normalTexture = "brick_nrm.png";
    VERIFY(mat.saveToFile(path), "MatAsset: save", "saveToFile returned false");
    VERIFY(QFile::exists(path), "MatAsset: file created", "no .ogsmat on disk");

    const assets::MaterialAsset loaded = assets::MaterialAsset::loadFromFile(path);
    VERIFY(loaded.name == "Brick", "MatAsset: name round-trip", loaded.name);
    VERIFY(std::abs(loaded.baseColorR - mat.baseColorR) < 1e-4f &&
           std::abs(loaded.baseColorG - mat.baseColorG) < 1e-4f &&
           std::abs(loaded.baseColorB - mat.baseColorB) < 1e-4f,
           "MatAsset: base color round-trip", "color values diverged");
    VERIFY(std::abs(loaded.roughness - 0.85f) < 1e-4f &&
           std::abs(loaded.metalness - 0.05f) < 1e-4f,
           "MatAsset: PBR round-trip", "roughness/metalness diverged");

    // Relative texture refs resolve against the .ogsmat's folder.
    const QString alb = loaded.resolveTexture(path, loaded.albedoTexture);
    const QString nrm = loaded.resolveTexture(path, loaded.normalTexture);
    VERIFY(alb.endsWith("Textures/brick_alb.png") && QFileInfo(alb).isAbsolute(),
           "MatAsset: relative albedo resolved", alb);
    VERIFY(nrm.endsWith("brick_nrm.png") && nrm.contains(dir.path()),
           "MatAsset: sibling normal resolved", nrm);

    // Name falls back to the file name when absent.
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(R"({"roughness":0.4,"baseColor":[0.1,0.2,0.3]})");
    f.close();
    const assets::MaterialAsset unnamed = assets::MaterialAsset::loadFromFile(path);
    VERIFY(unnamed.name == "brick", "MatAsset: name fallback", unnamed.name);
    VERIFY(std::abs(unnamed.roughness - 0.4f) < 1e-4f,
           "MatAsset: unnamed round-trip", "values lost");
}

void testActorMaterialFields()
{
    world::Actor a;
    a.roughness = 0.35f;
    a.metalness = 0.9f;
    a.albedoTexturePath = "C:/t/alb.png";
    a.normalTexturePath = "C:/t/nrm.png";

    const world::Actor b = world::Actor::fromJson(a.toJson());
    VERIFY(std::abs(b.roughness - 0.35f) < 1e-4f &&
           std::abs(b.metalness - 0.9f) < 1e-4f,
           "Actor: PBR override round-trip",
           QString("r=%1 m=%2").arg(b.roughness).arg(b.metalness));
    VERIFY(b.albedoTexturePath == a.albedoTexturePath &&
           b.normalTexturePath == a.normalTexturePath,
           "Actor: texture paths round-trip", "texture paths lost");

    // Unset (negative) values must survive as unset, not clamp to zero.
    world::Actor c;
    const world::Actor d = world::Actor::fromJson(c.toJson());
    VERIFY(d.roughness < 0.0f && d.metalness < 0.0f,
           "Actor: unset stays unset",
           QString("r=%1 m=%2").arg(d.roughness).arg(d.metalness));
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    testExtensionCheck();
    testObjGeometryMaterialTexture();
    testScaleOverride();
    testGeneratedNormalsAndWinding();
    testStlImport();
    testFailurePaths();
    testMaterialAssetRoundTrip();
    testActorMaterialFields();

    std::cout << "\n==== test_asset_import summary ====" << std::endl;
    std::cout << "Passed: " << g_passed << "  Failed: " << g_failed << std::endl;
    if (!g_failures.isEmpty()) {
        std::cout << "Failures:" << std::endl;
        for (const QString& f : g_failures)
            std::cout << "  - " << f.toStdString() << std::endl;
    }
    return g_failed == 0 ? 0 : 1;
}
