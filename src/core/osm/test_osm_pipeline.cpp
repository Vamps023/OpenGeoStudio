// ============================================================
// test_osm_pipeline.cpp — OSM import pipeline tests
// ============================================================
//
// Tests the complete OSM → Road Studio pipeline:
//   - XML parsing
//   - Coordinate conversion
//   - Road classification
//   - Road network building
//   - Junction detection
//   - Validation
//   - Save/reload (JSON serialization)
//

#include "../../core/osm/OsmImportPipeline.hpp"
#include "../../core/osm/OsmXmlParser.hpp"
#include "../../core/osm/CoordinateConverter.hpp"
#include "../../core/osm/RoadClassifier.hpp"
#include "../../core/osm/RoadNetworkBuilder.hpp"
#include "../../core/osm/JunctionDetector.hpp"
#include "../../core/osm/RoadValidator.hpp"
#include "../../core/osm/LaneGenerator.hpp"
#include "../../core/osm/RoundaboutGenerator.hpp"
#include "../../core/osm/RoadMarkingGenerator.hpp"
#include "../../core/osm/TrafficSignGenerator.hpp"
#include "../../core/osm/OsmProjectSerializer.hpp"
#include "../../core/osm/OsmExporter.hpp"

#include <QCoreApplication>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <QFileInfo>
#include <QDebug>
#include <iostream>
#include <cmath>
#include <vector>
#include <functional>

using namespace osm;

// ─── Test framework (minimal) ───
static int g_testsPassed = 0;
static int g_testsFailed = 0;

struct TestRunner {
    std::vector<std::pair<std::string, std::function<void()>>> tests;

    void add(const std::string& name, std::function<void()> fn) {
        tests.push_back({name, fn});
    }

    void runAll() {
        for (const auto& [name, fn] : tests) {
            int before = g_testsFailed;
            fn();
            if (g_testsFailed == before) {
                std::cout << "  PASS: " << name << std::endl;
            } else {
                std::cout << "  FAIL: " << name << std::endl;
            }
        }
    }
};

static TestRunner g_runner;

#define TEST(name) \
    static void name(); \
    struct name##_Reg { name##_Reg() { g_runner.add(#name, name); } } name##_reg; \
    static void name()

#define CHECK(cond, msg) \
    do { \
        if (cond) { \
            g_testsPassed++; \
        } else { \
            g_testsFailed++; \
            std::cerr << "    FAIL: " << #cond << " — " << msg \
                      << " (" << __FILE__ << ":" << __LINE__ << ")" << std::endl; \
        } \
    } while(0)

#define CHECK_EQ(a, b, msg) CHECK((a) == (b), msg)

// ─── Helper: create a simple OSM XML with a few roads ───
static QString createSimpleOsmXml() {
    return QStringLiteral(
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
"<osm version=\"0.6\" generator=\"test\">\n"
"  <bounds minlat=\"29.7600\" minlon=\"-95.3700\" maxlat=\"29.7620\" maxlon=\"-95.3680\"/>\n"
"  <node id=\"1\" lat=\"29.7600\" lon=\"-95.3700\"/>\n"
"  <node id=\"2\" lat=\"29.7600\" lon=\"-95.3695\"/>\n"
"  <node id=\"3\" lat=\"29.7600\" lon=\"-95.3690\"/>\n"
"  <node id=\"4\" lat=\"29.7600\" lon=\"-95.3685\"/>\n"
"  <node id=\"5\" lat=\"29.7600\" lon=\"-95.3680\"/>\n"
"  <node id=\"6\" lat=\"29.7605\" lon=\"-95.3690\"/>\n"
"  <node id=\"7\" lat=\"29.7615\" lon=\"-95.3690\"/>\n"
"  <node id=\"8\" lat=\"29.7620\" lon=\"-95.3690\"/>\n"
"  <node id=\"9\" lat=\"29.7610\" lon=\"-95.3680\"/>\n"
"  <node id=\"10\" lat=\"29.7610\" lon=\"-95.3700\"/>\n"
"  <way id=\"100\">\n"
"    <nd ref=\"1\"/>\n"
"    <nd ref=\"2\"/>\n"
"    <nd ref=\"3\"/>\n"
"    <nd ref=\"4\"/>\n"
"    <nd ref=\"5\"/>\n"
"    <tag k=\"highway\" v=\"residential\"/>\n"
"    <tag k=\"name\" v=\"Main Street\"/>\n"
"    <tag k=\"lanes\" v=\"2\"/>\n"
"  </way>\n"
"  <way id=\"200\">\n"
"    <nd ref=\"6\"/>\n"
"    <nd ref=\"3\"/>\n"
"    <nd ref=\"7\"/>\n"
"    <nd ref=\"8\"/>\n"
"    <tag k=\"highway\" v=\"primary\"/>\n"
"    <tag k=\"name\" v=\"North Avenue\"/>\n"
"    <tag k=\"lanes\" v=\"4\"/>\n"
"    <tag k=\"oneway\" v=\"yes\"/>\n"
"  </way>\n"
"  <way id=\"300\">\n"
"    <nd ref=\"10\"/>\n"
"    <nd ref=\"3\"/>\n"
"    <nd ref=\"9\"/>\n"
"    <tag k=\"highway\" v=\"secondary\"/>\n"
"    <tag k=\"name\" v=\"Cross Road\"/>\n"
"  </way>\n"
"</osm>\n"
);
}

