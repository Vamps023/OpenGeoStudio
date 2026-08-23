// ============================================================
// test_crs_system.cpp — Unit tests for PROJ-backed CRS system
//
// Uses the same VERIFY macro pattern as other OpenGeoStudio tests.
// ============================================================

#include "CRS.hpp"
#include "CRSManager.hpp"
#include "CoordinateTransform.hpp"
#include "CRSSearch.hpp"

#include <cmath>
#include <QString>
#include <iostream>

using namespace gis;

static int g_testsPassed = 0;
static int g_testsFailed = 0;

#define VERIFY(cond, name, msg) do { \
    if (cond) { \
        g_testsPassed++; \
        std::cout << "[PASS] " << name << std::endl; \
    } else { \
        g_testsFailed++; \
        std::cerr << "[FAIL] " << name << ": " << msg << std::endl; \
    } \
} while (0)

static bool approxEqual(double a, double b, double eps = 0.0001) {
    return std::fabs(a - b) < eps;
}

// ============================================================
// CRSDefinition tests
// ============================================================
void testCRSDefinition_DefaultInvalid() {
    CRSDefinition def;
    VERIFY(!def.isValid(), "CRSDef: Default Invalid", "Should be invalid");
    VERIFY(def.authId() == "", "CRSDef: Empty AuthId", "Should be empty");
}

void testCRSDefinition_AuthId() {
    CRSDefinition def;
    def.authority = "EPSG";
    def.code = 4326;
    VERIFY(def.authId() == "EPSG:4326", "CRSDef: AuthId", "Wrong authId");
    VERIFY(def.isValid(), "CRSDef: Valid", "Should be valid");
}

void testCRSDefinition_KindChecks() {
    CRSDefinition geo;
    geo.kind = CRSKind::Geographic;
    VERIFY(geo.isGeographic(), "CRSDef: IsGeographic", "Should be geographic");
    VERIFY(!geo.isProjected(), "CRSDef: NotProjected", "Should not be projected");

    CRSDefinition proj;
    proj.kind = CRSKind::Projected;
    VERIFY(proj.isProjected(), "CRSDef: IsProjected", "Should be projected");
}

void testBoundingBox_Contains() {
    BoundingBox bb;
    bb.west = -10; bb.east = 10; bb.south = -5; bb.north = 5;
    VERIFY(bb.contains(0, 0), "BBox: Contains center", "Center should be contained");
    VERIFY(bb.contains(-10, -5), "BBox: Contains corner", "Corner should be contained");
    VERIFY(!bb.contains(11, 0), "BBox: Not contains outside", "Should not contain");
}

// ============================================================
// CRSManager tests
// ============================================================
void testCRSManager_Initialized() {
    VERIFY(CRSManager::instance().isInitialized(),
           "CRSMgr: Initialized", "PROJ context not initialized");
}

void testCRSManager_FromEPSG_4326() {
    auto crs = CRSManager::instance().fromEPSG(4326);
    VERIFY(crs.has_value(), "CRSMgr: EPSG:4326 exists", "Should exist");
    if (crs) {
        VERIFY(crs->authority == "EPSG", "CRSMgr: 4326 authority", "Wrong authority");
        VERIFY(crs->code == 4326, "CRSMgr: 4326 code", "Wrong code");
        VERIFY(crs->isGeographic(), "CRSMgr: 4326 geographic", "Should be geographic");
        VERIFY(!crs->name.empty(), "CRSMgr: 4326 name", "Name should not be empty");
        VERIFY(!crs->wkt2.empty(), "CRSMgr: 4326 WKT2", "WKT2 should not be empty");
    }
}

void testCRSManager_FromEPSG_3857() {
    auto crs = CRSManager::instance().fromEPSG(3857);
    VERIFY(crs.has_value(), "CRSMgr: EPSG:3857 exists", "Should exist");
    if (crs) {
        VERIFY(crs->isProjected(), "CRSMgr: 3857 projected", "Should be projected");
    }
}

