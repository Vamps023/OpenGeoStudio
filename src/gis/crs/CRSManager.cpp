// ============================================================
// CRSManager.cpp — PROJ-backed CRS database and factory
// ============================================================

#include "CRSManager.hpp"

#include <proj.h>
#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace gis {

// ============================================================
// Helper: find proj.db directory
// ============================================================
static std::string findProjDataDir() {
    // 1. Check PROJ_DATA env var
    const char* envProjData = std::getenv("PROJ_DATA");
    if (envProjData && *envProjData) return envProjData;

    // 2. Check PROJ_LIB env var (deprecated but still supported)
    const char* envProjLib = std::getenv("PROJ_LIB");
    if (envProjLib && *envProjLib) return envProjLib;

    // 3. Try to find proj.db relative to the executable
#ifdef _WIN32
    char exePath[MAX_PATH] = {0};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::string exeDir(exePath);
    auto lastSlash = exeDir.find_last_of("\\/");
    if (lastSlash != std::string::npos) exeDir = exeDir.substr(0, lastSlash);

    // Check exeDir/proj/proj.db
    std::string candidate = exeDir + "\\proj\\proj.db";
    if (GetFileAttributesA(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return exeDir + "\\proj";
    }

    // Check exeDir/proj.db
    candidate = exeDir + "\\proj.db";
    if (GetFileAttributesA(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return exeDir;
    }
#endif

    return "";
}

// ============================================================
// Singleton
// ============================================================
CRSManager& CRSManager::instance() {
    static CRSManager inst;
    return inst;
}

CRSManager::CRSManager() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_ctx = reinterpret_cast<ProjContext*>(proj_context_create());
    if (m_ctx) {
        m_initialized = true;

        // Set the PROJ data directory to find proj.db
        std::string dataDir = findProjDataDir();
        if (!dataDir.empty()) {
            m_dataDir = dataDir;
            const char* dirCStr = dataDir.c_str();
            proj_context_set_search_paths(reinterpret_cast<PJ_CONTEXT*>(m_ctx), 1, &dirCStr);
        }
    } else {
        m_lastError = "Failed to create PROJ context";
    }
}

CRSManager::~CRSManager() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_ctx) {
        proj_context_destroy(reinterpret_cast<PJ_CONTEXT*>(m_ctx));
        m_ctx = nullptr;
    }
}

void CRSManager::setDataDirectory(const std::string& dir) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_dataDir = dir;
    if (m_ctx) {
        const char* dirCStr = dir.c_str();
        proj_context_set_search_paths(reinterpret_cast<PJ_CONTEXT*>(m_ctx), 1, &dirCStr);
    }
}

bool CRSManager::isInitialized() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_initialized && m_ctx != nullptr;
}

std::string CRSManager::lastError() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_lastError;
}

// ============================================================
// Helper: trim whitespace
// ============================================================
static std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Helper: check if string starts with a prefix (case-insensitive)
static bool startsWithCI(const std::string& s, const std::string& prefix) {
    if (s.length() < prefix.length()) return false;
    for (size_t i = 0; i < prefix.length(); ++i) {
        if (std::tolower(s[i]) != std::tolower(prefix[i])) return false;
    }
    return true;
}

// Helper: detect if a string looks like WKT
static bool looksLikeWKT(const std::string& s) {
    return startsWithCI(s, "GEOGCS") || startsWithCI(s, "PROJCS") ||
           startsWithCI(s, "COMPD_CS") || startsWithCI(s, "GEOCCS") ||
           startsWithCI(s, "VERT_CS") || startsWithCI(s, "LOCAL_CS") ||
           startsWithCI(s, "BOUNDCRS") || startsWithCI(s, "GEODCRS") ||
           startsWithCI(s, "PROJCRS") || startsWithCI(s, "COMPOUNDCRS") ||
           startsWithCI(s, "VERTCRS") || startsWithCI(s, "ENGCRS") ||
           startsWithCI(s, "GEOCENTRIC");
}

// Helper: detect if a string looks like JSON/PROJJSON
static bool looksLikeJSON(const std::string& s) {
    std::string t = trim(s);
    return !t.empty() && t[0] == '{';
}

