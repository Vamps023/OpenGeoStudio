// ============================================================
// CRSSearch.cpp — CRS database search utilities
// ============================================================

#include "CRSSearch.hpp"

#include <QSettings>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>

#include <algorithm>
#include <chrono>

namespace gis {

// ============================================================
// Search
// ============================================================
std::vector<CRSSearchResult> CRSSearch::search(
    const std::string& query,
    const std::string& authorityFilter,
    CRSKind kindFilter,
    const std::string& areaFilter,
    int maxResults) {

    CRSSearchFilter filter;
    filter.query = query;
    filter.authority = authorityFilter;
    filter.kind = kindFilter;
    filter.area = areaFilter;
    filter.maxResults = maxResults;

    return CRSManager::instance().search(filter);
}

std::optional<CRSDefinition> CRSSearch::findByEPSG(int code) {
    return CRSManager::instance().fromEPSG(code);
}

std::optional<CRSDefinition> CRSSearch::findByAuthId(const std::string& authId) {
    return CRSManager::instance().fromAuthId(authId);
}

std::optional<CRSDefinition> CRSSearch::findByAny(const std::string& identifier) {
    return CRSManager::instance().fromAny(identifier);
}

// ============================================================
// Common CRS for quick selection
// ============================================================
std::vector<CRSDefinition> CRSSearch::commonCRS() {
    std::vector<CRSDefinition> result;
    auto& mgr = CRSManager::instance();

    // WGS 84
    auto wgs84 = mgr.fromEPSG(4326);
    if (wgs84) result.push_back(*wgs84);

    // Web Mercator
    auto webMerc = mgr.fromEPSG(3857);
    if (webMerc) result.push_back(*webMerc);

    // WGS 84 / UTM zones (a few common ones)
    for (int zone : {1, 10, 17, 18, 19, 20, 29, 30, 31, 33, 43, 44, 50, 51, 52, 53}) {
        auto utm = mgr.fromEPSG(32600 + zone);
        if (utm) result.push_back(*utm);
    }

    // EGM96 height (vertical)
    auto egm96 = mgr.fromEPSG(5773);
    if (egm96) result.push_back(*egm96);

    return result;
}

// ============================================================
// CRS for a specific region
// ============================================================
std::vector<CRSDefinition> CRSSearch::crsForRegion(const std::string& region) {
    auto results = search("", "", CRSKind::Unknown, region, 50);
    std::vector<CRSDefinition> crsList;
    crsList.reserve(results.size());
    for (const auto& r : results) {
        crsList.push_back(r.crs);
    }
    return crsList;
}

// ============================================================
// UTM for position
// ============================================================
std::optional<CRSDefinition> CRSSearch::utmForPosition(double lat, double lon) {
    return CRSManager::instance().autoUtm(lat, lon);
}

// ============================================================
// Recent CRS list (persisted via QSettings)
// ============================================================
static const char* RECENT_KEY = "crs/recent";
static const int MAX_RECENT = 20;

static CRSDefinition fromJson(const QJsonObject& j) {
    CRSDefinition def;
    def.authority = j["authority"].toString().toStdString();
    def.code = j["code"].toInt();
    def.name = j["name"].toString().toStdString();
    def.wkt2 = j["wkt2"].toString().toStdString();
    def.projJson = j["projJson"].toString().toStdString();
    def.kind = static_cast<CRSKind>(j["kind"].toInt());
    def.unit = static_cast<Unit>(j["unit"].toInt());
    def.areaOfUseName = j["areaOfUseName"].toString().toStdString();
    def.hasVerticalComponent = j["hasVertical"].toBool();
    def.verticalCrsName = j["verticalCrsName"].toString().toStdString();
    def.verticalCrsCode = j["verticalCrsCode"].toInt();
    return def;
}

static QJsonObject toJson(const CRSDefinition& def) {
    QJsonObject j;
    j["authority"] = QString::fromStdString(def.authority);
    j["code"] = def.code;
    j["name"] = QString::fromStdString(def.name);
    j["wkt2"] = QString::fromStdString(def.wkt2);
    j["projJson"] = QString::fromStdString(def.projJson);
    j["kind"] = static_cast<int>(def.kind);
    j["unit"] = static_cast<int>(def.unit);
    j["areaOfUseName"] = QString::fromStdString(def.areaOfUseName);
    j["hasVertical"] = def.hasVerticalComponent;
    j["verticalCrsName"] = QString::fromStdString(def.verticalCrsName);
    j["verticalCrsCode"] = def.verticalCrsCode;
    return j;
}

std::vector<CRSDefinition> CRSSearch::recentCRS(int maxCount) {
    std::vector<CRSDefinition> result;

    QSettings settings;
    QJsonArray arr = settings.value(RECENT_KEY).toJsonArray();

    int count = std::min(static_cast<int>(arr.size()), maxCount);
    for (int i = 0; i < count; ++i) {
        QJsonObject j = arr[i].toObject();
        result.push_back(fromJson(j));
    }

    return result;
}

void CRSSearch::addRecent(const CRSDefinition& crs) {
    if (!crs.isValid()) return;

    QSettings settings;
    QJsonArray arr = settings.value(RECENT_KEY).toJsonArray();

    // Remove if already exists (move to front)
    QString authId = QString::fromStdString(crs.authId());
    for (int i = arr.size() - 1; i >= 0; --i) {
        QJsonObject j = arr[i].toObject();
        if (j["authority"].toString() == QString::fromStdString(crs.authority) &&
            j["code"].toInt() == crs.code) {
            arr.removeAt(i);
        }
    }

    // Add to front
    arr.prepend(toJson(crs));

    // Trim
    while (arr.size() > MAX_RECENT) {
        arr.removeLast();
    }

    settings.setValue(RECENT_KEY, arr);
}

void CRSSearch::clearRecent() {
    QSettings settings;
    settings.remove(RECENT_KEY);
}

// ============================================================
// Favorites (persisted via QSettings)
// ============================================================
static const char* FAVORITE_KEY = "crs/favorites";

std::vector<CRSDefinition> CRSSearch::favoriteCRS() {
    std::vector<CRSDefinition> result;

    QSettings settings;
    QJsonArray arr = settings.value(FAVORITE_KEY).toJsonArray();

    for (const auto& v : arr) {
        result.push_back(fromJson(v.toObject()));
    }

    return result;
}

void CRSSearch::addFavorite(const CRSDefinition& crs) {
    if (!crs.isValid()) return;
    if (isFavorite(crs.authId())) return;

    QSettings settings;
    QJsonArray arr = settings.value(FAVORITE_KEY).toJsonArray();
    arr.append(toJson(crs));
    settings.setValue(FAVORITE_KEY, arr);
}

void CRSSearch::removeFavorite(const std::string& authId) {
    QSettings settings;
    QJsonArray arr = settings.value(FAVORITE_KEY).toJsonArray();
    QJsonArray newArr;

    QString target = QString::fromStdString(authId);
    for (const auto& v : arr) {
        QJsonObject j = v.toObject();
        QString current = j["authority"].toString() + ":" +
            QString::number(j["code"].toInt());
        if (current != target) {
            newArr.append(v);
        }
    }

    settings.setValue(FAVORITE_KEY, newArr);
}

bool CRSSearch::isFavorite(const std::string& authId) {
    QSettings settings;
    QJsonArray arr = settings.value(FAVORITE_KEY).toJsonArray();

    QString target = QString::fromStdString(authId);
    for (const auto& v : arr) {
        QJsonObject j = v.toObject();
        QString current = j["authority"].toString() + ":" +
            QString::number(j["code"].toInt());
        if (current == target) return true;
    }

    return false;
}

} // namespace gis