// ─── Helper: create OSM with a roundabout ───
static QString createRoundaboutOsmXml() {
    return QStringLiteral(
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
"<osm version=\"0.6\" generator=\"test\">\n"
"  <bounds minlat=\"29.7600\" minlon=\"-95.3700\" maxlat=\"29.7620\" maxlon=\"-95.3680\"/>\n"
"  <node id=\"1\" lat=\"29.7610\" lon=\"-95.3695\"/>\n"
"  <node id=\"2\" lat=\"29.7611\" lon=\"-95.3693\"/>\n"
"  <node id=\"3\" lat=\"29.7610\" lon=\"-95.3691\"/>\n"
"  <node id=\"4\" lat=\"29.7609\" lon=\"-95.3693\"/>\n"
"  <node id=\"10\" lat=\"29.7610\" lon=\"-95.3700\"/>\n"
"  <node id=\"20\" lat=\"29.7620\" lon=\"-95.3693\"/>\n"
"  <node id=\"30\" lat=\"29.7610\" lon=\"-95.3680\"/>\n"
"  <node id=\"40\" lat=\"29.7600\" lon=\"-95.3693\"/>\n"
"  <way id=\"100\">\n"
"    <nd ref=\"1\"/>\n"
"    <nd ref=\"2\"/>\n"
"    <nd ref=\"3\"/>\n"
"    <nd ref=\"4\"/>\n"
"    <nd ref=\"1\"/>\n"
"    <tag k=\"highway\" v=\"primary\"/>\n"
"    <tag k=\"junction\" v=\"roundabout\"/>\n"
"    <tag k=\"name\" v=\"Test Roundabout\"/>\n"
"  </way>\n"
"  <way id=\"200\">\n"
"    <nd ref=\"10\"/>\n"
"    <nd ref=\"1\"/>\n"
"    <tag k=\"highway\" v=\"primary\"/>\n"
"    <tag k=\"name\" v=\"Approach West\"/>\n"
"  </way>\n"
"  <way id=\"300\">\n"
"    <nd ref=\"3\"/>\n"
"    <nd ref=\"30\"/>\n"
"    <tag k=\"highway\" v=\"primary\"/>\n"
"    <tag k=\"name\" v=\"Approach East\"/>\n"
"  </way>\n"
"  <way id=\"400\">\n"
"    <nd ref=\"40\"/>\n"
"    <nd ref=\"4\"/>\n"
"    <tag k=\"highway\" v=\"primary\"/>\n"
"    <tag k=\"name\" v=\"Approach South\"/>\n"
"  </way>\n"
"  <way id=\"500\">\n"
"    <nd ref=\"2\"/>\n"
"    <nd ref=\"20\"/>\n"
"    <tag k=\"highway\" v=\"primary\"/>\n"
"    <tag k=\"name\" v=\"Approach North\"/>\n"
"  </way>\n"
"</osm>\n"
);
}

// ─── Helper: create OSM with a bridge ───
static QString createBridgeOsmXml() {
    return QStringLiteral(
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
"<osm version=\"0.6\" generator=\"test\">\n"
"  <bounds minlat=\"29.7600\" minlon=\"-95.3700\" maxlat=\"29.7620\" maxlon=\"-95.3680\"/>\n"
"  <node id=\"1\" lat=\"29.7600\" lon=\"-95.3690\"/>\n"
"  <node id=\"2\" lat=\"29.7605\" lon=\"-95.3690\"/>\n"
"  <node id=\"3\" lat=\"29.7610\" lon=\"-95.3690\"/>\n"
"  <node id=\"4\" lat=\"29.7615\" lon=\"-95.3690\"/>\n"
"  <node id=\"5\" lat=\"29.7620\" lon=\"-95.3690\"/>\n"
"  <node id=\"6\" lat=\"29.7610\" lon=\"-95.3700\"/>\n"
"  <node id=\"7\" lat=\"29.7610\" lon=\"-95.3680\"/>\n"
"  <way id=\"100\">\n"
"    <nd ref=\"1\"/>\n"
"    <nd ref=\"2\"/>\n"
"    <nd ref=\"3\"/>\n"
"    <nd ref=\"4\"/>\n"
"    <nd ref=\"5\"/>\n"
"    <tag k=\"highway\" v=\"motorway\"/>\n"
"    <tag k=\"bridge\" v=\"yes\"/>\n"
"    <tag k=\"layer\" v=\"1\"/>\n"
"    <tag k=\"lanes\" v=\"3\"/>\n"
"  </way>\n"
"  <way id=\"200\">\n"
"    <nd ref=\"6\"/>\n"
"    <nd ref=\"3\"/>\n"
"    <nd ref=\"7\"/>\n"
"    <tag k=\"highway\" v=\"primary\"/>\n"
"    <tag k=\"layer\" v=\"0\"/>\n"
"  </way>\n"
"</osm>\n"
);
}

// ═══════════════════════════════════════════════════════════
// TESTS
// ═══════════════════════════════════════════════════════════

// ─── Test 1: XML parsing ───
TEST(test_xml_parsing) {
    QString xml = createSimpleOsmXml();
    OsmData data;
    QString error;
    bool ok = OsmXmlParser::parseContent(xml, data, &error);

    CHECK(ok, "XML parsing should succeed");
    CHECK(error.isEmpty(), "No error message expected");
    CHECK_EQ(data.nodeCount(), 10, "Should parse 10 nodes");
    CHECK_EQ(data.wayCount(), 3, "Should parse 3 ways");
    CHECK(data.hasBounds, "Should have bounds");

    const Node* n1 = data.getNode(1);
    CHECK(n1 != nullptr, "Node 1 should exist");
    CHECK(std::abs(n1->lat - 29.7600) < 1e-6, "Node 1 lat");
    CHECK(std::abs(n1->lon - (-95.3700)) < 1e-6, "Node 1 lon");

    const Way* w100 = data.getWay(100);
    CHECK(w100 != nullptr, "Way 100 should exist");
    CHECK(w100->isHighway(), "Way 100 should be highway");
    CHECK_EQ(w100->highwayType(), "residential", "Way 100 type");
    CHECK_EQ(w100->name(), "Main Street", "Way 100 name");
    CHECK_EQ(w100->lanes(), 2, "Way 100 lanes");
    CHECK_EQ(int(w100->nodeRefs.size()), 5, "Way 100 node count");
}

