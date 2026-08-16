# Road Engine Architecture — Python vs C++ vs TypeScript

## Current State

| Component | Language | Location | Performance |
|-----------|----------|----------|-------------|
| Road geometry (arcs, intersections, fillets) | TypeScript | `modules/road-studio/shared/types.ts` | OK for <100 roads |
| 2D rendering | TypeScript + MapLibre | `RoadViewport.tsx` | Good (GPU) |
| 3D rendering | TypeScript + Babylon.js | `RoadViewport.tsx` | Good (GPU) |
| State management | TypeScript + Zustand | `roadStudioStore.ts` | Good |
| UI | React + TypeScript | `RoadToolbar.tsx` etc. | Good |

**Current bottleneck:** Not the geometry math — it's the rendering and React re-renders. Road geometry (intersections, arcs, trimming) involves maybe 50-100 calculations per intersection. TypeScript handles this in <1ms. The real cost is re-rendering MapLibre layers and Babylon.js meshes.

---

## When Does Performance Actually Matter?

| Scenario | TS is fine | Need Python/C++ |
|----------|-----------|-----------------|
| <100 roads, manual editing | Yes | No |
| 1,000+ roads (city import) | Maybe slow | Yes |
| Real-time simulation (60fps AI) | No | Yes (C++) |
| OpenDRIVE export (10k roads) | Slow | Yes (Python) |
| Clothoid spline fitting | Slow | Yes (Python: scipy) |
| Mesh tessellation (100k triangles) | Slow | Yes (C++/WASM) |

**Conclusion:** For the interactive editor, TypeScript is fine. For import/export and simulation, a native backend helps.

---

## Option A: Python Sidecar (RECOMMENDED)

### Why Python?

1. **Shapely** — industry-standard 2D geometry (intersections, buffers, offsets)
2. **SciPy** — clothoid fitting, spline interpolation, optimization
3. **NumPy** — vectorized math for 1000+ roads
4. **NetworkX** — lane connectivity graph, pathfinding
5. **OpenDRIVE libs** — `pyodrx` exists (Python OpenDRIVE parser)
6. **Easy to write** — less code than C++, faster development
7. **Hot reload** — no compilation needed

### Architecture

```
┌─────────────────────────────────────────────┐
│  Electron App (existing)                     │
│                                              │
│  ┌─────────────┐    ┌─────────────────────┐ │
│  │  Renderer    │    │  Python Sidecar      │ │
│  │  (React+TS)  │    │  (FastAPI/ZeroMQ)    │ │
│  │              │    │                      │ │
│  │  - UI        │    │  - Road geometry     │ │
│  │  - MapLibre  │◄──►│  - Intersections     │ │
│  │  - Babylon   │IPC│  - Clothoids          │ │
│  │  - Zustand   │    │  - Lane graph        │ │
│  │              │    │  - OpenDRIVE I/O     │ │
│  │  - Lightweight│   │  - Mesh tessellation │ │
│  │    geometry   │    │  - OSM import        │ │
│  │    (preview)  │    │                      │ │
│  └─────────────┘    └─────────────────────┘ │
└─────────────────────────────────────────────┘
```

### IPC Protocol

Two options:

#### Option 1: ZeroMQ (low latency, ~0.1ms)
```python
# Python sidecar
import zmq, json
context = zmq.Context()
socket = context.socket(zmq.REP)
socket.bind("tcp://127.0.0.1:5555")

while True:
    msg = json.loads(socket.recv_string())
    if msg["type"] == "generateIntersection":
        result = generate_intersection(msg["road1"], msg["road2"])
        socket.send_string(json.dumps(result))
```

```typescript
// Electron main process
import { spawn } from 'child_process';
import zmq from 'zeromq';

// Start Python sidecar on app launch
const pythonProcess = spawn('python', ['road_engine/server.py']);
const socket = new zmq.Request();
await socket.connect('tcp://127.0.0.1:5555');

// IPC handler
ipcMain.handle('road:generateIntersection', async (e, road1, road2) => {
  await socket.send(JSON.stringify({ type: 'generateIntersection', road1, road2 }));
  const [reply] = await socket.receive();
  return JSON.parse(reply.toString());
});
```