void testCRSManager_FromEPSG_32643() {
    auto crs = CRSManager::instance().fromEPSG(32643);
    VERIFY(crs.has_value(), "CRSMgr: EPSG:32643 exists", "Should exist");
    if (crs) {
        VERIFY(crs->isProjected(), "CRSMgr: 32643 projected", "Should be projected");
        VERIFY(crs->unit == Unit::Metre, "CRSMgr: 32643 metre", "Should be metre");
    }
}

void testCRSManager_FromAuthId() {
    auto crs = CRSManager::instance().fromAuthId("EPSG:4326");
    VERIFY(crs.has_value(), "CRSMgr: AuthId EPSG:4326", "Should exist");
    if (crs) {
        VERIFY(crs->code == 4326, "CRSMgr: AuthId code", "Wrong code");
    }
}

void testCRSManager_FromInvalidEPSG() {
    auto crs = CRSManager::instance().fromEPSG(999999);
    VERIFY(!crs.has_value(), "CRSMgr: Invalid EPSG", "Should not exist");
}

void testCRSManager_FromAny_Numeric() {
    auto crs = CRSManager::instance().fromAny("4326");
    VERIFY(crs.has_value(), "CRSMgr: FromAny numeric", "Should resolve");
    if (crs) VERIFY(crs->code == 4326, "CRSMgr: FromAny numeric code", "Wrong code");
}

void testCRSManager_FromAny_AuthId() {
    auto crs = CRSManager::instance().fromAny("EPSG:32643");
    VERIFY(crs.has_value(), "CRSMgr: FromAny authId", "Should resolve");
    if (crs) VERIFY(crs->code == 32643, "CRSMgr: FromAny authId code", "Wrong code");
}

void testCRSManager_FromAny_WKT() {
    auto original = CRSManager::instance().fromEPSG(4326);
    VERIFY(original.has_value(), "CRSMgr: FromAny WKT original", "Original should exist");
    if (!original) return;

    auto parsed = CRSManager::instance().fromAny(original->wkt2);
    VERIFY(parsed.has_value(), "CRSMgr: FromAny WKT parse", "Should parse WKT");
    if (parsed) {
        VERIFY(parsed->isGeographic(), "CRSMgr: FromAny WKT geographic", "Should be geographic");
    }
}

void testCRSManager_AutoUtm() {
    auto utm = CRSManager::instance().autoUtm(19.0, 72.8);
    VERIFY(utm.has_value(), "CRSMgr: AutoUtm Mumbai", "Should resolve");
    if (utm) {
        VERIFY(utm->code == 32643, "CRSMgr: AutoUtm Mumbai code", "Should be 32643");
    }

    auto utmS = CRSManager::instance().autoUtm(-33.8, 151.2);
    VERIFY(utmS.has_value(), "CRSMgr: AutoUtm Sydney", "Should resolve");
    if (utmS) {
        VERIFY(utmS->code == 32756, "CRSMgr: AutoUtm Sydney code", "Should be 32756");
    }
}

void testCRSManager_WGS84() {
    CRSDefinition wgs84 = CRSManager::instance().wgs84();
    VERIFY(wgs84.isValid(), "CRSMgr: WGS84", "Should be valid");
    VERIFY(wgs84.code == 4326, "CRSMgr: WGS84 code", "Wrong code");
}

void testCRSManager_Exists() {
    VERIFY(CRSManager::instance().exists("EPSG:4326"), "CRSMgr: Exists 4326", "Should exist");
    VERIFY(!CRSManager::instance().exists("EPSG:999999"), "CRSMgr: Not exists 999999", "Should not exist");
}

// ============================================================
// Search tests
// ============================================================
void testSearch_ByCode() {
    auto results = CRSManager::instance().search("4326", 10);
    VERIFY(!results.empty(), "Search: By code 4326", "Should have results");
    if (!results.empty()) {
        VERIFY(results[0].crs.code == 4326, "Search: By code first result", "First should be 4326");
    }
}

void testSearch_ByName() {
    auto results = CRSManager::instance().search("WGS 84", 20);
    VERIFY(!results.empty(), "Search: By name WGS 84", "Should have results");
    bool foundWgs84 = false;
    for (const auto& r : results) {
        if (r.crs.code == 4326) { foundWgs84 = true; break; }
    }
    VERIFY(foundWgs84, "Search: By name finds 4326", "Should find EPSG:4326");
}

