#include "ImportedModel.hpp"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QStringList>
#include <QUrl>
#include <QCryptographicHash>

#include <algorithm>
#include <functional>

#include "../../core/logger/Logger.hpp"

namespace assets {

bool isImportableModelFile(const QString& path)
{
    static const QStringList kImportable = {
        "fbx", "glb", "gltf", "obj", "stl", "dae", "ply", "3ds"
    };
    return kImportable.contains(QFileInfo(path).suffix().toLower());
}

namespace {

// FBX/OBJ/COLLADA store UVs with origin at the bottom-left; OGRE (and
// glTF) use top-left. Flip v for the bottom-left formats only.
bool formatFlipsUVs(const QString& suffix)
{
    static const QStringList kFlip = {"fbx", "obj", "dae", "3ds", "ase"};
    return kFlip.contains(suffix);
}

// Embedded textures (glb, some FBX) live in aiTexture blobs. Write them to
// a stable temp path keyed by model + index so repeat imports reuse them.
QString extractEmbeddedTexture(const aiTexture* tex, const QFileInfo& modelFi,
                               int index)
{
    QString ext = QString::fromLatin1(tex->achFormatHint).left(3).toLower();
    if (ext.isEmpty()) ext = "png";
    const QString key = QString::fromLatin1(QCryptographicHash::hash(
        (modelFi.absoluteFilePath() + QChar('#') + QString::number(index)).toUtf8(),
        QCryptographicHash::Sha1).toHex()).left(24);
    const QString outPath = QDir::temp().filePath(
        "ogs_textures/" + key + "." + ext);
    if (QFile::exists(outPath)) return outPath;

    QDir().mkpath(QFileInfo(outPath).absolutePath());
    QFile f(outPath);
    if (tex->mHeight == 0) {
        // Compressed image data (png/jpg/tga/…)
        if (f.open(QIODevice::WriteOnly)) {
            f.write(reinterpret_cast<const char*>(tex->pcData),
                    qint64(tex->mWidth));
            f.close();
            return outPath;
        }
    } else {
        // Uncompressed 32-bit data. aiTexel is BGRA which matches
        // QImage::Format_ARGB32 byte order on little-endian.
        QImage image(reinterpret_cast<const uchar*>(tex->pcData),
                     int(tex->mWidth), int(tex->mHeight),
                     QImage::Format_ARGB32);
        if (!image.isNull() && image.save(outPath))
            return outPath;
    }
    return QString();
}

// Resolve one texture slot of a material to an absolute file path,
// trying the preferred type first (e.g. glTF base color) then the
// legacy fallback (FBX/OBJ diffuse).
QString resolveTexture(const aiMaterial* am, const aiScene* scene,
                       const QFileInfo& modelFi, aiTextureType preferred,
                       aiTextureType fallback)
{
    aiString path;
    if (am->GetTexture(preferred, 0, &path) != aiReturn_SUCCESS &&
        am->GetTexture(fallback, 0, &path) != aiReturn_SUCCESS)
        return QString();

    QString ref = QString::fromUtf8(path.C_Str());
    ref.replace('\\', '/');
    if (ref.isEmpty()) return QString();

    if (ref.startsWith('*')) {
        bool ok = false;
        const int idx = ref.mid(1).toInt(&ok);
        if (!ok || idx < 0 || idx >= int(scene->mNumTextures)) return QString();
        return extractEmbeddedTexture(scene->mTextures[idx], modelFi, idx);
    }

    // glTF URIs are percent-encoded; harmless for plain paths.
    const QString dec = QUrl::fromPercentEncoding(ref.toUtf8());
    if (QFileInfo(dec).isAbsolute() && QFile::exists(dec))
        return QFileInfo(dec).absoluteFilePath();
    const QString beside = modelFi.dir().filePath(dec);
    if (QFile::exists(beside))
        return QFileInfo(beside).absoluteFilePath();
    // Last resort: same directory, keep original casing.
    const QString besideRaw = modelFi.dir().filePath(ref);
    if (QFile::exists(besideRaw))
        return QFileInfo(besideRaw).absoluteFilePath();
    return QString();
}

} // namespace

ImportedModel importModel(const QString& path, float scaleOverride)
{
    ImportedModel result;
    result.sourcePath = path;

    const QFileInfo fi(path);
    if (!fi.exists() || fi.isDir()) {
        result.errorMessage = "File not found: " + path;
        return result;
    }

    Assimp::Importer importer;
    // FBX pivot nodes blow meshes apart into transforms; baking them away
    // gives cleaner geometry than preserving the pivots.
    importer.SetPropertyInteger(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, 0);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_NO_SKELETON_MESHES, true);