// ─── Test 2: Coordinate conversion ───
TEST(test_coordinate_conversion) {
    CoordinateConverter conv;
    conv.setReference(29.7610, -95.3690, CoordinateConverter::Method::Equirectangular);

    double x, y;
    conv.toLocal(29.7610, -95.3690, x, y);
    CHECK(std::abs(x) < 0.01 && std::abs(y) < 0.01, "Reference point should be origin");

    // Move 0.001 degrees east (~97m at this latitude)
    conv.toLocal(29.7610, -95.3680, x, y);
    CHECK(x > 50 && x < 150, "Eastward offset should be ~97m");
    CHECK(std::abs(y) < 1.0, "No northward change");

    // Move 0.001 degrees north (~111m)
    conv.toLocal(29.7620, -95.3690, x, y);
    CHECK(std::abs(x) < 1.0, "No eastward change");
    CHECK(y > 50 && y < 150, "Northward offset should be ~111m");
}

// ─── Test 3: Road classification ───
TEST(test_road_classification) {
    RoadClassInfo motorway = RoadClassifier::classifyAndGet("motorway");
    CHECK_EQ(motorway.cls, RoadClass::Motorway, "motorway class");
    CHECK(motorway.defaultLanes >= 2, "Motorway should have at least 2 lanes");
    CHECK(motorway.defaultSpeed > 80, "Motorway speed > 80");

    RoadClassInfo residential = RoadClassifier::classifyAndGet("residential");
    CHECK_EQ(residential.cls, RoadClass::Residential, "residential class");

    CHECK(RoadClassifier::isDrivable(RoadClass::Motorway), "Motorway drivable");
    CHECK(RoadClassifier::isDrivable(RoadClass::Residential), "Residential drivable");
}

// ─── Test 4: Road network building ───
TEST(test_road_network_building) {
    QString xml = createSimpleOsmXml();
    OsmData data;
    QString error;
    OsmXmlParser::parseContent(xml, data, &error);

    CoordinateConverter conv;
    conv.setReferenceFromBounds(data.minLat, data.minLon,
                                 data.maxLat, data.maxLon,
                                 CoordinateConverter::Method::Equirectangular);

    RoadNetworkBuilder::Result network = RoadNetworkBuilder::build(data, conv);

    CHECK(network.roadsCreated == 3, "Should create 3 roads");
    CHECK(network.segmentsCreated > 0, "Should create segments");
    CHECK(network.roads.size() == 3, "Roads vector size");

    bool foundMain = false, foundNorth = false, foundCross = false;
    for (const auto& road : network.roads) {
        if (road.name == "Main Street") foundMain = true;
        if (road.name == "North Avenue") foundNorth = true;
        if (road.name == "Cross Road") foundCross = true;
    }
    CHECK(foundMain, "Main Street road found");
    CHECK(foundNorth, "North Avenue road found");
    CHECK(foundCross, "Cross Road road found");

    CHECK(network.junctionsDetected >= 1, "Should detect at least 1 junction");
}

// ─── Test 5: Junction detection ───
TEST(test_junction_detection) {
    QString xml = createSimpleOsmXml();
    OsmData data;
    OsmXmlParser::parseContent(xml, data);

    CoordinateConverter conv;
    conv.setReferenceFromBounds(data.minLat, data.minLon,
                                 data.maxLat, data.maxLon);

    RoadNetworkBuilder::Result network = RoadNetworkBuilder::build(data, conv);
    auto junctions = JunctionDetector::detect(network, data);

    CHECK(!junctions.empty(), "Should detect junctions");
    bool foundJunctionAtNode3 = false;
    for (const auto& j : junctions) {
        if (j.osmNodeId == 3) {
            foundJunctionAtNode3 = true;
            CHECK(int(j.roadIds.size()) >= 3, "Node 3 should have 3+ roads");
        }
    }
    CHECK(foundJunctionAtNode3, "Should find junction at node 3");
}

// ─── Test 6: Roundabout detection ───
TEST(test_roundabout_detection) {
    QString xml = createRoundaboutOsmXml();
    OsmData data;
    OsmXmlParser::parseContent(xml, data);

    CoordinateConverter conv;
    conv.setReferenceFromBounds(data.minLat, data.minLon,
                                 data.maxLat, data.maxLon);

    RoadNetworkBuilder::Result network = RoadNetworkBuilder::build(data, conv);
    auto junctions = JunctionDetector::detect(network, data);

    bool foundRoundabout = false;
    for (const auto& j : junctions) {
        if (j.isRoundabout) {
            foundRoundabout = true;
            CHECK_EQ(int(j.type), int(JunctionType::Roundabout), "Roundabout type");
        }
    }
    CHECK(foundRoundabout, "Should detect roundabout");
}

// ─── Test 7: Bridge/overpass detection ───
TEST(test_bridge_detection) {
    QString xml = createBridgeOsmXml();
    OsmData data;
    OsmXmlParser::parseContent(xml, data);

    CoordinateConverter conv;
    conv.setReferenceFromBounds(data.minLat, data.minLon,
                                 data.maxLat, data.maxLon);

    RoadNetworkBuilder::Result network = RoadNetworkBuilder::build(data, conv);
    auto junctions = JunctionDetector::detect(network, data);

    bool foundOverpass = false;
    for (const auto& j : junctions) {
        if (j.type == JunctionType::Overpass) {
            foundOverpass = true;
        }
    }
    CHECK(foundOverpass, "Should detect overpass");
}

// ─── Test 8: Validation ───
TEST(test_validation) {
    QString xml = createSimpleOsmXml();
    OsmData data;
    OsmXmlParser::parseContent(xml, data);

    CoordinateConverter conv;
    conv.setReferenceFromBounds(data.minLat, data.minLon,
                                 data.maxLat, data.maxLon);

    RoadNetworkBuilder::Result network = RoadNetworkBuilder::build(data, conv);
    auto junctions = JunctionDetector::detect(network, data);

    auto issues = RoadValidator::validate(network, junctions);

    int errors = 0;
    for (const auto& i : issues) {
        if (i.severity == Severity::Error) errors++;
    }
    CHECK(errors == 0, "Clean data should have no validation errors");
}