void testSearch_ByUTMName() {
    // PROJ's name search is exact-ish; try the full CRS name
    auto results = CRSManager::instance().search("WGS 84 / UTM zone 43N", 20);
    VERIFY(!results.empty(), "Search: UTM zone 43N", "Should have results");
    bool found32643 = false;
    for (const auto& r : results) {
        if (r.crs.code == 32643) { found32643 = true; break; }
    }
    VERIFY(found32643, "Search: UTM zone 43N finds 32643", "Should find EPSG:32643");
}

void testSearch_WithAuthorityFilter() {
    CRSSearchFilter filter;
    filter.query = "WGS 84";
    filter.authority = "EPSG";
    filter.maxResults = 50;
    auto results = CRSManager::instance().search(filter);
    VERIFY(!results.empty(), "Search: Authority filter", "Should have results");
    for (const auto& r : results) {
        if (r.crs.authority != "EPSG") {
            VERIFY(false, "Search: Authority filter all EPSG", "Non-EPSG result found");
            break;
        }
    }
}

void testSearch_WithKindFilter() {
    CRSSearchFilter filter;
    filter.query = "WGS 84";
    filter.kind = CRSKind::Geographic;
    filter.maxResults = 50;
    auto results = CRSManager::instance().search(filter);
    VERIFY(!results.empty(), "Search: Kind filter", "Should have results");
    for (const auto& r : results) {
        if (!r.crs.isGeographic()) {
            VERIFY(false, "Search: Kind filter all geographic", "Non-geographic result found");
            break;
        }
    }
}

// ============================================================
// Transform tests
// ============================================================
void testTransform_WGS84_ShortCircuit() {
    auto wgs84 = CRSManager::instance().fromEPSG(4326);
    if (!wgs84) { VERIFY(false, "Transform: Setup WGS84", "No WGS84"); return; }

    CoordinateTransform t(*wgs84, *wgs84);
    VERIFY(t.isValid(), "Transform: Short-circuit valid", "Should be valid");
    VERIFY(t.isShortCircuited(), "Transform: Short-circuit", "Should short-circuit");

    auto result = t.transform(10.0, 20.0);
    VERIFY(result.success, "Transform: Short-circuit success", "Should succeed");
    if (result.success) {
        VERIFY(approxEqual(result.point.x, 10.0), "Transform: Short-circuit x", "Wrong x");
        VERIFY(approxEqual(result.point.y, 20.0), "Transform: Short-circuit y", "Wrong y");
    }
}

void testTransform_WGS84_To_WebMercator() {
    auto wgs84 = CRSManager::instance().fromEPSG(4326);
    auto webMerc = CRSManager::instance().fromEPSG(3857);
    if (!wgs84 || !webMerc) { VERIFY(false, "Transform: Setup", "No CRS"); return; }

    CoordinateTransform t(*wgs84, *webMerc);
    VERIFY(t.isValid(), "Transform: WGS84→WebMerc valid", "Should be valid");

    auto result = t.transform(0.0, 0.0);
    VERIFY(result.success, "Transform: WGS84→WebMerc origin", "Should succeed");
    if (result.success) {
        VERIFY(approxEqual(result.point.x, 0.0, 1.0), "Transform: WGS84→WebMerc x", "Wrong x");
        VERIFY(approxEqual(result.point.y, 0.0, 1.0), "Transform: WGS84→WebMerc y", "Wrong y");
    }
}

void testTransform_WGS84_To_UTM43N() {
    auto wgs84 = CRSManager::instance().fromEPSG(4326);
    auto utm43 = CRSManager::instance().fromEPSG(32643);
    if (!wgs84 || !utm43) { VERIFY(false, "Transform: Setup UTM", "No CRS"); return; }

    CoordinateTransform t(*wgs84, *utm43);
    VERIFY(t.isValid(), "Transform: WGS84→UTM43 valid", "Should be valid");

    auto result = t.transform(72.8, 19.0);
    VERIFY(result.success, "Transform: WGS84→UTM43 Mumbai", "Should succeed");
    if (result.success) {
        VERIFY(result.point.x > 100000.0, "Transform: UTM43 easting range", "Easting out of range");
        VERIFY(result.point.x < 900000.0, "Transform: UTM43 easting range2", "Easting out of range");
        VERIFY(result.point.y > 0.0, "Transform: UTM43 northing positive", "Northing should be positive");
    }
}