    const unsigned flags = aiProcess_Triangulate
                         | aiProcess_JoinIdenticalVertices
                         | aiProcess_GenSmoothNormals
                         | aiProcess_CalcTangentSpace
                         | aiProcess_ImproveCacheLocality
                         | aiProcess_FixInfacingNormals
                         | aiProcess_SortByPType
                         | aiProcess_OptimizeMeshes;

    const aiScene* scene = importer.ReadFile(path.toStdString(), flags);
    if (!scene || !scene->mRootNode ||
        (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)) {
        result.errorMessage =
            QString::fromUtf8(importer.GetErrorString());
        return result;
    }

    // ── Materials ──────────────────────────────────────────────
    for (unsigned m = 0; m < scene->mNumMaterials; ++m) {
        const aiMaterial* am = scene->mMaterials[m];
        ImportedMaterial mat;
        aiString n;
        if (am->Get(AI_MATKEY_NAME, n) == aiReturn_SUCCESS)
            mat.name = QString::fromUtf8(n.C_Str());

        aiColor3D c;
        if (am->Get(AI_MATKEY_BASE_COLOR, c) == aiReturn_SUCCESS ||
            am->Get(AI_MATKEY_COLOR_DIFFUSE, c) == aiReturn_SUCCESS) {
            mat.baseColorR = c.r; mat.baseColorG = c.g; mat.baseColorB = c.b;
        }
        am->Get(AI_MATKEY_ROUGHNESS_FACTOR, mat.roughness);
        am->Get(AI_MATKEY_METALLIC_FACTOR, mat.metalness);
        mat.roughness = std::clamp(mat.roughness, 0.02f, 1.0f);
        mat.metalness = std::clamp(mat.metalness, 0.0f, 1.0f);

        mat.albedoTexture = resolveTexture(am, scene, fi,
                                           aiTextureType_BASE_COLOR,
                                           aiTextureType_DIFFUSE);
        mat.normalTexture = resolveTexture(am, scene, fi,
                                           aiTextureType_NORMALS,
                                           aiTextureType_HEIGHT);
        result.materials.append(mat);
    }

    // ── Geometry: walk the node graph, baking accumulated transforms ──
    const QString suffix = fi.suffix().toLower();
    const bool flipUV = formatFlipsUVs(suffix);

