#pragma once

// ============================================================
// CRSSearch.hpp — CRS database search utilities
//
// Provides higher-level search functions on top of CRSManager
// for the UI layer. Supports:
//   - Text search (EPSG code, name, authority:code)
//   - Authority filter
//   - Kind filter (geographic, projected, compound, etc.)
//   - Area filter
//   - Bounding box intersection filter
//   - Recent CRS list
// ============================================================

#include "CRS.hpp"
#include "CRSManager.hpp"
#include <vector>
#include <string>

namespace gis {

// ============================================================
// CRSSearch — stateless search helpers
// ============================================================
class CRSSearch {
public:
    // Search by text query with optional filters
    static std::vector<CRSSearchResult> search(
        const std::string& query,
        const std::string& authorityFilter = "",
        CRSKind kindFilter = CRSKind::Unknown,
        const std::string& areaFilter = "",
        int maxResults = 50);

    // Search by EPSG code
    static std::optional<CRSDefinition> findByEPSG(int code);

    // Search by authority:code
    static std::optional<CRSDefinition> findByAuthId(const std::string& authId);

    // Get CRS by any identifier (EPSG code, auth:code, WKT, PROJJSON)
    static std::optional<CRSDefinition> findByAny(const std::string& identifier);

    // Get common CRS for quick selection
    static std::vector<CRSDefinition> commonCRS();

    // Get CRS for a specific region
    static std::vector<CRSDefinition> crsForRegion(const std::string& region);

    // Get UTM CRS for a lat/lon position
    static std::optional<CRSDefinition> utmForPosition(double lat, double lon);

    // --------------------------------------------------------
    // Recent CRS list (persisted in user settings)
    // --------------------------------------------------------

    static std::vector<CRSDefinition> recentCRS(int maxCount = 10);
    static void addRecent(const CRSDefinition& crs);
    static void clearRecent();

    // --------------------------------------------------------
    // Favorites
    // --------------------------------------------------------

    static std::vector<CRSDefinition> favoriteCRS();
    static void addFavorite(const CRSDefinition& crs);
    static void removeFavorite(const std::string& authId);
    static bool isFavorite(const std::string& authId);
};

} // namespace gis