void testTransform_UTM43N_To_WGS84() {
    auto wgs84 = CRSManager::instance().fromEPSG(4326);
    auto utm43 = CRSManager::instance().fromEPSG(32643);
    if (!wgs84 || !utm43) { VERIFY(false, "Transform: Setup UTM reverse", "No CRS"); return; }

    CoordinateTransform t(*utm43, *wgs84);
    VERIFY(t.isValid(), "Transform: UTM43→WGS84 valid", "Should be valid");

    auto result = t.transform(300000.0, 2000000.0);
    VERIFY(result.success, "Transform: UTM43→WGS84", "Should succeed");
    if (result.success) {
        VERIFY(result.point.x > 72.0, "Transform: UTM43→WGS84 lon range", "Lon out of range");
        VERIFY(result.point.x < 78.0, "Transform: UTM43→WGS84 lon range2", "Lon out of range");
        VERIFY(result.point.y > 0.0, "Transform: UTM43→WGS84 lat positive", "Lat should be positive");
    }
}

void testTransform_RoundTrip() {
    auto wgs84 = CRSManager::instance().fromEPSG(4326);
    auto utm43 = CRSManager::instance().fromEPSG(32643);
    if (!wgs84 || !utm43) { VERIFY(false, "Transform: Setup roundtrip", "No CRS"); return; }

    CoordinateTransform forward(*wgs84, *utm43);
    CoordinateTransform backward(*utm43, *wgs84);

    double lon = 72.8, lat = 19.0;
    auto fwd = forward.transform(lon, lat);
    VERIFY(fwd.success, "Transform: Roundtrip forward", "Forward failed");
    if (!fwd.success) return;

    auto bwd = backward.transform(fwd.point.x, fwd.point.y);
    VERIFY(bwd.success, "Transform: Roundtrip backward", "Backward failed");
    if (!bwd.success) return;

    VERIFY(approxEqual(bwd.point.x, lon, 0.0001), "Transform: Roundtrip lon", "Lon mismatch");
    VERIFY(approxEqual(bwd.point.y, lat, 0.0001), "Transform: Roundtrip lat", "Lat mismatch");
}

void testTransform_Batch() {
    auto wgs84 = CRSManager::instance().fromEPSG(4326);
    auto webMerc = CRSManager::instance().fromEPSG(3857);
    if (!wgs84 || !webMerc) { VERIFY(false, "Transform: Setup batch", "No CRS"); return; }

    CoordinateTransform t(*wgs84, *webMerc);
    std::vector<GeoPoint> points = {{0,0}, {10,10}, {-10,-10}, {170,0}};
    auto results = t.transformBatch(points);
    VERIFY(results.size() == points.size(), "Transform: Batch size", "Size mismatch");
    for (const auto& r : results) {
        if (!r.success) {
            VERIFY(false, "Transform: Batch all succeed", "One failed");
            return;
        }
    }
    VERIFY(true, "Transform: Batch all succeed", "All succeeded");
}

void testTransform_Bounds() {
    auto wgs84 = CRSManager::instance().fromEPSG(4326);
    auto utm43 = CRSManager::instance().fromEPSG(32643);
    if (!wgs84 || !utm43) { VERIFY(false, "Transform: Setup bounds", "No CRS"); return; }

    CoordinateTransform t(*wgs84, *utm43);
    BoundingBox bb;
    bb.west = 72.0; bb.east = 78.0; bb.south = 18.0; bb.north = 22.0;

    auto result = t.transformBounds(bb, 10);
    VERIFY(result.isValid(), "Transform: Bounds valid", "Should be valid");
    VERIFY(result.west > 10000.0, "Transform: Bounds west", "West out of range");
    VERIFY(result.east < 900000.0, "Transform: Bounds east", "East out of range");
}