// Helper: detect if a string looks like an authority:code
static bool looksLikeAuthId(const std::string& s) {
    auto colon = s.find(':');
    if (colon == std::string::npos || colon == 0) return false;
    // After colon should be digits (or alphanumeric for some authorities)
    for (size_t i = colon + 1; i < s.length(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(s[i])) &&
            s[i] != '-' && s[i] != '_') {
            // Allow some non-digit chars for non-EPSG authorities
            return std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == ':';
        }
    }
    return true;
}

// Helper: extract numeric code from "AUTHORITY:CODE"
static int extractCode(const std::string& authId) {
    auto colon = authId.find(':');
    if (colon == std::string::npos) return 0;
    std::string codeStr = authId.substr(colon + 1);
    try {
        return std::stoi(codeStr);
    } catch (...) {
        return 0;
    }
}

// Helper: extract authority from "AUTHORITY:CODE"
static std::string extractAuthority(const std::string& authId) {
    auto colon = authId.find(':');
    if (colon == std::string::npos) return "";
    return authId.substr(0, colon);
}

// ============================================================
// CRS kind detection from PROJ type
// ============================================================
static CRSKind kindFromPJType(PJ_TYPE type) {
    switch (type) {
        case PJ_TYPE_GEODETIC_CRS:
        case PJ_TYPE_GEOGRAPHIC_CRS:
        case PJ_TYPE_GEOGRAPHIC_2D_CRS:
        case PJ_TYPE_GEOGRAPHIC_3D_CRS:
            return CRSKind::Geographic;
        case PJ_TYPE_PROJECTED_CRS:
        case PJ_TYPE_DERIVED_PROJECTED_CRS:
            return CRSKind::Projected;
        case PJ_TYPE_GEOCENTRIC_CRS:       return CRSKind::Geocentric;
        case PJ_TYPE_VERTICAL_CRS:         return CRSKind::Vertical;
        case PJ_TYPE_COMPOUND_CRS:         return CRSKind::Compound;
        case PJ_TYPE_ENGINEERING_CRS:      return CRSKind::Engineering;
        case PJ_TYPE_BOUND_CRS:            return CRSKind::Bound;
        default:                             return CRSKind::Unknown;
    }
}

// ============================================================
// Unit detection from PROJ unit string
// ============================================================
static Unit unitFromString(const std::string& s) {
    std::string lower;
    lower.reserve(s.size());
    for (char c : s) lower.push_back(static_cast<char>(std::tolower(c)));

    if (lower.find("degree") != std::string::npos || lower.find("deg") != std::string::npos)
        return Unit::Degree;
    if (lower.find("radian") != std::string::npos)
        return Unit::Radian;
    if (lower.find("us survey foot") != std::string::npos ||
        lower.find("ussurveyfoot") != std::string::npos ||
        lower.find("us-foot") != std::string::npos)
        return Unit::USFoot;
    if (lower.find("foot") != std::string::npos || lower.find("feet") != std::string::npos)
        return Unit::Foot;
    if (lower.find("kilomet") != std::string::npos)
        return Unit::Kilometre;
    if (lower.find("nautical") != std::string::npos)
        return Unit::NauticalMile;
    if (lower.find("metre") != std::string::npos || lower.find("meter") != std::string::npos)
        return Unit::Metre;
    return Unit::Unknown;
}