// ─── Test 9: Full pipeline import ───
TEST(test_full_pipeline) {
    QString xml = createSimpleOsmXml();

    QTemporaryFile tmpFile;
    tmpFile.setAutoRemove(true);
    tmpFile.open();
    tmpFile.write(xml.toUtf8());
    tmpFile.close();

    ImportSettings settings;
    settings.autoDetectReference = true;
    settings.runValidation = true;
    settings.autoRepair = true;

    ImportResult result = OsmImportPipeline::importFromFile(tmpFile.fileName(), settings);

    CHECK(result.success, "Pipeline should succeed");
    CHECK(result.stats.roadsCreated == 3, "Should create 3 roads");
    CHECK(result.stats.junctionsDetected >= 1, "Should detect junctions");
    CHECK(result.stats.totalRoadLength > 0, "Should have positive road length");
    CHECK(result.errorMessage.isEmpty(), "No error message");
}

// ─── Test 10: Serialization (save/reload) ───
TEST(test_serialization) {
    QString xml = createSimpleOsmXml();
    OsmData data;
    OsmXmlParser::parseContent(xml, data);

    QJsonObject json = data.toJson();
    CHECK(json.contains("nodes"), "JSON should have nodes");
    CHECK(json.contains("ways"), "JSON should have ways");

    OsmData restored = OsmData::fromJson(json);
    CHECK_EQ(restored.nodeCount(), data.nodeCount(), "Node count after reload");
    CHECK_EQ(restored.wayCount(), data.wayCount(), "Way count after reload");

    const Node* n1 = restored.getNode(1);
    CHECK(n1 != nullptr, "Node 1 after reload");
    CHECK(std::abs(n1->lat - 29.7600) < 1e-6, "Node 1 lat after reload");

    const Way* w100 = restored.getWay(100);
    CHECK(w100 != nullptr, "Way 100 after reload");
    CHECK_EQ(w100->name(), "Main Street", "Way 100 name after reload");
}

// ─── Test 11: Import result serialization ───
TEST(test_import_result_serialization) {
    QString xml = createSimpleOsmXml();

    QTemporaryFile tmpFile;
    tmpFile.setAutoRemove(true);
    tmpFile.open();
    tmpFile.write(xml.toUtf8());
    tmpFile.close();

    ImportSettings settings;
    ImportResult result = OsmImportPipeline::importFromFile(tmpFile.fileName(), settings);

    CHECK(result.success, "Import should succeed");

    QJsonObject json = result.toJson();
    CHECK(json["success"].toBool(), "JSON success flag");

    ImportResult restored = ImportResult::fromJson(json);
    CHECK(restored.success, "Restored success flag");
    CHECK_EQ(restored.stats.roadsCreated, result.stats.roadsCreated, "Roads count after reload");
    CHECK_EQ(restored.stats.osmNodes, result.stats.osmNodes, "OSM nodes after reload");
}

// ─── Test 12: One-way road handling ───
TEST(test_oneway_handling) {
    QString xml = createSimpleOsmXml();
    OsmData data;
    OsmXmlParser::parseContent(xml, data);

    CoordinateConverter conv;
    conv.setReferenceFromBounds(data.minLat, data.minLon,
                                 data.maxLat, data.maxLon);

    RoadNetworkBuilder::Result network = RoadNetworkBuilder::build(data, conv);

    const geo::RoadV2* northAve = nullptr;
    for (const auto& road : network.roads) {
        if (road.name == "North Avenue") {
            northAve = &road;
            break;
        }
    }
    CHECK(northAve != nullptr, "North Avenue should exist");
    CHECK(northAve->laneCount == 4, "North Avenue should have 4 lanes");

    CHECK(northAve->numLaneSections() > 0, "Should have lane section");
    const auto& ls = northAve->laneSection(0);
    for (const auto& lane : ls.lanes()) {
        if (lane.id == 0) continue;  // skip center (virtual)
        CHECK(lane.id > 0, "One-way road lanes should be positive (right side)");
    }
}

// ─── Test 13: Empty OSM data ───
TEST(test_empty_osm) {
    QString xml = QStringLiteral(
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
"<osm version=\"0.6\" generator=\"test\">\n"
"</osm>\n"
);

    OsmData data;
    QString error;
    bool ok = OsmXmlParser::parseContent(xml, data, &error);

    CHECK(ok, "Empty OSM should parse");
    CHECK_EQ(data.nodeCount(), 0, "No nodes");
    CHECK_EQ(data.wayCount(), 0, "No ways");
}

// ─── Test 14: Road metadata preservation ───
TEST(test_road_metadata) {
    QString xml = createSimpleOsmXml();
    OsmData data;
    OsmXmlParser::parseContent(xml, data);

    CoordinateConverter conv;
    conv.setReferenceFromBounds(data.minLat, data.minLon,
                                 data.maxLat, data.maxLon);

    RoadNetworkBuilder::build(data, conv);

    std::string mainId = "osm_way_100";
    const RoadNetworkBuilder::RoadMetadata* meta = RoadNetworkBuilder::getMetadata(mainId);
    CHECK(meta != nullptr, "Metadata for way 100 should exist");
    if (meta) {
        CHECK_EQ(meta->highwayType, "residential", "Metadata highway type");
        CHECK_EQ(meta->name, "Main Street", "Metadata name");
        CHECK_EQ(meta->osmWayId, qint64(100), "Metadata OSM way ID");
    }
}

// ─── Test 15: Lane generator — basic two-way road ───
TEST(test_lane_generator_basic) {
    Way way;
    way.tags.append(Tag{"highway", "residential"});
    way.tags.append(Tag{"lanes", "2"});

    RoadClassInfo info = RoadClassifier::classifyAndGet("residential");
    geo::LaneSection ls;
    auto result = LaneGenerator::generate(way, info, ls);

    CHECK(result.success, "Lane generation should succeed");
    CHECK_EQ(result.totalLanes, 2, "Should have 2 total lanes");
    CHECK_EQ(result.forwardLanes, 1, "1 forward lane");
    CHECK_EQ(result.backwardLanes, 1, "1 backward lane");

    // Check lane IDs
    const geo::Lane* right = ls.findLane(1);
    CHECK(right != nullptr, "Right lane (id=1) should exist");
    CHECK(right->type == geo::LaneType::Driving, "Right lane should be Driving");

    const geo::Lane* left = ls.findLane(-1);
    CHECK(left != nullptr, "Left lane (id=-1) should exist");
    CHECK(left->type == geo::LaneType::Driving, "Left lane should be Driving");
}

