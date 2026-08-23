#pragma once

// ============================================================
// CRSManager.hpp — PROJ-backed CRS database and factory
//
// Uses PROJ's proj.db for:
//   - Looking up CRS definitions by EPSG code / authority:code
//   - Parsing WKT2 / PROJJSON strings into CRSDefinition
//   - Searching the CRS database by name or code
//   - Creating coordinate transformations with area-of-use
//
// PROJ reference: https://proj.org/en/stable/development/
// ============================================================

#include "CRS.hpp"
#include <vector>
#include <string>
#include <memory>
#include <mutex>

// PROJ opaque types (avoid pulling proj.h into every TU)
// PJ_CONTEXT and PJ are typedef'd in proj.h as opaque structs.
// We use void* internally and cast in the .cpp files.
using ProjContext = void;
using ProjObject = void;
using ProjObjList = void;

namespace gis {

// Search filter for CRS database queries
struct CRSSearchFilter {
    std::string query;              // free text: "32643", "UTM 43N", "WGS 84"

    // Authority filter (empty = all)
    std::string authority;          // "EPSG", "ESRI", "IGNF", ...

    // Kind filter (Unknown = all kinds)
    CRSKind kind = CRSKind::Unknown;

    // Area filter (empty = all)
    std::string area;               // "India", "Europe", "World"

    // Bounding box filter (optional — only return CRS whose area of use intersects)
    bool useBboxFilter = false;
    BoundingBox bbox;

    // Result limit
    int maxResults = 100;
};

// A single search result
struct CRSSearchResult {
    CRSDefinition crs;
    int relevance = 0;              // higher = more relevant
};

// ============================================================
// CRSManager — singleton, PROJ-backed
// ============================================================
class CRSManager {
public:
    // Singleton access
    static CRSManager& instance();

    // Non-copyable
    CRSManager(const CRSManager&) = delete;
    CRSManager& operator=(const CRSManager&) = delete;

    ~CRSManager();

    // --------------------------------------------------------
    // Initialization
    // --------------------------------------------------------

    // Set the PROJ data directory (where proj.db lives).
    // If not called, PROJ uses its compiled-in default.
    void setDataDirectory(const std::string& dir);

    // Get the PROJ data directory currently in use (may be empty if
    // the compiled-in default is used).
    std::string dataDirectory() const { return m_dataDir; }

    // Check if PROJ context is valid and proj.db is accessible
    bool isInitialized() const;

    // Get the last error message from PROJ
    std::string lastError() const;

    // --------------------------------------------------------
    // CRS lookup (factory methods)
    // --------------------------------------------------------

    // From EPSG code (e.g. 4326, 32643)
    std::optional<CRSDefinition> fromEPSG(int code);

    // From authority:code string (e.g. "EPSG:4326", "ESRI:54004")
    std::optional<CRSDefinition> fromAuthId(const std::string& authId);

    // From WKT2 string
    std::optional<CRSDefinition> fromWKT(const std::string& wkt);

    // From PROJJSON string
    std::optional<CRSDefinition> fromPROJJSON(const std::string& json);

    // From any PROJ-compatible identifier:
    //   "EPSG:4326", "urn:ogc:def:crs:EPSG::4326",
    //   a WKT string, a PROJJSON string, or a PROJ string
    std::optional<CRSDefinition> fromAny(const std::string& identifier);

    // --------------------------------------------------------
    // CRS search
    // --------------------------------------------------------

    // Search the CRS database with a filter
    std::vector<CRSSearchResult> search(const CRSSearchFilter& filter);

    // Quick search by text query (convenience)
    std::vector<CRSSearchResult> search(const std::string& query, int maxResults = 50);

    // --------------------------------------------------------
    // Compound CRS
    // --------------------------------------------------------

    // Create a compound CRS from horizontal + vertical
    // e.g. EPSG:32643 (UTM 43N) + EPSG:5773 (EGM96 height)
    std::optional<CRSDefinition> createCompound(
        const CRSDefinition& horizontal,
        const CRSDefinition& vertical);

    // --------------------------------------------------------
    // Built-in CRS accessors
    // --------------------------------------------------------

    CRSDefinition wgs84();
    CRSDefinition webMercator();

    // Auto-detect best UTM zone from lat/lon
    std::optional<CRSDefinition> autoUtm(double lat, double lon);

    // --------------------------------------------------------
    // Metadata queries
    // --------------------------------------------------------

    // Get list of known authorities in proj.db
    std::vector<std::string> knownAuthorities();

    // Check if a CRS exists in the database
    bool exists(const std::string& authId);

private:
    CRSManager();

    // Internal: populate CRSDefinition from a PROJ PJ* object
    bool populateFromPJ(CRSDefinition& def, ProjObject* pj);

    // Internal: query proj.db for CRS metadata
    bool queryMetadata(CRSDefinition& def);

    // Internal: get area of use from PROJ
    bool queryAreaOfUse(CRSDefinition& def, ProjObject* pj);

    // PROJ context (thread-safe via mutex)
    ProjContext* m_ctx = nullptr;
    mutable std::recursive_mutex m_mutex;
    std::string m_dataDir;
    mutable std::string m_lastError;
    bool m_initialized = false;
};

} // namespace gis