// ============================================================
// Populate CRSDefinition from a PROJ PJ* object
// ============================================================
bool CRSManager::populateFromPJ(CRSDefinition& def, ProjObject* pj) {
    if (!pj) return false;

    PJ* pjObj = static_cast<PJ*>(pj);

    // Get type
    PJ_TYPE type = proj_get_type(pjObj);
    def.kind = kindFromPJType(type);

    // Get name
    const char* name = proj_get_name(pjObj);
    if (name) def.name = name;

    // Get authority/code
    const char* authName = proj_get_id_auth_name(pjObj, 0);
    const char* code = proj_get_id_code(pjObj, 0);
    if (authName) def.authority = authName;
    if (code) def.code = std::atoi(code);

    // Get WKT2:2019
    PJ_CONTEXT* ctx = reinterpret_cast<PJ_CONTEXT*>(m_ctx);
    const char* wkt2 = proj_as_wkt(ctx, pjObj, PJ_WKT2_2019, nullptr);
    if (wkt2) def.wkt2 = wkt2;

    // Get PROJJSON
    const char* projJson = proj_as_projjson(ctx, pjObj, nullptr);
    if (projJson) def.projJson = projJson;

    // Get PROJ string (legacy)
    const char* projStr = proj_as_proj_string(ctx, pjObj, PJ_PROJ_5, nullptr);
    if (projStr) def.projString = projStr;

    // Get deprecated flag
    def.deprecated = proj_is_deprecated(pjObj);

    // Get unit from coordinate system
    PJ* cs = proj_crs_get_coordinate_system(ctx, pjObj);
    if (cs) {
        int axisCount = proj_cs_get_axis_count(ctx, cs);
        if (axisCount > 0) {
            const char* unitName = nullptr;
            proj_cs_get_axis_info(ctx, cs, 0, nullptr, nullptr, nullptr,
                nullptr, &unitName, nullptr, nullptr);
            if (unitName) {
                def.unit = unitFromString(unitName);
            }
        }
        proj_destroy(cs);
    }

    // Get ellipsoid info
    PJ* ellipsoid = proj_get_ellipsoid(ctx, pjObj);
    if (ellipsoid) {
        double semiMajor = 0, semiMinor = 0, invFlat = 0;
        int isComputed = 0;
        if (proj_ellipsoid_get_parameters(ctx, ellipsoid,
            &semiMajor, &semiMinor, &isComputed, &invFlat)) {
            def.semiMajorAxis = semiMajor;
            def.inverseFlattening = invFlat;
        }
        const char* ellipName = proj_get_name(ellipsoid);
        if (ellipName) def.ellipsoid = ellipName;
        proj_destroy(ellipsoid);
    }

    // Area of use
    queryAreaOfUse(def, pj);

    return true;
}

// ============================================================
// Query area of use from PROJ
// ============================================================
bool CRSManager::queryAreaOfUse(CRSDefinition& def, ProjObject* pj) {
    if (!pj) return false;

    PJ* pjObj = static_cast<PJ*>(pj);
    PJ_CONTEXT* ctx = reinterpret_cast<PJ_CONTEXT*>(m_ctx);

    double west, south, east, north;
    const char* areaName = nullptr;

    if (proj_get_area_of_use(ctx, pjObj, &west, &south, &east, &north, &areaName)) {
        def.areaOfUse.west = west;
        def.areaOfUse.south = south;
        def.areaOfUse.east = east;
        def.areaOfUse.north = north;
        if (areaName) def.areaOfUseName = areaName;
        return true;
    }
    return false;
}

// ============================================================
// Query additional metadata from proj.db via SQLite
// ============================================================
bool CRSManager::queryMetadata(CRSDefinition& def) {
    if (!m_ctx) return false;

    // Use PROJ's API to get ellipsoid info if available
    // For now, we rely on populateFromPJ for the main metadata.
    // Ellipsoid/datum details can be queried from the PJ object
    // using proj_get_ellipsoid / proj_get_datum APIs if needed.
    return true;
}

// ============================================================
// Factory: from EPSG code
// ============================================================
std::optional<CRSDefinition> CRSManager::fromEPSG(int code) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_ctx) {
        m_lastError = "PROJ context not initialized";
        return std::nullopt;
    }

    PJ_CONTEXT* ctx = reinterpret_cast<PJ_CONTEXT*>(m_ctx);
    PJ* pj = proj_create_from_database(ctx, "EPSG",
        std::to_string(code).c_str(),
        PJ_CATEGORY_CRS, false, nullptr);

    if (!pj) {
        m_lastError = "EPSG:" + std::to_string(code) + " not found in proj.db";
        return std::nullopt;
    }

    CRSDefinition def;
    bool ok = populateFromPJ(def, reinterpret_cast<ProjObject*>(pj));
    proj_destroy(pj);

    if (!ok) {
        m_lastError = "Failed to populate CRSDefinition from EPSG:" + std::to_string(code);
        return std::nullopt;
    }

    return def;
}

// ============================================================
// Factory: from authority:code
// ============================================================
std::optional<CRSDefinition> CRSManager::fromAuthId(const std::string& authId) {
    std::string auth = extractAuthority(authId);
    int code = extractCode(authId);

    if (auth.empty() || code <= 0) {
        m_lastError = "Invalid authority:code format: " + authId;
        return std::nullopt;
    }

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_ctx) {
        m_lastError = "PROJ context not initialized";
        return std::nullopt;
    }

    PJ_CONTEXT* ctx = reinterpret_cast<PJ_CONTEXT*>(m_ctx);
    PJ* pj = proj_create_from_database(ctx, auth.c_str(),
        std::to_string(code).c_str(),
        PJ_CATEGORY_CRS, false, nullptr);

    if (!pj) {
        m_lastError = authId + " not found in proj.db";
        return std::nullopt;
    }

    CRSDefinition def;
    bool ok = populateFromPJ(def, reinterpret_cast<ProjObject*>(pj));
    proj_destroy(pj);

    if (!ok) {
        m_lastError = "Failed to populate CRSDefinition from " + authId;
        return std::nullopt;
    }

    return def;
}