void testTransform_WithAreaOfInterest() {
    auto wgs84 = CRSManager::instance().fromEPSG(4326);
    auto utm43 = CRSManager::instance().fromEPSG(32643);
    if (!wgs84 || !utm43) { VERIFY(false, "Transform: Setup AOI", "No CRS"); return; }

    BoundingBox aoi;
    aoi.west = 72.0; aoi.east = 78.0; aoi.south = 18.0; aoi.north = 22.0;

    CoordinateTransform t(*wgs84, *utm43, aoi);
    VERIFY(t.isValid(), "Transform: AOI valid", "Should be valid with AOI");

    auto result = t.transform(75.0, 20.0);
    VERIFY(result.success, "Transform: AOI success", "Should succeed");
}

void testTransform_FreeFunction() {
    auto wgs84 = CRSManager::instance().fromEPSG(4326);
    auto webMerc = CRSManager::instance().fromEPSG(3857);
    if (!wgs84 || !webMerc) { VERIFY(false, "Transform: Setup free fn", "No CRS"); return; }

    auto result = transformPoint(*wgs84, *webMerc, GeoPoint(0.0, 0.0));
    VERIFY(result.success, "Transform: Free function success", "Should succeed");
    if (result.success) {
        VERIFY(approxEqual(result.point.x, 0.0, 1.0), "Transform: Free function x", "Wrong x");
    }
}

// ============================================================
// Metadata tests
// ============================================================
void testWGS84_HasAreaOfUse() {
    auto wgs84 = CRSManager::instance().fromEPSG(4326);
    if (!wgs84) { VERIFY(false, "Metadata: WGS84 area setup", "No WGS84"); return; }
    VERIFY(wgs84->areaOfUse.isValid(), "Metadata: WGS84 area valid", "Area should be valid");
    VERIFY(!wgs84->areaOfUseName.empty(), "Metadata: WGS84 area name", "Area name should not be empty");
}

void testUTM43_HasAreaOfUse() {
    auto utm43 = CRSManager::instance().fromEPSG(32643);
    if (!utm43) { VERIFY(false, "Metadata: UTM43 area setup", "No UTM43"); return; }
    VERIFY(utm43->areaOfUse.isValid(), "Metadata: UTM43 area valid", "Area should be valid");
    VERIFY(approxEqual(utm43->areaOfUse.west, 72.0, 1.0), "Metadata: UTM43 west", "Wrong west");
    VERIFY(approxEqual(utm43->areaOfUse.east, 78.0, 1.0), "Metadata: UTM43 east", "Wrong east");
}

void testWGS84_HasWKT2() {
    auto wgs84 = CRSManager::instance().fromEPSG(4326);
    if (!wgs84) { VERIFY(false, "Metadata: WKT2 setup", "No WGS84"); return; }
    VERIFY(!wgs84->wkt2.empty(), "Metadata: WKT2 not empty", "WKT2 should not be empty");
    VERIFY(wgs84->wkt2.find("GEOGCRS") != std::string::npos ||
           wgs84->wkt2.find("GEOGCS") != std::string::npos,
           "Metadata: WKT2 contains GEOGCRS", "WKT2 should contain GEOGCRS");
}

void testWGS84_HasPROJJSON() {
    auto wgs84 = CRSManager::instance().fromEPSG(4326);
    if (!wgs84) { VERIFY(false, "Metadata: PROJJSON setup", "No WGS84"); return; }
    VERIFY(!wgs84->projJson.empty(), "Metadata: PROJJSON not empty", "PROJJSON should not be empty");
    VERIFY(wgs84->projJson[0] == '{', "Metadata: PROJJSON starts with {", "Should start with {");
}

// ============================================================
// Extended CRS Discovery Tests
// ============================================================

// EPSG:32615 — WGS 84 / UTM zone 15N (the CRS that was missing)
void testEPSG_32615_Lookup() {
    auto crs = CRSManager::instance().fromEPSG(32615);
    VERIFY(crs.has_value(), "EPSG:32615 exists", "Should exist in PROJ database");
    if (crs) {
        VERIFY(crs->code == 32615, "EPSG:32615 code", "Wrong code");
        VERIFY(crs->authority == "EPSG", "EPSG:32615 authority", "Wrong authority");
        VERIFY(crs->isProjected(), "EPSG:32615 projected", "Should be projected");
        VERIFY(crs->unit == Unit::Metre, "EPSG:32615 metre", "Should be metre");
        VERIFY(crs->name.find("UTM") != std::string::npos,
               "EPSG:32615 name contains UTM", "Name should contain UTM");
        VERIFY(crs->name.find("15") != std::string::npos,
               "EPSG:32615 name contains 15", "Name should contain 15");
    }
}