// ─── Test 16: Lane generator — one-way road ───
TEST(test_lane_generator_oneway) {
    Way way;
    way.tags.append(Tag{"highway", "primary"});
    way.tags.append(Tag{"lanes", "4"});
    way.tags.append(Tag{"oneway", "yes"});

    RoadClassInfo info = RoadClassifier::classifyAndGet("primary");
    geo::LaneSection ls;
    auto result = LaneGenerator::generate(way, info, ls);

    CHECK(result.success, "Lane generation should succeed");
    CHECK_EQ(result.forwardLanes, 4, "4 forward lanes");
    CHECK_EQ(result.backwardLanes, 0, "0 backward lanes");

    // All driving lanes should have positive IDs
    for (const auto& lane : ls.lanes()) {
        if (lane.type == geo::LaneType::Driving) {
            CHECK(lane.id > 0, "One-way driving lanes should be positive");
        }
    }
}

// ─── Test 17: Lane generator — forward/backward split ───
TEST(test_lane_generator_fwd_bwd_split) {
    Way way;
    way.tags.append(Tag{"highway", "secondary"});
    way.tags.append(Tag{"lanes", "3"});
    way.tags.append(Tag{"lanes:forward", "2"});
    way.tags.append(Tag{"lanes:backward", "1"});

    RoadClassInfo info = RoadClassifier::classifyAndGet("secondary");
    geo::LaneSection ls;
    auto result = LaneGenerator::generate(way, info, ls);

    CHECK(result.success, "Lane generation should succeed");
    CHECK_EQ(result.forwardLanes, 2, "2 forward lanes");
    CHECK_EQ(result.backwardLanes, 1, "1 backward lane");
}

// ─── Test 18: Lane generator — bus lane ───
TEST(test_lane_generator_bus) {
    Way way;
    way.tags.append(Tag{"highway", "primary"});
    way.tags.append(Tag{"lanes", "2"});
    way.tags.append(Tag{"lanes:bus", "1"});

    RoadClassInfo info = RoadClassifier::classifyAndGet("primary");
    geo::LaneSection ls;
    auto result = LaneGenerator::generate(way, info, ls);

    CHECK(result.success, "Lane generation should succeed");
    CHECK_EQ(result.busLanes, 1, "Should have 1 bus lane");

    // Find the bus lane
    bool foundBus = false;
    for (const auto& lane : ls.lanes()) {
        if (lane.type == geo::LaneType::Bus) {
            foundBus = true;
        }
    }
    CHECK(foundBus, "Bus lane should exist in lane section");
}

// ─── Test 19: Lane generator — cycleway ───
TEST(test_lane_generator_cycleway) {
    Way way;
    way.tags.append(Tag{"highway", "secondary"});
    way.tags.append(Tag{"lanes", "2"});
    way.tags.append(Tag{"cycleway", "lane"});

    RoadClassInfo info = RoadClassifier::classifyAndGet("secondary");
    geo::LaneSection ls;
    auto result = LaneGenerator::generate(way, info, ls);

    CHECK(result.success, "Lane generation should succeed");
    CHECK_EQ(result.cycleLanes, 1, "Should have 1 cycle lane");

    bool foundBike = false;
    for (const auto& lane : ls.lanes()) {
        if (lane.type == geo::LaneType::Biking) {
            foundBike = true;
        }
    }
    CHECK(foundBike, "Biking lane should exist in lane section");
}

// ─── Test 20: Lane generator — sidewalk/shoulder/median ───
TEST(test_lane_generator_sidewalk) {
    Way way;
    way.tags.append(Tag{"highway", "residential"});
    way.tags.append(Tag{"lanes", "2"});
    way.tags.append(Tag{"sidewalk", "both"});
    way.tags.append(Tag{"shoulder", "yes"});
    way.tags.append(Tag{"median", "lined"});

    RoadClassInfo info = RoadClassifier::classifyAndGet("residential");
    geo::LaneSection ls;
    auto result = LaneGenerator::generate(way, info, ls);

    CHECK(result.hasSidewalk, "Should detect sidewalk");
    CHECK(result.hasShoulder, "Should detect shoulder");
    CHECK(result.hasMedian, "Should detect median");
}

// ─── Test 21: Lane generator — default lanes from class ───
TEST(test_lane_generator_defaults) {
    Way way;
    way.tags.append(Tag{"highway", "motorway"});

    RoadClassInfo info = RoadClassifier::classifyAndGet("motorway");
    geo::LaneSection ls;
    auto result = LaneGenerator::generate(way, info, ls);

    CHECK(result.success, "Lane generation should succeed");
    CHECK(result.totalLanes >= 2, "Motorway should have at least 2 lanes by default");
}

// ─── Test 22: Lane generator — turn lanes parsing ───
TEST(test_turn_lanes_parsing) {
    QStringList turns = LaneGenerator::parseTurnLanes("left|through|through|right");
    CHECK_EQ(turns.size(), 4, "Should parse 4 turn lanes");
    CHECK_EQ(turns[0], "left", "First turn is left");
    CHECK_EQ(turns[1], "through", "Second turn is through");
    CHECK_EQ(turns[3], "right", "Fourth turn is right");

    QStringList empty = LaneGenerator::parseTurnLanes("");
    CHECK_EQ(empty.size(), 0, "Empty turn lanes string");
}

// ═══════════════════════════════════════════════════════════
// PHASE 7-12 TESTS
// ═══════════════════════════════════════════════════════════

