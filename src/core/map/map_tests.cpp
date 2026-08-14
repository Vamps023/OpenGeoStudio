// ============================================================
// Map Subsystem Tests — QGIS-inspired coordinate/tile math
// ============================================================

#include "doctest.h"
#include "MapSubsystem.hpp"
#include <cmath>

using namespace map;

TEST_SUITE("MapRectangle") {
    TEST_CASE("construction and basic properties") {
        MapRectangle r(0, 0, 10, 20);
        CHECK(r.width() == 10);
        CHECK(r.height() == 20);
        CHECK(r.centerX() == 5);
        CHECK(r.centerY() == 10);
        CHECK(r.isValid());
        CHECK(!r.isNull());
        CHECK(!r.isEmpty());
    }

    TEST_CASE("normalize") {
        MapRectangle r(10, 20, 0, 0);
        r.normalize();
        CHECK(r.xMin == 0);
        CHECK(r.xMax == 10);
        CHECK(r.yMin == 0);
        CHECK(r.yMax == 20);
    }

    TEST_CASE("contains") {
        MapRectangle r(0, 0, 10, 10);
        CHECK(r.contains(5, 5));
        CHECK(r.contains(0, 0));
        CHECK(r.contains(10, 10));
        CHECK(!r.contains(-1, 5));
        CHECK(!r.contains(11, 5));
    }

    TEST_CASE("intersects") {
        MapRectangle a(0, 0, 10, 10);
        MapRectangle b(5, 5, 15, 15);
        MapRectangle c(20, 20, 30, 30);
        CHECK(a.intersects(b));
        CHECK(!a.intersects(c));
    }

    TEST_CASE("scale") {
        MapRectangle r(0, 0, 10, 10);
        r.scale(2.0);  // zoom out 2x
        CHECK(r.width() == doctest::Approx(20.0));
        CHECK(r.height() == doctest::Approx(20.0));
        CHECK(r.centerX() == doctest::Approx(5.0));
        CHECK(r.centerY() == doctest::Approx(5.0));
    }

    TEST_CASE("combineWith") {
        MapRectangle a(0, 0, 5, 5);
        MapRectangle b(10, 10, 15, 15);
        a.combineWith(b);
        CHECK(a.xMin == 0);
        CHECK(a.yMin == 0);
        CHECK(a.xMax == 15);
        CHECK(a.yMax == 15);
    }
}

TEST_SUITE("CoordinateReferenceSystem") {
    TEST_CASE("fromEpsg 4326") {
        auto crs = CoordinateReferenceSystem::fromEpsg(4326);
        CHECK(crs.isValid());
        CHECK(crs.isGeographic());
        CHECK(crs.units() == Units::Degrees);
        CHECK(crs.authId() == "EPSG:4326");
    }

    TEST_CASE("fromEpsg 3857") {
        auto crs = CoordinateReferenceSystem::fromEpsg(3857);
        CHECK(crs.isValid());
        CHECK(!crs.isGeographic());
        CHECK(crs.units() == Units::Meters);
        CHECK(crs.authId() == "EPSG:3857");
    }

    TEST_CASE("invalid EPSG") {
        auto crs = CoordinateReferenceSystem::fromEpsg(9999);
        CHECK(!crs.isValid());
    }

    TEST_CASE("fromAuthId string") {
        auto crs = CoordinateReferenceSystem::fromAuthId("EPSG:3857");
        CHECK(crs.isValid());
        CHECK(crs.id() == CRSId::EPSG_3857);
    }

    TEST_CASE("equality") {
        auto a = CoordinateReferenceSystem::fromEpsg(4326);
        auto b = CoordinateReferenceSystem::fromEpsg(4326);
        auto c = CoordinateReferenceSystem::fromEpsg(3857);
        CHECK(a == b);
        CHECK(!(a == c));
    }
}