// ============================================================
// Factory: from WKT2
// ============================================================
std::optional<CRSDefinition> CRSManager::fromWKT(const std::string& wkt) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_ctx) {
        m_lastError = "PROJ context not initialized";
        return std::nullopt;
    }

    PJ_CONTEXT* ctx = reinterpret_cast<PJ_CONTEXT*>(m_ctx);
    PJ* pj = proj_create(ctx, wkt.c_str());
    if (!pj) {
        m_lastError = "Failed to parse WKT string";
        return std::nullopt;
    }

    CRSDefinition def;
    bool ok = populateFromPJ(def, reinterpret_cast<ProjObject*>(pj));
    def.wkt2 = wkt;  // keep original WKT
    proj_destroy(pj);

    if (!ok) {
        m_lastError = "Failed to populate CRSDefinition from WKT";
        return std::nullopt;
    }

    return def;
}

// ============================================================
// Factory: from PROJJSON
// ============================================================
std::optional<CRSDefinition> CRSManager::fromPROJJSON(const std::string& json) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_ctx) {
        m_lastError = "PROJ context not initialized";
        return std::nullopt;
    }

    PJ_CONTEXT* ctx = reinterpret_cast<PJ_CONTEXT*>(m_ctx);
    PJ* pj = proj_create(ctx, json.c_str());
    if (!pj) {
        m_lastError = "Failed to parse PROJJSON string";
        return std::nullopt;
    }

    CRSDefinition def;
    bool ok = populateFromPJ(def, reinterpret_cast<ProjObject*>(pj));
    def.projJson = json;  // keep original JSON
    proj_destroy(pj);

    if (!ok) {
        m_lastError = "Failed to populate CRSDefinition from PROJJSON";
        return std::nullopt;
    }

    return def;
}

// ============================================================
// Factory: from any identifier
// ============================================================
std::optional<CRSDefinition> CRSManager::fromAny(const std::string& identifier) {
    std::string id = trim(identifier);
    if (id.empty()) {
        m_lastError = "Empty CRS identifier";
        return std::nullopt;
    }

    // Try authority:code first
    if (looksLikeAuthId(id)) {
        auto result = fromAuthId(id);
        if (result) return result;
    }

    // Try URN form: urn:ogc:def:crs:EPSG::4326
    if (startsWithCI(id, "urn:ogc:def:crs:") ||
        startsWithCI(id, "urn:ogc:def:coordinateOperation:")) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (!m_ctx) return std::nullopt;
        PJ_CONTEXT* ctx = reinterpret_cast<PJ_CONTEXT*>(m_ctx);
        PJ* pj = proj_create(ctx, id.c_str());
        if (pj) {
            CRSDefinition def;
            bool ok = populateFromPJ(def, reinterpret_cast<ProjObject*>(pj));
            proj_destroy(pj);
            if (ok) return def;
        }
    }

    // Try WKT
    if (looksLikeWKT(id)) {
        return fromWKT(id);
    }

    // Try JSON/PROJJSON
    if (looksLikeJSON(id)) {
        return fromPROJJSON(id);
    }

    // Try as a plain number (EPSG code)
    bool allDigits = true;
    for (char c : id) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            allDigits = false;
            break;
        }
    }
    if (allDigits) {
        try {
            int code = std::stoi(id);
            return fromEPSG(code);
        } catch (...) {}
    }

    // Last resort: try proj_create with the string
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (!m_ctx) return std::nullopt;
        PJ_CONTEXT* ctx = reinterpret_cast<PJ_CONTEXT*>(m_ctx);
        PJ* pj = proj_create(ctx, id.c_str());
        if (pj) {
            CRSDefinition def;
            bool ok = populateFromPJ(def, reinterpret_cast<ProjObject*>(pj));
            proj_destroy(pj);
            if (ok) return def;
        }
    }

    m_lastError = "Could not resolve CRS identifier: " + id;
    return std::nullopt;
}

