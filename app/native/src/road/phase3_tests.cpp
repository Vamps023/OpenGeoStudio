// ═══════════════════════════════════════════════════════════
// Phase 3 Unit Tests (doctest)
// ═══════════════════════════════════════════════════════════
//
// Tests for road_graph.hpp, lane_graph.hpp, junction_builder.hpp
//
// Run with: cl /std:c++20 /EHsc /Fe:phase3_tests.exe phase3_tests.cpp && phase3_tests.exe

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#define _USE_MATH_DEFINES
#include "doctest.h"

// Include Phase 3 headers (their tests are gated by DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
// which is already defined above, so we include them without the main)
#include "road_graph.hpp"
#include "lane_graph.hpp"
#include "junction_builder.hpp"
