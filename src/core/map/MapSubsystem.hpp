#pragma once

// ============================================================
// Map Subsystem — QGIS-inspired map/coordinate infrastructure
// Provides: CRS, coordinate transforms, tile math, screen↔world,
//           tile caching, XYZ tile provider
// ============================================================

#include "MapRectangle.hpp"
#include "CoordinateReferenceSystem.hpp"
#include "CoordinateTransform.hpp"
#include "TileMatrix.hpp"
#include "MapToPixel.hpp"
#include "TileCache.hpp"
#include "XyzTileProvider.hpp"