// ============================================================
// Search the CRS database
// ============================================================
std::vector<CRSSearchResult> CRSManager::search(const CRSSearchFilter& filter) {
    std::vector<CRSSearchResult> results;

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_ctx) {
        m_lastError = "PROJ context not initialized";
        return results;
    }

    // Build the PROJ string for proj_create_from_name
    // PROJ's API: proj_create_from_name(ctx, name, types, type_count,
    //                                    category, allow_deprecated, crs_area_of_use,
    //                                    bbox, west_lon, south_lat, east_lon, north_lat)
    std::string query = filter.query;

    // Determine PJ_TYPE filter
    PJ_TYPE typeFilter = PJ_TYPE_UNKNOWN;
    switch (filter.kind) {
        case CRSKind::Geographic:  typeFilter = PJ_TYPE_GEOGRAPHIC_CRS; break;
        case CRSKind::Projected:   typeFilter = PJ_TYPE_PROJECTED_CRS; break;
        case CRSKind::Geocentric:  typeFilter = PJ_TYPE_GEOCENTRIC_CRS; break;
        case CRSKind::Vertical:    typeFilter = PJ_TYPE_VERTICAL_CRS; break;
        case CRSKind::Compound:    typeFilter = PJ_TYPE_COMPOUND_CRS; break;
        case CRSKind::Engineering: typeFilter = PJ_TYPE_ENGINEERING_CRS; break;
        default: break;
    }

    PJ_CONTEXT* ctx = reinterpret_cast<PJ_CONTEXT*>(m_ctx);

    // If query is a pure number, treat as EPSG code lookup
    bool isNumeric = !query.empty();
    for (char c : query) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            isNumeric = false;
            break;
        }
    }

    if (isNumeric) {
        // Direct EPSG lookup
        auto crs = fromEPSG(std::atoi(query.c_str()));
        if (crs) {
            CRSSearchResult r;
            r.crs = *crs;
            r.relevance = 100;
            results.push_back(r);
        }
        // Also search by code substring in the database
    }

    // Use proj_create_from_name for text search
    // This searches the CRS database by name
    PJ_OBJ_LIST* list = nullptr;

    // Determine authority for search (empty = all authorities)
    const char* authName = filter.authority.empty() ? nullptr : filter.authority.c_str();

    if (typeFilter != PJ_TYPE_UNKNOWN) {
        PJ_TYPE types[] = { typeFilter };
        list = proj_create_from_name(ctx, authName, query.c_str(),
            types, 1, true, filter.maxResults, nullptr);
    } else {
        // Search all CRS types
        PJ_TYPE types[] = {
            PJ_TYPE_GEOGRAPHIC_CRS,
            PJ_TYPE_PROJECTED_CRS,
            PJ_TYPE_GEOCENTRIC_CRS,
            PJ_TYPE_VERTICAL_CRS,
            PJ_TYPE_COMPOUND_CRS,
            PJ_TYPE_ENGINEERING_CRS,
        };
        list = proj_create_from_name(ctx, authName, query.c_str(),
            types, 6, true, filter.maxResults, nullptr);
    }

    if (list) {
        int count = proj_list_get_count(list);
        int maxResults = filter.maxResults;
        if (count > maxResults) count = maxResults;

        for (int i = 0; i < count; ++i) {
            PJ* pj = proj_list_get(ctx, list, i);
            if (!pj) continue;

            CRSDefinition def;
            if (populateFromPJ(def, reinterpret_cast<ProjObject*>(pj))) {
                // Apply authority filter
                if (!filter.authority.empty() &&
                    def.authority != filter.authority) {
                    proj_destroy(pj);
                    continue;
                }

                // Apply area filter
                if (!filter.area.empty()) {
                    std::string areaLower = filter.area;
                    std::string defAreaLower = def.areaOfUseName;
                    std::transform(areaLower.begin(), areaLower.end(),
                                   areaLower.begin(), ::tolower);
                    std::transform(defAreaLower.begin(), defAreaLower.end(),
                                   defAreaLower.begin(), ::tolower);
                    if (defAreaLower.find(areaLower) == std::string::npos) {
                        proj_destroy(pj);
                        continue;
                    }
                }

                // Apply bbox filter
                if (filter.useBboxFilter && def.areaOfUse.isValid()) {
                    // Check intersection
                    bool intersects =
                        def.areaOfUse.west <= filter.bbox.east &&
                        def.areaOfUse.east >= filter.bbox.west &&
                        def.areaOfUse.south <= filter.bbox.north &&
                        def.areaOfUse.north >= filter.bbox.south;
                    if (!intersects) {
                        proj_destroy(pj);
                        continue;
                    }
                }

                CRSSearchResult r;
                r.crs = def;
                r.relevance = 100 - i;  // earlier = more relevant
                results.push_back(r);
            }
            proj_destroy(pj);
        }
        proj_list_destroy(list);
    }

    return results;
}