// Multiple UTM zones — all 60 northern + key southern zones
void testMultipleUTMZones() {
    // Test all northern hemisphere zones 1-60
    for (int zone = 1; zone <= 60; ++zone) {
        int epsg = 32600 + zone;
        auto crs = CRSManager::instance().fromEPSG(epsg);
        if (!crs) {
            VERIFY(false, ("EPSG:" + std::to_string(epsg) + " exists").c_str(),
                   "Should exist in PROJ database");
            continue;
        }
        VERIFY(crs->isProjected(), ("EPSG:" + std::to_string(epsg) + " projected").c_str(),
               "Should be projected");
    }
    std::cout << "  [INFO] All 60 northern UTM zones (32601-32660) verified" << std::endl;
}

// Southern hemisphere UTM zones
void testSouthernHemisphereUTM() {
    for (int zone : {15, 18, 30, 43, 60}) {
        int epsg = 32700 + zone;
        auto crs = CRSManager::instance().fromEPSG(epsg);
        VERIFY(crs.has_value(), ("EPSG:" + std::to_string(epsg) + " exists").c_str(),
               "Should exist in PROJ database");
        if (crs) {
            VERIFY(crs->isProjected(), ("EPSG:" + std::to_string(epsg) + " projected").c_str(),
                   "Should be projected");
        }
    }
    std::cout << "  [INFO] Southern hemisphere UTM zones (32715, 32718, 32730, 32743, 32760) verified" << std::endl;
}

// Non-UTM CRS — geographic, Web Mercator, national grids
void testNonUTM_CRS() {
    struct CRSCheck { int epsg; const char* name; CRSKind kind; };
    CRSCheck checks[] = {
        {4326,  "WGS 84",                CRSKind::Geographic},
        {3857,  "Pseudo-Mercator",       CRSKind::Projected},
        {3035,  "ETRS89-extended",       CRSKind::Projected},
        {25832, "ETRS89 / UTM zone 32N", CRSKind::Projected},
        {3413,  "WGS 84 / NSIDC",        CRSKind::Projected},
    };

    for (const auto& c : checks) {
        auto crs = CRSManager::instance().fromEPSG(c.epsg);
        VERIFY(crs.has_value(), ("EPSG:" + std::to_string(c.epsg) + " exists").c_str(),
               "Should exist in PROJ database");
        if (crs) {
            VERIFY(crs->kind == c.kind,
                   ("EPSG:" + std::to_string(c.epsg) + " kind matches").c_str(),
                   "Wrong CRS kind");
        }
    }
}

// Search for EPSG:32615 by numeric code
void testSearch_EPSG32615_ByCode() {
    auto results = CRSManager::instance().search("32615", 10);
    VERIFY(!results.empty(), "Search: 32615 by code", "Should have results");
    bool found32615 = false;
    for (const auto& r : results) {
        if (r.crs.code == 32615) { found32615 = true; break; }
    }
    VERIFY(found32615, "Search: 32615 by code finds it", "Should find EPSG:32615");
}

// Search for EPSG:32615 by full name
void testSearch_EPSG32615_ByName() {
    auto results = CRSManager::instance().search("WGS 84 / UTM zone 15N", 20);
    VERIFY(!results.empty(), "Search: UTM zone 15N by name", "Should have results");
    bool found32615 = false;
    for (const auto& r : results) {
        if (r.crs.code == 32615) { found32615 = true; break; }
    }
    VERIFY(found32615, "Search: UTM zone 15N finds 32615", "Should find EPSG:32615");
}