TEST_SUITE("CoordinateTransform") {
    TEST_CASE("4326 to 3857 — origin") {
        CoordinateTransform t(CRSId::EPSG_4326, CRSId::EPSG_3857);
        auto p = t.transform(MapPoint(0, 0));  // lon=0, lat=0
        CHECK(p.x == doctest::Approx(0.0).epsilon(0.01));
        CHECK(p.y == doctest::Approx(0.0).epsilon(0.01));
    }

    TEST_CASE("4326 to 3857 — known point") {
        CoordinateTransform t(CRSId::EPSG_4326, CRSId::EPSG_3857);
        // London: lon=-0.1278, lat=51.5074
        auto p = t.transform(MapPoint(-0.1278, 51.5074));
        // Expected: x≈-14226, y≈6711966
        CHECK(p.x == doctest::Approx(-14226.0).epsilon(0.01));
        CHECK(p.y == doctest::Approx(6711966.0).epsilon(0.01));
    }

    TEST_CASE("3857 to 4326 — round trip") {
        CoordinateTransform t1(CRSId::EPSG_4326, CRSId::EPSG_3857);
        CoordinateTransform t2(CRSId::EPSG_3857, CRSId::EPSG_4326);
        MapPoint original(73.85, 18.52);  // Pune, India
        auto merc = t1.transform(original);
        auto back = t2.transform(merc);
        CHECK(back.x == doctest::Approx(original.x).epsilon(0.0001));
        CHECK(back.y == doctest::Approx(original.y).epsilon(0.0001));
    }

    TEST_CASE("short-circuited transform") {
        CoordinateTransform t(CRSId::EPSG_4326, CRSId::EPSG_4326);
        CHECK(t.isShortCircuited());
        auto p = t.transform(MapPoint(10, 20));
        CHECK(p.x == 10);
        CHECK(p.y == 20);
    }

    TEST_CASE("rectangle transform") {
        CoordinateTransform t(CRSId::EPSG_4326, CRSId::EPSG_3857);
        MapRectangle lonLat(73.8, 18.5, 73.9, 18.6);
        auto merc = t.transform(lonLat);
        CHECK(merc.width() > 8000);  // ~8km in meters
        CHECK(merc.height() > 11000);  // ~11km in meters
    }
}

TEST_SUITE("TileMatrix") {
    TEST_CASE("fromWebMercator zoom 0") {
        auto m = TileMatrix::fromWebMercator(0);
        CHECK(m.zoomLevel() == 0);
        CHECK(m.matrixWidth() == 1);
        CHECK(m.matrixHeight() == 1);
    }

    TEST_CASE("fromWebMercator zoom 3") {
        auto m = TileMatrix::fromWebMercator(3);
        CHECK(m.matrixWidth() == 8);
        CHECK(m.matrixHeight() == 8);
    }

    TEST_CASE("tileExtent zoom 1") {
        auto m = TileMatrix::fromWebMercator(1);
        // Tile (0,0) at zoom 1: top-left quadrant
        auto ext = m.tileExtent(TileXYZ(0, 0, 1));
        CHECK(ext.xMin == doctest::Approx(-ORIGIN_SHIFT).epsilon(0.01));
        CHECK(ext.xMax == doctest::Approx(0).epsilon(0.01));
        CHECK(ext.yMax == doctest::Approx(ORIGIN_SHIFT).epsilon(0.01));
        CHECK(ext.yMin == doctest::Approx(0).epsilon(0.01));
    }

    TEST_CASE("tileRangeFromExtent") {
        auto m = TileMatrix::fromWebMercator(2);
        // Small extent in top-left quadrant
        MapRectangle rect(-15000000, 5000000, -14000000, 6000000);
        auto range = m.tileRangeFromExtent(rect);
        CHECK(range.isValid());
        CHECK(range.startCol >= 0);
        CHECK(range.endCol < m.matrixWidth());
    }

    TEST_CASE("tileCenter") {
        auto m = TileMatrix::fromWebMercator(1);
        auto center = m.tileCenter(TileXYZ(0, 0, 1));
        CHECK(center.x == doctest::Approx(-ORIGIN_SHIFT / 2.0).epsilon(0.01));
        CHECK(center.y == doctest::Approx(ORIGIN_SHIFT / 2.0).epsilon(0.01));
    }
}