#### Option 2: HTTP/FastAPI (simpler, ~1ms)
```python
from fastapi import FastAPI
from pydantic import BaseModel
app = FastAPI()

@app.post("/generateIntersection")
def gen_intersection(road1: Road, road2: Road):
    return generate_intersection(road1, road2)
```

### What Stays in TypeScript

- **UI / React components** — toolbar, panels, dialogs
- **MapLibre 2D rendering** — road polygons, markings
- **Babylon.js 3D rendering** — road meshes, textures
- **Zustand store** — UI state, selection, undo/redo
- **Lightweight preview geometry** — arc preview, snap points
- **Mouse/keyboard interaction** — click handling, dragging

### What Moves to Python

- **Complex geometry algorithms:**
  - `generateIntersection()` — edge-based polygon, fillet arcs
  - `computeCircleArc()` — circular arc with tangent continuity
  - `computeClothoid()` — clothoid spiral (Euler spiral) for highways
  - `trimRoad()` — split and trim at intersection boundary
  - `generateLaneConnections()` — lane connectivity graph
- **Import/Export:**
  - OSM → OpenDRIVE conversion
  - GeoJSON import/export
  - OpenDRIVE (.xodr) read/write
- **Heavy computation:**
  - Mesh tessellation (100k+ triangles)
  - Clothoid fitting (nonlinear optimization)
  - Road network graph analysis (Dijkstra, A*)
  - Batch intersection detection (1000+ roads)

### Python Project Structure

```
road_engine/
├── server.py                 # FastAPI or ZeroMQ server
├── requirements.txt
├── road_engine/
│   ├── __init__.py
│   ├── geometry/
│   │   ├── intersection.py   # Edge-based intersection generation
│   │   ├── arc.py            # Circular arc computation
│   │   ├── clothoid.py       # Clothoid spiral (Euler spiral)
│   │   ├── fillet.py         # Fillet arc between two lines
│   │   ├── offset.py         # Polyline offset (road edges)
│   │   └── trim.py           # Road splitting and trimming
│   ├── network/
│   │   ├── lane_graph.py     # Lane connectivity graph
│   │   ├── pathfinding.py    # Dijkstra, A* for route planning
│   │   └── connectivity.py   # Lane-to-lane connections
│   ├── import_export/
│   │   ├── osm_converter.py  # OSM → road network
│   │   ├── opendrive.py      # OpenDRIVE read/write
│   │   └── geojson.py        # GeoJSON import/export
│   ├── mesh/
│   │   ├── tessellate.py     # Polygon → triangle mesh
│   │   ├── markings.py       # Lane markings generation
│   │   └── sidewalks.py      # Sidewalk/curb generation
│   └── models/
│       ├── road.py           # Road data model
│       ├── intersection.py   # Intersection data model
│       └── junction.py       # Junction data model
└── tests/
    ├── test_intersection.py
    ├── test_arc.py
    └── test_clothoid.py
```

### Python Dependencies

```
# requirements.txt
shapely>=2.0          # 2D geometry: intersections, buffers, offsets
numpy>=1.24           # Vectorized math
scipy>=1.11           # Clothoid fitting, optimization
networkx>=3.1         # Lane connectivity graph
pydantic>=2.0         # Data models with validation
fastapi>=0.104        # HTTP server (if using HTTP IPC)
uvicorn>=0.24         # ASGI server
pyzmq>=25.1           # ZeroMQ (if using ZMQ IPC)
pyproj>=3.6           # Coordinate projections (UTM, etc.)
```

### Migration Plan (Incremental)

**Phase 1: Setup (1 day)**
1. Create `road_engine/` Python project
2. Set up FastAPI/ZeroMQ server
3. Add IPC handler in Electron main process
4. Test: ping/pong between Electron and Python

**Phase 2: Move intersection logic (2 days)**
1. Port `generateIntersection()` to Python using Shapely
2. Port `generateEdgeBasedPolygon()` — use `shapely.intersection()`
3. Port `filletArc()` — use `shapely.buffer()` for curves
4. Port `trimRoad()` — use `shapely.split()`
5. Test: same output as TypeScript version