// ============================================================
// Quick search convenience
// ============================================================
std::vector<CRSSearchResult> CRSManager::search(const std::string& query, int maxResults) {
    CRSSearchFilter filter;
    filter.query = query;
    filter.maxResults = maxResults;
    return search(filter);
}

// ============================================================
// Compound CRS creation
// ============================================================
std::optional<CRSDefinition> CRSManager::createCompound(
    const CRSDefinition& horizontal,
    const CRSDefinition& vertical) {

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_ctx) {
        m_lastError = "PROJ context not initialized";
        return std::nullopt;
    }

    // Build compound CRS using proj_create
    // Format: "COMPOUNDCRS[\"name\", <horizontal WKT>, <vertical WKT>]"
    // Or use the PROJ API for compound creation
    std::string compoundName = horizontal.name + " + " + vertical.name;

    // Use proj_create_from_database to get PJ objects, then combine
    PJ_CONTEXT* ctx = reinterpret_cast<PJ_CONTEXT*>(m_ctx);
    PJ* hPj = proj_create_from_database(ctx,
        horizontal.authority.c_str(),
        std::to_string(horizontal.code).c_str(),
        PJ_CATEGORY_CRS, false, nullptr);
    if (!hPj) {
        m_lastError = "Could not load horizontal CRS";
        return std::nullopt;
    }

    PJ* vPj = proj_create_from_database(ctx,
        vertical.authority.c_str(),
        std::to_string(vertical.code).c_str(),
        PJ_CATEGORY_CRS, false, nullptr);
    if (!vPj) {
        m_lastError = "Could not load vertical CRS";
        proj_destroy(hPj);
        return std::nullopt;
    }

    // Create compound CRS
    PJ* compound = proj_create_compound_crs(ctx,
        compoundName.c_str(), hPj, vPj);

    proj_destroy(hPj);
    proj_destroy(vPj);

    if (!compound) {
        m_lastError = "Failed to create compound CRS";
        return std::nullopt;
    }

    CRSDefinition def;
    bool ok = populateFromPJ(def, compound);
    def.hasVerticalComponent = true;
    def.verticalCrsName = vertical.name;
    def.verticalCrsCode = vertical.code;

    proj_destroy(compound);

    if (!ok) {
        m_lastError = "Failed to populate compound CRSDefinition";
        return std::nullopt;
    }

    return def;
}

// ============================================================
// Built-in CRS accessors
// ============================================================
CRSDefinition CRSManager::wgs84() {
    auto crs = fromEPSG(CRS::EPSG_WGS84);
    return crs.value_or(CRSDefinition{});
}

CRSDefinition CRSManager::webMercator() {
    auto crs = fromEPSG(CRS::EPSG_WEB_MERCATOR);
    return crs.value_or(CRSDefinition{});
}

std::optional<CRSDefinition> CRSManager::autoUtm(double lat, double lon) {
    int zone = static_cast<int>(std::floor((lon + 180.0) / 6.0)) + 1;
    bool north = lat >= 0.0;
    int epsg = north ? (CRS::EPSG_UTM_NORTH_BASE + zone)
                     : (CRS::EPSG_UTM_SOUTH_BASE + zone);
    return fromEPSG(epsg);
}

// ============================================================
// Known authorities
// ============================================================
std::vector<std::string> CRSManager::knownAuthorities() {
    std::vector<std::string> authorities = {
        "EPSG", "ESRI", "IGNF", "OGC", "NKG", "NRCAN", "NGA"
    };
    return authorities;
}

// ============================================================
// Check if CRS exists
// ============================================================
bool CRSManager::exists(const std::string& authId) {
    auto crs = fromAuthId(authId);
    return crs.has_value();
}

} // namespace gis