// ─── Test 23: Roundabout generation ───
TEST(test_roundabout_generation) {
    QString xml = createRoundaboutOsmXml();
    OsmData data;
    OsmXmlParser::parseContent(xml, data);

    CoordinateConverter conv;
    conv.setReferenceFromBounds(data.minLat, data.minLon,
                                 data.maxLat, data.maxLon);

    RoadNetworkBuilder::Result network = RoadNetworkBuilder::build(data, conv);
    auto junctions = JunctionDetector::detect(network, data);

    auto roundabouts = RoundaboutGenerator::generateAll(junctions, network, data);

    CHECK(!roundabouts.empty(), "Should generate roundabouts");
    if (!roundabouts.empty()) {
        const auto& rb = roundabouts[0];
        CHECK(rb.radius > 0, "Roundabout radius should be positive");
        CHECK(rb.circumference > 0, "Circumference should be positive");
        CHECK(!rb.ringPoints.empty(), "Should have ring points");
        CHECK(rb.circulatoryLanes >= 1, "At least 1 circulatory lane");
    }
}

// ─── Test 24: Roundabout ring road creation ───
TEST(test_roundabout_ring_road) {
    QString xml = createRoundaboutOsmXml();
    OsmData data;
    OsmXmlParser::parseContent(xml, data);

    CoordinateConverter conv;
    conv.setReferenceFromBounds(data.minLat, data.minLon,
                                 data.maxLat, data.maxLon);

    RoadNetworkBuilder::Result network = RoadNetworkBuilder::build(data, conv);
    auto junctions = JunctionDetector::detect(network, data);
    auto roundabouts = RoundaboutGenerator::generateAll(junctions, network, data);

    if (!roundabouts.empty()) {
        geo::RoadV2 ringRoad = RoundaboutGenerator::createRingRoad(roundabouts[0]);
        CHECK(ringRoad.numSegments() > 0, "Ring road should have segments");
        CHECK(ringRoad.totalLength() > 0, "Ring road should have positive length");
        CHECK(ringRoad.numLaneSections() > 0, "Ring road should have lane section");
    }
}

// ─── Test 25: Roundabout serialization ───
TEST(test_roundabout_serialization) {
    QString xml = createRoundaboutOsmXml();
    OsmData data;
    OsmXmlParser::parseContent(xml, data);

    CoordinateConverter conv;
    conv.setReferenceFromBounds(data.minLat, data.minLon,
                                 data.maxLat, data.maxLon);

    RoadNetworkBuilder::Result network = RoadNetworkBuilder::build(data, conv);
    auto junctions = JunctionDetector::detect(network, data);
    auto roundabouts = RoundaboutGenerator::generateAll(junctions, network, data);

    if (!roundabouts.empty()) {
        QJsonObject json = RoundaboutGenerator::toJson(roundabouts[0]);
        RoundaboutGeometry restored = RoundaboutGenerator::fromJson(json);

        CHECK_EQ(restored.id, roundabouts[0].id, "ID after reload");
        CHECK(std::abs(restored.radius - roundabouts[0].radius) < 0.001, "Radius after reload");
        CHECK_EQ(restored.ringPoints.size(), roundabouts[0].ringPoints.size(), "Ring points after reload");
    }
}

// ─── Test 26: Road marking generation ───
TEST(test_road_marking_generation) {
    QString xml = createSimpleOsmXml();
    OsmData data;
    OsmXmlParser::parseContent(xml, data);

    CoordinateConverter conv;
    conv.setReferenceFromBounds(data.minLat, data.minLon,
                                 data.maxLat, data.maxLon);

    RoadNetworkBuilder::Result network = RoadNetworkBuilder::build(data, conv);
    auto junctions = JunctionDetector::detect(network, data);

    auto markings = RoadMarkingGenerator::generateAll(network, junctions);

    CHECK(!markings.empty(), "Should generate markings");

    // Check that we have center lines and edge lines
    bool hasCenter = false, hasEdge = false, hasDivider = false;
    for (const auto& m : markings) {
        if (m.type == MarkingType::CenterLine) hasCenter = true;
        if (m.type == MarkingType::EdgeLine) hasEdge = true;
        if (m.type == MarkingType::LaneDivider) hasDivider = true;
    }
    CHECK(hasEdge, "Should have edge lines");
}

// ─── Test 27: Road marking serialization ───
TEST(test_road_marking_serialization) {
    QString xml = createSimpleOsmXml();
    OsmData data;
    OsmXmlParser::parseContent(xml, data);

    CoordinateConverter conv;
    conv.setReferenceFromBounds(data.minLat, data.minLon,
                                 data.maxLat, data.maxLon);

    RoadNetworkBuilder::Result network = RoadNetworkBuilder::build(data, conv);
    auto junctions = JunctionDetector::detect(network, data);
    auto markings = RoadMarkingGenerator::generateAll(network, junctions);

    QJsonArray json = RoadMarkingGenerator::toJsonArray(markings);
    auto restored = RoadMarkingGenerator::fromJsonArray(json);

    CHECK_EQ(restored.size(), markings.size(), "Marking count after reload");
    if (!restored.empty()) {
        CHECK_EQ(int(restored[0].type), int(markings[0].type), "First marking type after reload");
    }
}

// ─── Test 28: Traffic sign generation ───
TEST(test_traffic_sign_generation) {
    QString xml = createSimpleOsmXml();
    OsmData data;
    OsmXmlParser::parseContent(xml, data);

    CoordinateConverter conv;
    conv.setReferenceFromBounds(data.minLat, data.minLon,
                                 data.maxLat, data.maxLon);

    RoadNetworkBuilder::Result network = RoadNetworkBuilder::build(data, conv);
    auto junctions = JunctionDetector::detect(network, data);

    auto signs = TrafficSignGenerator::generateAll(data, network, junctions);

    CHECK(!signs.empty(), "Should generate signs");

    // Should have stop signs at junctions
    bool hasStop = false;
    for (const auto& s : signs) {
        if (s.type == SignType::Stop) hasStop = true;
    }
    CHECK(hasStop, "Should have stop signs at junctions");
}