**Phase 3: Add clothoid (1 day)**
1. Implement clothoid spiral in Python (scipy optimization)
2. Add clothoid tool to toolbar
3. Test: smooth transitions for highway design

**Phase 4: Move import/export (2 days)**
1. Port OSM → OpenDRIVE to Python
2. Port OpenDRIVE reader/writer
3. Add batch intersection detection
4. Test: import OSM file with 1000+ roads

**Phase 5: Mesh tessellation (1 day)**
1. Port mesh generation to Python (numpy + shapely)
2. Return triangle indices + vertices via IPC
3. Test: 100k triangle mesh generation

---

## Option B: C++ Native Addon (Maximum Performance)

### Why C++?

1. **Maximum speed** — 10-100x faster than Python
2. **WASM compilation** — can run in browser too
3. **Existing libraries** — CGAL, GEOS, Boost.Geometry
4. **Memory control** — no garbage collection pauses

### When to Choose C++ over Python?

- Real-time simulation (60fps AI vehicles)
- Mobile/embedded deployment
- Browser-only (no Python sidecar possible)
- Need <0.1ms per intersection

### Architecture

```
┌─────────────────────────────────────────────┐
│  Electron App                                │
│                                              │
│  ┌─────────────┐    ┌─────────────────────┐ │
│  │  Renderer    │    │  C++ Native Addon   │ │
│  │  (React+TS)  │    │  (node-addon-api)   │ │
│  │              │    │                      │ │
│  │  - UI        │    │  - Road geometry     │ │
│  │  - MapLibre  │◄──►│  - Intersection      │ │
│  │  - Babylon   │NAPI│  - Clothoid          │ │
│  │  - Zustand   │    │  - Mesh tessellation │ │
│  │              │    │  - Simulation        │ │
│  └─────────────┘    └─────────────────────┘ │
└─────────────────────────────────────────────┘
```

### C++ Project Structure

```
road_engine_cpp/
├── CMakeLists.txt
├── binding.gyp              # node-gyp config
├── src/
│   ├── addon.cc             # N-API bindings
│   ├── geometry/
│   │   ├── intersection.cpp
│   │   ├── arc.cpp
│   │   ├── clothoid.cpp
│   │   └── fillet.cpp
│   ├── mesh/
│   │   └── tessellate.cpp
│   └── network/
│       └── lane_graph.cpp
└── tests/
```

### C++ Dependencies

```cmake
# CMakeLists.txt
find_package(Boost REQUIRED COMPONENTS geometry)
find_package(CGAL REQUIRED)        # Computational geometry
# OR
find_package(GEOS REQUIRED)        # Geometry engine (same as Shapely)
```

### N-API Binding Example

```cpp
// src/addon.cc
#include <napi.h>
#include "geometry/intersection.hpp"

Napi::Value GenerateIntersection(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    // Parse road1 and road2 from JS objects
    Road road1 = parseRoad(info[0]);
    Road road2 = parseRoad(info[1]);
    
    // Compute intersection
    IntersectionResult result = generateIntersection(road1, road2);
    
    // Return as JS object
    return serializeResult(env, result);
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set("generateIntersection", Napi::Function::New(env, GenerateIntersection));
    return exports;
}

NODE_API_MODULE(road_engine, Init)
```

```typescript
// Use in Electron
import { generateIntersection } from '../../native/road_engine.node';

const result = generateIntersection(road1, road2);
// Synchronous, no IPC overhead
```

### C++ Pros/Cons

| Pros | Cons |
|------|------|
| 10-100x faster than Python | Complex build system (CMake + node-gyp) |
| No IPC overhead (in-process) | Harder to debug |
| Can compile to WASM | Slower development cycle |
| Memory efficient | No hot reload |
| Production-grade | Steep learning curve |

---

## Option C: WebAssembly (Rust or C++)

### Why WASM?

1. **Runs in browser** — no sidecar needed
2. **Near-native speed** — 2-5x slower than C++, 10x faster than JS
3. **Safe** — Rust is memory-safe
4. **Portable** — same code runs in Electron and browser

