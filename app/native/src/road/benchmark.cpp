// ═══════════════════════════════════════════════════════════
// Phase 1 Exit — Performance Benchmark
// ═══════════════════════════════════════════════════════════
//
// Measures adapter and sampling performance for 100/500/1000 segments.
// Records conversion time, sampling time, and segment counts.
//
// Build:
//   cl /std:c++20 /EHsc /Fe:benchmark.exe /I app\native\src /I app\native\src\road app\native\src\road\benchmark.cpp
//
// Run:
//   .\benchmark.exe
//

#include "road/geometry_segment.hpp"
#include "road/road_v2.hpp"
#include "road/road_adapter.hpp"
#include "road/road.hpp"
#include <chrono>
#include <cstdio>
#include <cmath>
#include <vector>

using namespace geo;
using Clock = std::chrono::high_resolution_clock;
using ms = std::chrono::duration<double, std::milli>;

// ─── Helper: create a legacy road with N line segments ───
static Road makeLineRoad(int numSegs) {
    Road road;
    road.id = "bench_line";
    road.formatVersion = 2;
    for (int i = 0; i <= numSegs; i++) {
        ControlPoint cp;
        cp.position = {i * 10.0, std::sin(i * 0.3) * 5.0};
        cp.type = "corner";
        road.points.push_back(cp);
    }
    return road;
}

// ─── Helper: create a legacy road with N bezier segments ───
static Road makeBezierRoad(int numSegs) {
    Road road;
    road.id = "bench_bez";
    road.formatVersion = 2;
    for (int i = 0; i < numSegs; i++) {
        double x0 = i * 20.0;
        ControlPoint cpStart;
        cpStart.position = {x0, 0};
        cpStart.type = "smooth";
        cpStart.handleOut = {10, 15};
        cpStart.hasHandleOut = true;
        road.points.push_back(cpStart);

        ControlPoint cpEnd;
        cpEnd.position = {x0 + 20, 0};
        cpEnd.type = "smooth";
        cpEnd.handleIn = {-10, 15};
        cpEnd.hasHandleIn = true;
        road.points.push_back(cpEnd);
    }
    return road;
}

// ─── Benchmark runner ───
struct BenchResult {
    double convertMs;
    double sampleMs;
    double reverseMs;
    int numSegments;
    double totalLength;
};

static BenchResult benchmark(const Road& road, int numSamples) {
    BenchResult r{};

    // Forward: Road → RoadV2
    auto t0 = Clock::now();
    AdapterReport report;
    RoadV2 v2 = roadToV2Auto(road, report);
    auto t1 = Clock::now();
    r.convertMs = ms(t1 - t0).count();
    r.numSegments = v2.numSegments();
    r.totalLength = v2.totalLength();

    // Sampling
    auto t2 = Clock::now();
    double sumX = 0;
    for (int i = 0; i < numSamples; i++) {
        double s = r.totalLength * static_cast<double>(i) / (numSamples - 1);
        Point2D p = v2.geometry().positionAt(s);
        sumX += p.x;
    }
    // Prevent optimization
    if (sumX < -1e18) printf("unlikely\n");
    auto t3 = Clock::now();
    r.sampleMs = ms(t3 - t2).count();

    // Reverse: RoadV2 → Road
    auto t4 = Clock::now();
    ReverseAdapterReport revReport;
    Road restored = roadFromV2(v2, revReport);
    auto t5 = Clock::now();
    r.reverseMs = ms(t5 - t4).count();

    // Prevent optimization
    if (restored.points.empty()) printf("unlikely\n");

    return r;
}

int main() {
    printf("═══════════════════════════════════════════════════════════\n");
    printf("Phase 1 Exit — Performance Benchmark\n");
    printf("═══════════════════════════════════════════════════════════\n");
    printf("\n");

    // Warmup
    {
        Road warmup = makeLineRoad(10);
        AdapterReport rpt;
        RoadV2 v2 = roadToV2Auto(warmup, rpt);
        ReverseAdapterReport rrpt;
        roadFromV2(v2, rrpt);
    }

    // ─── Line segments ───
    printf("─── Line Segments (exact path, formatVersion=2) ───\n");
    printf("%6s  %10s  %10s  %10s  %10s  %10s\n",
           "Segs", "Convert(ms)", "Sample(ms)", "Reverse(ms)", "TotalLen", "Samples");
    printf("%6s  %10s  %10s  %10s  %10s  %10s\n",
           "-----", "----------", "----------", "----------", "----------", "-------");

    for (int n : {100, 500, 1000}) {
        Road road = makeLineRoad(n);
        BenchResult r = benchmark(road, 1000);
        printf("%6d  %10.2f  %10.2f  %10.2f  %10.1f  %10d\n",
               n, r.convertMs, r.sampleMs, r.reverseMs, r.totalLength, 1000);
    }

    printf("\n");

    // ─── Bezier segments ───
    printf("─── Bezier Segments (exact path, formatVersion=2) ───\n");
    printf("%6s  %10s  %10s  %10s  %10s  %10s\n",
           "Segs", "Convert(ms)", "Sample(ms)", "Reverse(ms)", "TotalLen", "Samples");
    printf("%6s  %10s  %10s  %10s  %10s  %10s\n",
           "-----", "----------", "----------", "----------", "----------", "-------");

    for (int n : {100, 500, 1000}) {
        Road road = makeBezierRoad(n);
        BenchResult r = benchmark(road, 1000);
        printf("%6d  %10.2f  %10.2f  %10.2f  %10.1f  %10d\n",
               n, r.convertMs, r.sampleMs, r.reverseMs, r.totalLength, 1000);
    }

    printf("\n");

    // ─── Legacy path (formatVersion=1) ───
    printf("─── Line Segments (legacy path, formatVersion=1) ───\n");
    printf("%6s  %10s  %10s  %10s  %10s  %10s\n",
           "Segs", "Convert(ms)", "Sample(ms)", "Reverse(ms)", "TotalLen", "Samples");
    printf("%6s  %10s  %10s  %10s  %10s  %10s\n",
           "-----", "----------", "----------", "----------", "----------", "-------");

    for (int n : {100, 500, 1000}) {
        Road road = makeLineRoad(n);
        road.formatVersion = 1;  // Force legacy path
        BenchResult r = benchmark(road, 1000);
        printf("%6d  %10.2f  %10.2f  %10.2f  %10.1f  %10d\n",
               n, r.convertMs, r.sampleMs, r.reverseMs, r.totalLength, 1000);
    }

    printf("\n");
    printf("═══════════════════════════════════════════════════════════\n");
    printf("Benchmark complete.\n");
    printf("═══════════════════════════════════════════════════════════\n");
    return 0;
}
