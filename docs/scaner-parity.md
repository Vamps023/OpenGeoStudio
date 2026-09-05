# SCANeR Parity Checklist

Status vs the SCANeRstudio 2026 Terrain + Simulation docs
(C:/AVSimulation/SCANeRstudio_2026/doc/help/html). DONE = implemented AND regression-covered.

## Terrain mode (5.5)

| Doc | Feature | Status |
|---|---|---|
| 5.5.1 | Network init: location, projection, georeference, background map | DONE |
| 5.5.2.1 | Altitude: handles, stick to terrain, banking, smoothing | PARTIAL - stick+banking+smoothing DONE; topography/picture sources missing |
| 5.5.4.1-2 | Roads: insert curves (segment/arc/clothoid/polyline/bezier/spline), constraints, converts | DONE |
| 5.5.4.3 | Functions/Portions/Tracks: merge, stick, link, bind, split, orientation | DONE |
| 5.5.4.4 | Intersections: insert, detect, contours, ways, authorizations, passageways | DONE |
| 5.5.4.5 | Bifurcations: extract ways, create interchange | DONE |
| 5.5.5.1 | Lane editing: split, begin/end lane tapers, borders, potholes | PARTIAL (tapers + borders DONE, potholes missing) |
| 5.5.5.2 | Lane border editing + sidewalk | DONE |
| 5.5.5.3 | Portions: bridge/tunnel, lengths, centers | DONE |
| 5.5.5.4 | Profiles / road style libraries | DONE |
| 5.5.6 | Composite objects | MISSING |
| 5.5.7 | Topography | MISSING |
| 5.5.8 | Procedural generation | MISSING |
| RoadStyleEditor | Marking / road style editor | PARTIAL (green centre, white lane marks, style library) |
| Import | OpenDRIVE, OSM, ShapeFile, CSV, ... | OpenDRIVE DONE; others MISSING |
| Export | OpenDRIVE, 3D database, Sherpa | OpenDRIVE DONE; 3D DB / Sherpa MISSING |

## Simulation mode (2.x)

| Feature | Status |
|---|---|
| Runtime: play/pause/speed, time step | DONE (traffic playback in 3D Studio) |
| Vehicles driving on lanes (surrogate models) | DONE |
| Scenario management (create/save/reopen) | DONE |
| Storyboard / scripting | MISSING |
| KPIs / results database | MISSING |
| Multiplayer | MISSING |
| Sensors | MISSING |
