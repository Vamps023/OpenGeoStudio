// MaterialAsset — .ogsmat read/write implementation

#include "MaterialAsset.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace assets {

bool MaterialAsset::saveToFile(const QString& path) const
{
    QJsonObject j;
    if (!name.isEmpty()) j["name"] = name;
    QJsonArray color;
    color.append(baseColorR);
    color.append(baseColorG);
    color.append(baseColorB);
    j["baseColor"] = color;
    j["roughness"] = roughness;
    j["metalness"] = metalness;
    if (!albedoTexture.isEmpty()) j["albedoTexture"] = albedoTexture;
    if (!normalTexture.isEmpty()) j["normalTexture"] = normalTexture;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(QJsonDocument(j).toJson(QJsonDocument::Indented));
    return true;
}

MaterialAsset MaterialAsset::loadFromFile(const QString& path)
{
    MaterialAsset mat;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return mat;   // empty name = unreadable

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return mat;

    const QJsonObject j = doc.object();
    mat.name = j["name"].toString();
    if (mat.name.isEmpty())
        mat.name = QFileInfo(path).completeBaseName();

    const QJsonArray color = j["baseColor"].toArray();
    if (color.size() == 3) {
        mat.baseColorR = static_cast<float>(color.at(0).toDouble(0.8));
        mat.baseColorG = static_cast<float>(color.at(1).toDouble(0.8));
        mat.baseColorB = static_cast<float>(color.at(2).toDouble(0.8));
    }
    mat.roughness  = static_cast<float>(j["roughness"].toDouble(0.5));
    mat.metalness  = static_cast<float>(j["metalness"].toDouble(0.0));
    mat.albedoTexture = j["albedoTexture"].toString();
    mat.normalTexture = j["normalTexture"].toString();
    return mat;
}

QString MaterialAsset::resolveTexture(const QString& matPath,
                                      const QString& ref) const
{
    if (ref.isEmpty())
        return QString();
    if (QFileInfo(ref).isAbsolute())
        return QDir::cleanPath(ref);
    // Relative refs resolve against the .ogsmat's own folder.
    return QDir::cleanPath(
        QDir(QFileInfo(matPath).absolutePath()).filePath(ref));
}

} // namespace assets