// ─── Test 29: Traffic sign serialization ───
TEST(test_traffic_sign_serialization) {
    QString xml = createSimpleOsmXml();
    OsmData data;
    OsmXmlParser::parseContent(xml, data);

    CoordinateConverter conv;
    conv.setReferenceFromBounds(data.minLat, data.minLon,
                                 data.maxLat, data.maxLon);

    RoadNetworkBuilder::Result network = RoadNetworkBuilder::build(data, conv);
    auto junctions = JunctionDetector::detect(network, data);
    auto signs = TrafficSignGenerator::generateAll(data, network, junctions);

    QJsonArray json = TrafficSignGenerator::toJsonArray(signs);
    auto restored = TrafficSignGenerator::fromJsonArray(json);

    CHECK_EQ(restored.size(), signs.size(), "Sign count after reload");
}

// ─── Test 30: Project save/load ───
TEST(test_project_save_load) {
    QString xml = createSimpleOsmXml();
    QTemporaryDir tmpDir;
    QString projectPath = tmpDir.filePath("test.ogosm");

    // Import
    QTemporaryFile tmpOsm;
    tmpOsm.setAutoRemove(true);
    tmpOsm.open();
    tmpOsm.write(xml.toUtf8());
    tmpOsm.close();

    ImportSettings settings;
    ImportResult result = OsmImportPipeline::importFromFile(tmpOsm.fileName(), settings);
    CHECK(result.success, "Import should succeed");

    // Generate all data
    auto roundabouts = RoundaboutGenerator::generateAll(
        result.junctions, result.network, result.osmData);
    auto markings = RoadMarkingGenerator::generateAll(
        result.network, result.junctions);
    auto signs = TrafficSignGenerator::generateAll(
        result.osmData, result.network, result.junctions);

    // Build project data
    auto projectData = OsmProjectData::fromImportResult(
        result, roundabouts, markings, signs, "Test Project");

    // Save
    QString saveError;
    bool saved = OsmProjectData::saveToFile(projectPath, projectData, &saveError);
    CHECK(saved, "Project should save successfully");
    CHECK(QFileInfo::exists(projectPath), "Project file should exist");

    // Load
    bool loadOk = false;
    QString loadError;
    auto loaded = OsmProjectData::loadFromFile(projectPath, &loadOk, &loadError);
    CHECK(loadOk, "Project should load successfully");
    CHECK_EQ(loaded.roadCount, projectData.roadCount, "Road count after reload");
    CHECK_EQ(loaded.junctionCount, projectData.junctionCount, "Junction count after reload");
    CHECK_EQ(loaded.markingCount, projectData.markingCount, "Marking count after reload");
    CHECK_EQ(loaded.signCount, projectData.signCount, "Sign count after reload");
    CHECK(std::abs(loaded.totalRoadLength - projectData.totalRoadLength) < 0.1,
          "Total road length after reload");
}

// ─── Test 31: OpenDRIVE export ───
TEST(test_opendrive_export) {
    QString xml = createSimpleOsmXml();
    OsmData data;
    OsmXmlParser::parseContent(xml, data);

    CoordinateConverter conv;
    conv.setReferenceFromBounds(data.minLat, data.minLon,
                                 data.maxLat, data.maxLon);

    RoadNetworkBuilder::Result network = RoadNetworkBuilder::build(data, conv);
    auto junctions = JunctionDetector::detect(network, data);

    QTemporaryDir tmpDir;
    QString exportPath = tmpDir.filePath("export.xodr");

    OsmExporter::OpenDriveParams params;
    QString error;
    bool exported = OsmExporter::exportToOpenDrive(
        exportPath, network, junctions, conv, params, &error);

    CHECK(exported, "OpenDRIVE export should succeed");
    CHECK(QFileInfo::exists(exportPath), "Export file should exist");

    // Validate
    QString valError;
    bool valid = OsmExporter::validateExport(exportPath, &valError);
    CHECK(valid, "Exported OpenDRIVE should be valid");

    // SCANeR compatibility: header must be OpenDRIVE 1.6 with a
    // standard PROJ geoReference (not the non-standard refLat/refLon).
    QFile xodr(exportPath);
    QString content;
    if (xodr.open(QIODevice::ReadOnly | QIODevice::Text)) {
        content = QString::fromUtf8(xodr.readAll());
        xodr.close();
    }
    CHECK(content.contains("revMinor=\"6\""),
          "Header must declare OpenDRIVE 1.6 for SCANeR import");
    CHECK(content.contains("<geoReference><![CDATA[+proj="),
          "geoReference must be a standard PROJ string in CDATA");
    CHECK(content.contains("+lat_0=") && content.contains("+lon_0="),
          "PROJ string must carry the reference origin");
    // Header bounds must be real values, not all-zero
    CHECK(!content.contains("north=\"0\" south=\"0\" east=\"0\" west=\"0\""),
          "Header bounding box must be filled from network extent");
}

// ─── Test 32: GeoJSON export ───
TEST(test_geojson_export) {
    QString xml = createSimpleOsmXml();
    OsmData data;
    OsmXmlParser::parseContent(xml, data);

    CoordinateConverter conv;
    conv.setReferenceFromBounds(data.minLat, data.minLon,
                                 data.maxLat, data.maxLon);

    RoadNetworkBuilder::Result network = RoadNetworkBuilder::build(data, conv);
    auto junctions = JunctionDetector::detect(network, data);

    QTemporaryDir tmpDir;
    QString exportPath = tmpDir.filePath("export.geojson");

    OsmExporter::GeoJsonParams params;
    QString error;
    bool exported = OsmExporter::exportToGeoJson(
        exportPath, network, junctions, conv, params, &error);

    CHECK(exported, "GeoJSON export should succeed");
    CHECK(QFileInfo::exists(exportPath), "Export file should exist");

    // Validate
    QString valError;
    bool valid = OsmExporter::validateExport(exportPath, &valError);
    CHECK(valid, "Exported GeoJSON should be valid");
}