### Architecture

```
┌─────────────────────────────────────────────┐
│  Electron / Browser                          │
│                                              │
│  ┌─────────────┐    ┌─────────────────────┐ │
│  │  Renderer    │    │  WASM Module        │ │
│  │  (React+TS)  │    │  (Rust → WASM)      │ │
│  │              │    │                      │ │
│  │  - UI        │    │  - Road geometry     │ │
│  │  - MapLibre  │◄──►│  - Intersection      │ │
│  │  - Babylon   │WASM│  - Clothoid          │ │
│  │  - Zustand   │    │  - Mesh tessellation │ │
│  │              │    │                      │ │
│  └─────────────┘    └─────────────────────┘ │
└─────────────────────────────────────────────┘
```

### Rust Project Structure

```
road_engine_wasm/
├── Cargo.toml
├── src/
│   ├── lib.rs               # WASM exports
│   ├── geometry/
│   │   ├── intersection.rs
│   │   ├── arc.rs
│   │   ├── clothoid.rs
│   │   └── fillet.rs
│   └── mesh/
│       └── tessellate.rs
└── tests/
```

### Rust Example

```rust
// src/lib.rs
use wasm_bindgen::prelude::*;
use serde::{Serialize, Deserialize};

#[wasm_bindgen]
pub fn generate_intersection(road1_js: &JsValue, road2_js: &JsValue) -> JsValue {
    let road1: Road = road1_js.into_serde().unwrap();
    let road2: Road = road2_js.into_serde().unwrap();
    
    let result = geometry::generate_intersection(&road1, &road2);
    
    JsValue::from_serde(&result).unwrap()
}
```

```typescript
// Use in renderer
import { generateIntersection } from 'road-engine-wasm';

const result = generateIntersection(road1, road2);
// Synchronous, in-process, near-native speed
```

---

## Comparison Matrix

| Feature | TypeScript (current) | Python Sidecar | C++ Native | Rust/WASM |
|---------|---------------------|----------------|------------|-----------|
| **Performance** | 1x | 5-10x | 50-100x | 20-50x |
| **Development speed** | Fast | Fast | Slow | Medium |
| **Geometry libraries** | Basic | Excellent (Shapely) | Good (CGAL) | Growing (geo-rs) |
| **IPC overhead** | None | 0.1-1ms | None | None |
| **Hot reload** | Yes | Yes | No | No |
| **Browser support** | Yes | No | No | Yes |
| **Deployment** | Easy | Need Python | Need compilation | Need compilation |
| **Maintainability** | High | High | Low | Medium |
| **Clothoid support** | Manual | scipy.optimize | Manual | Manual |
| **OpenDRIVE libs** | Custom | pyodrx exists | Custom | Custom |
| **OSM import** | Custom | osmnx exists | Custom | Custom |
| **Team skills needed** | TypeScript | + Python | + C++ | + Rust |

---

## My Recommendation

### For your project (Road Studio in Electron):

**Phase 1 (Now): Keep TypeScript for interactive editing**
- The interactive editor (drawing, dragging, preview) needs <1ms response
- TypeScript is fine for this — the bottleneck is rendering, not geometry
- Keep the current architecture

**Phase 2 (Next): Add Python sidecar for heavy computation**
- Move intersection generation, clothoid fitting, and import/export to Python
- Use Shapely for geometry (much better than hand-written TS)
- Use scipy for clothoid optimization
- Use ZeroMQ for IPC (0.1ms latency)

**Phase 3 (Future): Add C++/WASM only if needed**
- Only if you add real-time simulation (60fps AI vehicles)
- Only if you need to process 10,000+ roads in real-time
- Only if Python sidecar proves too slow

### Why not C++ now?

1. **Overkill** — road geometry is not computationally intensive
2. **Complex build** — CMake + node-gyp + cross-compilation is painful
3. **Slow development** — no hot reload, harder to debug
4. **Shapely doesn't exist in C++** — CGAL is harder to use

### Why not Rust/WASM now?