// Search for EPSG:32615 by authority:code identifier
void testSearch_EPSG32615_ByAuthId() {
    auto crs = CRSManager::instance().fromAny("EPSG:32615");
    VERIFY(crs.has_value(), "Search: EPSG:32615 by authId", "Should resolve");
    if (crs) {
        VERIFY(crs->code == 32615, "Search: EPSG:32615 authId code", "Wrong code");
    }
}

// Auto UTM for multiple geographic positions
void testAutoUtm_MultipleZones() {
    struct PosTest { double lat; double lon; int expectedEpsg; const char* name; };
    PosTest tests[] = {
        // Northern hemisphere
        {19.0,  72.8,  32643, "Mumbai (UTM 43N)"},
        {40.0,  -93.0, 32615, "USA Central (UTM 15N)"},
        {40.0,  -87.0, 32616, "USA Chicago (UTM 16N)"},
        {34.0,  -118.0,32611, "Los Angeles (UTM 11N)"},
        {51.5,  -0.1,  32630, "London (UTM 30N)"},
        {35.7,  139.7, 32654, "Tokyo (UTM 54N)"},
        // Southern hemisphere
        {-33.8, 151.2, 32756, "Sydney (UTM 56S)"},
        {-23.5, -46.6, 32723, "Sao Paulo (UTM 23S)"},
    };

    for (const auto& t : tests) {
        auto utm = CRSManager::instance().autoUtm(t.lat, t.lon);
        VERIFY(utm.has_value(), ("AutoUtm: " + std::string(t.name)).c_str(),
               "Should resolve");
        if (utm) {
            VERIFY(utm->code == t.expectedEpsg,
                   ("AutoUtm: " + std::string(t.name) + " code").c_str(),
                   ("Expected " + std::to_string(t.expectedEpsg) +
                    " but got " + std::to_string(utm->code)).c_str());
        }
    }
}

// ============================================================
// Main
// ============================================================
int main(int argc, char* argv[]) {
    std::cout << "=== CRS System Tests ===" << std::endl;

    testCRSDefinition_DefaultInvalid();
    testCRSDefinition_AuthId();
    testCRSDefinition_KindChecks();
    testBoundingBox_Contains();

    testCRSManager_Initialized();
    testCRSManager_FromEPSG_4326();
    testCRSManager_FromEPSG_3857();
    testCRSManager_FromEPSG_32643();
    testCRSManager_FromAuthId();
    testCRSManager_FromInvalidEPSG();
    testCRSManager_FromAny_Numeric();
    testCRSManager_FromAny_AuthId();
    testCRSManager_FromAny_WKT();
    testCRSManager_AutoUtm();
    testCRSManager_WGS84();
    testCRSManager_Exists();

    std::cout << "\n--- Search Tests ---" << std::endl;
    testSearch_ByCode();
    testSearch_ByName();
    testSearch_ByUTMName();
    testSearch_WithAuthorityFilter();
    testSearch_WithKindFilter();

    std::cout << "\n--- Transform Tests ---" << std::endl;
    testTransform_WGS84_ShortCircuit();
    testTransform_WGS84_To_WebMercator();
    testTransform_WGS84_To_UTM43N();
    testTransform_UTM43N_To_WGS84();
    testTransform_RoundTrip();
    testTransform_Batch();
    testTransform_Bounds();
    testTransform_WithAreaOfInterest();
    testTransform_FreeFunction();

    std::cout << "\n--- Metadata Tests ---" << std::endl;
    testWGS84_HasAreaOfUse();
    testUTM43_HasAreaOfUse();
    testWGS84_HasWKT2();
    testWGS84_HasPROJJSON();

    std::cout << "\n--- Extended CRS Discovery Tests ---" << std::endl;
    testEPSG_32615_Lookup();
    testMultipleUTMZones();
    testSouthernHemisphereUTM();
    testNonUTM_CRS();
    testSearch_EPSG32615_ByCode();
    testSearch_EPSG32615_ByName();
    testSearch_EPSG32615_ByAuthId();
    testAutoUtm_MultipleZones();

    std::cout << "\n=== Results ===" << std::endl;
    std::cout << "Passed: " << g_testsPassed << std::endl;
    std::cout << "Failed: " << g_testsFailed << std::endl;

    return g_testsFailed == 0 ? 0 : 1;
}