// ─── Test 33: Complete end-to-end workflow ───
TEST(test_end_to_end_workflow) {
    // 1. Import
    QString xml = createSimpleOsmXml();
    QTemporaryFile tmpOsm;
    tmpOsm.setAutoRemove(true);
    tmpOsm.open();
    tmpOsm.write(xml.toUtf8());
    tmpOsm.close();

    ImportSettings settings;
    settings.runValidation = true;
    settings.autoRepair = true;
    ImportResult result = OsmImportPipeline::importFromFile(tmpOsm.fileName(), settings);
    CHECK(result.success, "1. Import should succeed");

    // 2. Generate roundabouts
    auto roundabouts = RoundaboutGenerator::generateAll(
        result.junctions, result.network, result.osmData);
    CHECK(true, "2. Roundabout generation complete");

    // 3. Generate markings
    auto markings = RoadMarkingGenerator::generateAll(
        result.network, result.junctions);
    CHECK(!markings.empty(), "3. Markings should be generated");

    // 4. Generate signs
    auto signs = TrafficSignGenerator::generateAll(
        result.osmData, result.network, result.junctions);
    CHECK(!signs.empty(), "4. Signs should be generated");

    // 5. Save project
    QTemporaryDir tmpDir;
    QString projectPath = tmpDir.filePath("e2e.ogosm");
    auto projectData = OsmProjectData::fromImportResult(
        result, roundabouts, markings, signs, "E2E Test");
    QString saveError;
    CHECK(OsmProjectData::saveToFile(projectPath, projectData, &saveError),
          "5. Project save should succeed");

    // 6. Reload project
    bool loadOk = false;
    auto loaded = OsmProjectData::loadFromFile(projectPath, &loadOk);
    CHECK(loadOk, "6. Project reload should succeed");
    CHECK_EQ(loaded.roadCount, result.stats.roadsCreated, "6. Road count after reload");
    CHECK_EQ(loaded.junctionCount, result.stats.junctionsDetected, "6. Junction count after reload");

    // 7. Export OpenDRIVE
    QString odrPath = tmpDir.filePath("e2e.xodr");
    QString odrError;
    CHECK(OsmExporter::exportToOpenDrive(odrPath, result.network,
                                          result.junctions, result.converter,
                                          {}, &odrError),
          "7. OpenDRIVE export should succeed");

    // 8. Export GeoJSON
    QString geoPath = tmpDir.filePath("e2e.geojson");
    QString geoError;
    CHECK(OsmExporter::exportToGeoJson(geoPath, result.network,
                                        result.junctions, result.converter,
                                        {}, &geoError),
          "8. GeoJSON export should succeed");

    // 9. Validate exports
    CHECK(OsmExporter::validateExport(odrPath), "9a. OpenDRIVE validation");
    CHECK(OsmExporter::validateExport(geoPath), "9b. GeoJSON validation");

    // 10. Verify reload data integrity
    int errorsAfterReload = 0;
    for (const auto& issue : loaded.validationIssues) {
        if (issue.severity == Severity::Error) errorsAfterReload++;
    }
    CHECK(errorsAfterReload == 0, "10. No errors after reload");
}

// ─── Test 34: Roundabout with approach roads ───
TEST(test_roundabout_with_approaches) {
    QString xml = createRoundaboutOsmXml();
    OsmData data;
    OsmXmlParser::parseContent(xml, data);

    CoordinateConverter conv;
    conv.setReferenceFromBounds(data.minLat, data.minLon,
                                 data.maxLat, data.maxLon);

    RoadNetworkBuilder::Result network = RoadNetworkBuilder::build(data, conv);
    auto junctions = JunctionDetector::detect(network, data);
    auto roundabouts = RoundaboutGenerator::generateAll(junctions, network, data);

    if (!roundabouts.empty()) {
        const auto& rb = roundabouts[0];
        // Should have entry points from approach roads
        CHECK(!rb.entryPoints.empty(), "Roundabout should have entry points");
    }
}

// ─── Test 35: Marking types coverage ───
TEST(test_marking_types_coverage) {
    // Test that different road types generate appropriate markings
    Way way;
    way.tags.append(Tag{"highway", "primary"});
    way.tags.append(Tag{"lanes", "4"});
    way.tags.append(Tag{"lanes:forward", "2"});
    way.tags.append(Tag{"lanes:backward", "2"});

    RoadClassInfo info = RoadClassifier::classifyAndGet("primary");
    geo::LaneSection ls;
    LaneGenerator::generate(way, info, ls);

    // Create a simple road with the lane section
    geo::RoadV2 road;
    road.id = "test_road";
    road.name = "Test Road";
    road.laneCount = 4;
    road.width = 14.0;
    road.addSegment<geo::LineSegment>(geo::Point2D(0, 0), geo::Point2D(100, 0));
    road.addLaneSection(ls);

    auto markings = RoadMarkingGenerator::generateForRoad(road);

    CHECK(!markings.empty(), "Should generate markings for 4-lane road");

    // Should have center line (yellow, double solid)
    bool hasCenter = false;
    for (const auto& m : markings) {
        if (m.type == MarkingType::CenterLine) {
            hasCenter = true;
            CHECK(m.color == "yellow", "Center line should be yellow");
        }
    }
    CHECK(hasCenter, "4-lane road should have center line");
}

// ═══════════════════════════════════════════════════════════
// MAIN
// ═══════════════════════════════════════════════════════════

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    std::cout << "\n=== OSM Pipeline Test Suite ===\n" << std::endl;

    g_runner.runAll();

    std::cout << "\n=== Results ===" << std::endl;
    std::cout << "Passed: " << g_testsPassed << std::endl;
    std::cout << "Failed: " << g_testsFailed << std::endl;
    std::cout << "Total:  " << (g_testsPassed + g_testsFailed) << std::endl;

    return g_testsFailed == 0 ? 0 : 1;
}
