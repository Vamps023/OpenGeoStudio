# Type Mappings: RoadEngine C++ ↔ JavaScript/TypeScript

## Overview
This document describes the type translations between the RoadEngine C++ core and the JavaScript/TypeScript bindings used by OpenGeoStudio via N-API.

## Geometry Types

### Point2D / Vec2
| C++ Field | JS Field | Type | Notes |
|-----------|----------|------|-------|
| `double x` | `x` | number | X coordinate in local meters |
| `double y` | `y` | number | Y coordinate in local meters |

**Ownership**: C++ owns the struct; JS receives a copy via Napi::Object. No shared memory.

### Point3D
| C++ Field | JS Field | Type | Notes |
|-----------|----------|------|-------|
| `double x` | `x` | number | X coordinate in local meters |
| `double y` | `y` | number | Y coordinate in local meters |
| `double z` | `z` | number | Z elevation in meters |

## Road Model Types

### ControlPoint
| C++ Field | JS Field | Type | Notes |
|-----------|----------|------|-------|
| `Point2D position` | `lat`, `lon` | number | Geo coordinates (converted to local meters in C++) |
| `double z` | `z` | number | Elevation in meters |
| `Vec2* handleIn` | `handleIn.lat`, `handleIn.lon` | number? | Optional bezier handle |
| `Vec2* handleOut` | `handleOut.lat`, `handleOut.lon` | number? | Optional bezier handle |
| `std::string type` | `type` | string | "smooth" or "corner" |
| `std::string id` | `id` | string | Unique control point ID |
| `SegmentMetadata* segmentMeta` | `segmentMeta` | object? | Optional segment metadata |

### SegmentMetadata
| C++ Field | JS Field | Type | Notes |
|-----------|----------|------|-------|
| `SegmentKind kind` | `kind` | string | "line", "arc", "spiral", "bezier" |
| `int version` | `version` | number | Metadata format version |
| `double startHeading` | `startHeading` | number | Heading in radians |
| `double curvature` | `curvature` | number | Arc curvature (1/radius) |
| `double arcLength` | `arcLength` | number | Arc length in meters |
| `double curvatureStart` | `curvatureStart` | number | Spiral start curvature |
| `double curvatureEnd` | `curvatureEnd` | number | Spiral end curvature |
| `double segmentLength` | `segmentLength` | number | Segment length in meters |

### Road
| C++ Field | JS Field | Type | Notes |
|-----------|----------|------|-------|
| `std::string id` | `id` | string | Unique road ID |
| `std::string name` | `name` | string | Display name |
| `std::vector<ControlPoint> points` | `points` | ControlPoint[] | Array of control points |
| `double width` | `width` | number | Road width in meters |
| `int laneCount` | `laneCount` | number | Number of lanes |
| `std::string profileName` | `profile` | string | Road profile name |
| `int formatVersion` | `formatVersion` | number | 1=legacy, 2=with metadata |

## Mesh Types

### MeshData
| C++ Field | JS Field | Type | Notes |
|-----------|----------|------|-------|
| `std::vector<float> vertices` | `vertices` | Float32Array | 3 floats per vertex (x,y,z) |
| `std::vector<float> normals` | `normals` | Float32Array | 3 floats per vertex (nx,ny,nz) |
| `std::vector<float> uvs` | `uvs` | Float32Array | 2 floats per vertex (u,v) |
| `std::vector<uint32_t> indices` | `indices` | Uint32Array | Triangle indices |

**Ownership**: C++ creates Napi::Float32Array/Uint32Array which copies data. JS owns the typed arrays after creation.

## Error Code Ranges

| Range | Category | N-API Error Type |
|-------|----------|-----------------|
| 1000-1999 | Geometry errors | Napi::Error |
| 2000-2999 | Parsing errors | Napi::SyntaxError |
| 3000-3999 | Serialization errors | Napi::RangeError |
| 4000-4999 | Mesh generation errors | Napi::Error |
| 5000-5999 | I/O errors | Napi::Error |

## Memory Ownership Semantics

- **Value types** (Point2D, Vec2, ControlPoint, Road): Copied across the boundary. No shared memory.
- **Container types** (std::vector, MeshData): Copied into Napi::TypedArrays. JS owns the copy.
- **Strings** (std::string): Copied into Napi::String. JS owns the copy.
- **No shared pointers**: The binding layer does not use shared_ptr or raw pointers across the boundary.