1. **Learning curve** — team needs Rust skills
2. **Ecosystem smaller** — fewer geometry libraries than Python
3. **No Shapely equivalent** — `geo-rs` is still maturing

### Why Python is the sweet spot:

1. **Shapely is industry-standard** — used by GIS professionals worldwide
2. **scipy for clothoids** — nonlinear optimization out of the box
3. **Fast development** — hot reload, easy debugging
4. **Good enough performance** — 5-10x faster than TS for geometry
5. **Easy deployment** — bundle Python with PyInstaller or use conda
6. **Existing ecosystem** — osmnx, pyodrx, networkx, pyproj

---

## Implementation Roadmap

### Step 1: Create Python Road Engine (Week 1)

```bash
mkdir road_engine
cd road_engine
python -m venv venv
source venv/bin/activate  # or venv\Scripts\activate on Windows
pip install shapely numpy scipy networkx pyzmq pydantic
```

### Step 2: Implement Core Geometry (Week 1-2)

```python
# road_engine/geometry/intersection.py
from shapely.geometry import LineString, Polygon, Point
from shapely.ops import split, unary_union
import numpy as np

def generate_intersection(road1: dict, road2: dict) -> dict:
    """
    Edge-based intersection generation using Shapely.
    
    1. Create road centerlines as LineString
    2. Buffer to create road polygons (edges)
    3. Find intersection of road polygons
    4. Compute corner fillets with circular arcs
    5. Return intersection polygon + approaches + lane connections
    """
    # Convert to Shapely geometries
    cl1 = LineString([(p['x'], p['y']) for p in road1['centerline']])
    cl2 = LineString([(p['x'], p['y']) for p in road2['centerline']])
    
    # Buffer to road width
    poly1 = cl1.buffer(road1['width'] / 2, cap_style='flat')
    poly2 = cl2.buffer(road2['width'] / 2, cap_style='flat')
    
    # Intersection polygon
    intersection_poly = poly1.intersection(poly2)
    
    # Find centerline crossing
    center = cl1.intersection(cl2)
    
    # Compute trim distance from angle
    angle = compute_angle_between(cl1, cl2, center)
    trim_dist = (road2['width'] / 2) + 5 * np.tan(angle / 2)
    
    # Trim roads at trim distance
    trimmed1 = trim_road(cl1, center, trim_dist)
    trimmed2 = trim_road(cl2, center, trim_dist)
    
    # Generate corner fillets
    corners = compute_corner_fillets(trimmed1, trimmed2, radius=5)
    
    # Build final polygon
    polygon = build_polygon_from_edges(trimmed1, trimmed2, corners)
    
    return {
        'center': {'x': center.x, 'y': center.y},
        'polygon': list(polygon.exterior.coords),
        'approaches': [...],
        'laneConnections': [...],
    }
```

### Step 3: Implement Clothoid (Week 2)

```python
# road_engine/geometry/clothoid.py
from scipy.optimize import minimize
import numpy as np

def compute_clothoid(start_point, start_direction, end_point, end_direction):
    """
    Compute a clothoid (Euler spiral) between two points with
    tangent continuity at both ends.
    
    A clothoid has linearly varying curvature:
    κ(s) = κ0 + s/R²
    
    where s is arc length, κ0 is initial curvature, R is clothoid parameter.
    """
    # Optimize clothoid parameters to match endpoints
    def objective(params):
        A, L = params  # clothoid parameter, length
        # Simulate clothoid
        points = simulate_clothoid(start_point, start_direction, A, L)
        # Error = distance to end point + direction
        pos_error = np.sum((points[-1] - np.array(end_point))**2)
        dir_error = (points[-1] - points[-2]) - np.array(end_direction)
        return pos_error + np.sum(dir_error**2)
    
    result = minimize(objective, x0=[50, 100], method='Nelder-Mead')
    A, L = result.x
    
    return simulate_clothoid(start_point, start_direction, A, L)
```

### Step 4: Set up IPC (Week 2)