TEST_SUITE("TileMatrixSet") {
    TEST_CASE("matrix access") {
        TileMatrixSet set;
        auto m0 = set.matrix(0);
        CHECK(m0.matrixWidth() == 1);
        auto m5 = set.matrix(5);
        CHECK(m5.matrixWidth() == 32);
    }

    TEST_CASE("scaleToZoomLevel") {
        TileMatrixSet set;
        // At zoom 10: mpp = 2*20037508 / (256 * 1024) ≈ 152.9
        double mpp = (2.0 * ORIGIN_SHIFT) / (256.0 * 1024.0);
        int z = set.scaleToZoomLevel(mpp);
        CHECK(z == 10);
    }

    TEST_CASE("zoomLevelForExtent") {
        TileMatrixSet set;
        // Pune area ~10km x 10km, viewport 1000x1000
        MapRectangle lonLat(73.8, 18.5, 73.9, 18.6);
        int z = set.zoomLevelForExtent(lonLat, 1000, 1000);
        CHECK(z >= 12);
        CHECK(z <= 16);
    }
}

TEST_SUITE("MapToPixel") {
    TEST_CASE("world to screen center") {
        MapRectangle extent(0, 0, 1000, 1000);
        MapToPixel mtp = MapToPixel::fromExtent(extent, 500, 500);
        // Center of extent should map to center of screen
        QPointF screen = mtp.transform(500, 500);
        CHECK(screen.x() == doctest::Approx(250.0).epsilon(1.0));
        CHECK(screen.y() == doctest::Approx(250.0).epsilon(1.0));
    }

    TEST_CASE("screen to world center") {
        MapRectangle extent(0, 0, 1000, 1000);
        MapToPixel mtp = MapToPixel::fromExtent(extent, 500, 500);
        auto world = mtp.toMapCoordinates(250.0, 250.0);
        CHECK(world.x == doctest::Approx(500.0).epsilon(1.0));
        CHECK(world.y == doctest::Approx(500.0).epsilon(1.0));
    }

    TEST_CASE("Y axis flipped") {
        MapRectangle extent(0, 0, 100, 100);
        MapToPixel mtp = MapToPixel::fromExtent(extent, 100, 100);
        // North (y=100) should be at top of screen (y=0)
        QPointF north = mtp.transform(50, 100);
        CHECK(north.y() < 50);
        // South (y=0) should be at bottom of screen (y=100)
        QPointF south = mtp.transform(50, 0);
        CHECK(south.y() > 50);
    }

    TEST_CASE("round trip") {
        MapRectangle extent(100, 200, 300, 400);
        MapToPixel mtp = MapToPixel::fromExtent(extent, 800, 600);
        auto world = mtp.toMapCoordinates(400, 300);
        QPointF screen = mtp.transform(world.x, world.y);
        CHECK(screen.x() == doctest::Approx(400.0).epsilon(1.0));
        CHECK(screen.y() == doctest::Approx(300.0).epsilon(1.0));
    }
}

TEST_SUITE("XyzTileProvider URL formatting") {
    TEST_CASE("standard XYZ URL") {
        XyzTileProvider provider("https://mt1.google.com/vt/lyrs=s&x={x}&y={y}&z={z}");
        QString url = provider.urlForTile(TileXYZ(361, 229, 9));
        CHECK(url == "https://mt1.google.com/vt/lyrs=s&x=361&y=229&z=9");
    }

    TEST_CASE("TMS URL with {-y}") {
        XyzTileProvider provider("https://example.com/{z}/{x}/{-y}.png");
        QString url = provider.urlForTile(TileXYZ(0, 0, 1));
        // At zoom 1, matrixHeight=2, {-y} = 2-0-1 = 1
        CHECK(url == "https://example.com/1/0/1.png");
    }

    TEST_CASE("URL template stored") {
        XyzTileProvider provider("https://test.com/{z}/{x}/{y}");
        CHECK(provider.urlTemplate() == "https://test.com/{z}/{x}/{y}");
    }
}