    size_t totalVerts = 0;
    std::function<void(const aiNode*, const aiMatrix4x4&)> bake =
        [&](const aiNode* node, const aiMatrix4x4& parentXform) {
        const aiMatrix4x4 xform = parentXform * node->mTransformation;
        const aiMatrix3x3 normalMat(xform);

        for (unsigned mi = 0; mi < node->mNumMeshes; ++mi) {
            const aiMesh* mesh = scene->mMeshes[node->mMeshes[mi]];
            if ((mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE) == 0)
                continue;  // point/line meshes are not renderable props

            ImportedSubMesh sm;
            if (mesh->mName.length)
                sm.name = QString::fromUtf8(mesh->mName.C_Str());
            sm.materialIndex = int(mesh->mMaterialIndex);

            const size_t vc = mesh->mNumVertices;
            const bool hasNormals = mesh->mNormals != nullptr;
            const bool hasUV = mesh->mTextureCoords[0] != nullptr;
            sm.positions.resize(vc * 3);
            if (hasNormals) sm.normals.resize(vc * 3);
            if (hasUV) sm.uvs.resize(vc * 2);

            for (size_t v = 0; v < vc; ++v) {
                const aiVector3D p = xform * mesh->mVertices[v];
                sm.positions[v * 3 + 0] = p.x;
                sm.positions[v * 3 + 1] = p.y;
                sm.positions[v * 3 + 2] = p.z;

                if (hasNormals) {
                    aiVector3D nm = normalMat * mesh->mNormals[v];
                    if (nm.Length() > 1e-12f) nm.Normalize();
                    sm.normals[v * 3 + 0] = nm.x;
                    sm.normals[v * 3 + 1] = nm.y;
                    sm.normals[v * 3 + 2] = nm.z;
                }
                if (hasUV) {
                    sm.uvs[v * 2 + 0] = mesh->mTextureCoords[0][v].x;
                    float tv = mesh->mTextureCoords[0][v].y;
                    if (flipUV) tv = 1.0f - tv;
                    sm.uvs[v * 2 + 1] = tv;
                }
            }
            sm.indices.reserve(size_t(mesh->mNumFaces) * 3);
            for (unsigned f = 0; f < mesh->mNumFaces; ++f) {
                const aiFace& face = mesh->mFaces[f];
                for (unsigned k = 0; k < face.mNumIndices; ++k)
                    sm.indices.push_back(face.mIndices[k]);
            }
            totalVerts += vc;
            result.subMeshes.append(std::move(sm));
        }

        for (unsigned c = 0; c < node->mNumChildren; ++c)
            bake(node->mChildren[c], xform);
    };
    bake(scene->mRootNode, aiMatrix4x4());

    if (result.subMeshes.isEmpty() || totalVerts == 0) {
        result.errorMessage = "No triangle geometry found in " + fi.fileName();
        return result;
    }
    if (totalVerts > 20'000'000) {
        result.errorMessage = QString("Model too heavy for the viewport (%1 vertices)")
                                  .arg(qulonglong(totalVerts));
        return result;
    }

    // ── Unit conversion ────────────────────────────────────────
    // Measure the bounding box at the authored scale, then suggest a
    // metres conversion. FBX files are frequently authored in centimetres;
    // glTF is metres by spec and OBJ is unitless (usually metres).
    float mnX = 1e30f, mnY = 1e30f, mnZ = 1e30f;
    float mxX = -1e30f, mxY = -1e30f, mxZ = -1e30f;
    for (const auto& sm : result.subMeshes) {
        for (size_t v = 0; v + 2 < sm.positions.size(); v += 3) {
            mnX = std::min(mnX, sm.positions[v]);     mxX = std::max(mxX, sm.positions[v]);
            mnY = std::min(mnY, sm.positions[v + 1]); mxY = std::max(mxY, sm.positions[v + 1]);
            mnZ = std::min(mnZ, sm.positions[v + 2]); mxZ = std::max(mxZ, sm.positions[v + 2]);
        }
    }
    const float maxDim = std::max({mxX - mnX, mxY - mnY, mxZ - mnZ});
    float suggested = 1.0f;
    if ((suffix == "fbx" || suffix == "dae" || suffix == "3ds") && maxDim > 50.0f)
        suggested = 0.01f;  // centimetre-authored file

    result.appliedScale = scaleOverride > 0.0f ? scaleOverride : suggested;
    if (result.appliedScale != 1.0f) {
        for (auto& sm : result.subMeshes)
            for (auto& p : sm.positions) p *= result.appliedScale;
    }

    result.success = true;
    appLog().info("Imported '{}' → {} submeshes, {} verts (scale {})",
                  fi.fileName(), result.subMeshes.size(), totalVerts,
                  result.appliedScale);
    return result;
}

} // namespace assets