```python
# road_engine/server.py
import zmq, json
from geometry.intersection import generate_intersection
from geometry.clothoid import compute_clothoid

context = zmq.Context()
socket = context.socket(zmq.REP)
socket.bind("tcp://127.0.0.1:5555")

print("Road Engine listening on port 5555...")

while True:
    msg = json.loads(socket.recv_string())
    msg_type = msg["type"]
    
    if msg_type == "generateIntersection":
        result = generate_intersection(msg["road1"], msg["road2"])
        socket.send_string(json.dumps(result))
    
    elif msg_type == "computeClothoid":
        result = compute_clothoid(
            msg["startPoint"], msg["startDirection"],
            msg["endPoint"], msg["endDirection"]
        )
        socket.send_string(json.dumps(result))
    
    elif msg_type == "ping":
        socket.send_string(json.dumps({"status": "ok"}))
```

### Step 5: Add IPC Handler in Electron (Week 2)

```typescript
// app/handlers/roadEngineHandler.ts
import { spawn } from 'child_process';
import * as path from 'path';
import zmq from 'zeromq';

let pythonProcess: any = null;
let socket: any = null;

export function startRoadEngine() {
  const enginePath = path.join(__dirname, '../../road_engine/server.py');
  pythonProcess = spawn('python', [enginePath]);
  
  pythonProcess.stdout.on('data', (data) => {
    console.log(`[Road Engine] ${data}`);
  });
  
  // Connect ZeroMQ socket
  socket = new zmq.Request();
  socket.connect('tcp://127.0.0.1:5555');
  
  console.log('Road Engine started');
}

export async function callRoadEngine(type: string, data: any): Promise<any> {
  if (!socket) throw new Error('Road Engine not started');
  await socket.send(JSON.stringify({ type, ...data }));
  const [reply] = await socket.receive();
  return JSON.parse(reply.toString());
}

// IPC handlers
ipcMain.handle('road:generateIntersection', async (e, road1, road2) => {
  return callRoadEngine('generateIntersection', { road1, road2 });
});

ipcMain.handle('road:computeClothoid', async (e, ...args) => {
  return callRoadEngine('computeClothoid', { ...args });
});
```

### Step 6: Use in Renderer (Week 3)

```typescript
// modules/road-studio/client/store/roadStudioStore.ts

// Before (TypeScript):
const generated = generateIntersection(road1, road2, refLat, refLon);

// After (Python via IPC):
const generated = await window.roadEngine.generateIntersection(road1, road2);
```

### Step 7: Bundle Python with Electron (Week 3)

```bash
# Package Python as standalone executable
cd road_engine
pyinstaller --onefile server.py
# Creates dist/server.exe

# Copy to Electron resources
cp dist/server.exe ../resources/road_engine/

# Electron packages it automatically
```

---

## Performance Benchmarks (Estimated)

| Operation | TypeScript | Python (Shapely) | C++ (CGAL) |
|-----------|-----------|------------------|------------|
| 1 intersection | 0.5ms | 0.3ms + 0.1ms IPC | 0.05ms |
| 100 intersections | 50ms | 30ms + 10ms IPC | 5ms |
| 1000 intersections | 500ms | 300ms + 100ms IPC | 50ms |
| Clothoid fit | 100ms (manual) | 5ms (scipy) | 1ms |
| Mesh tessellation (10k tri) | 50ms | 20ms (numpy) | 2ms |
| OSM import (10k roads) | 5000ms | 500ms (osmnx) | 100ms |

**Key insight:** For interactive editing (1 intersection at a time), the IPC overhead (0.1ms) is negligible. Python with Shapely is actually **faster** than the current TypeScript code because Shapely is a C library under the hood.

---

## Summary

| Question | Answer |
|----------|--------|
| Should we switch to Python? | **Yes, for heavy computation** (intersections, clothoids, import/export) |
| Should we switch to C++? | **Not yet** — only if you need real-time simulation |
| Should we keep TypeScript? | **Yes, for UI and rendering** — it's the right tool for that |
| What's the best architecture? | **Hybrid: TypeScript UI + Python sidecar** |
| How long does migration take? | **2-3 weeks** (incremental, not big-bang) |
| Is it worth it? | **Yes** — Shapely alone saves 1000+ lines of geometry code |
