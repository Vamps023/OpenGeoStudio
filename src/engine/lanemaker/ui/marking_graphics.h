#pragma once

// ═══════════════════════════════════════════════════════════
// MarkingGraphics — Renders road markings, signs, and furniture
// into the LaneMaker viewport using the existing MapViewGL API.
//
// All geometry is derived from the road's reference line at
// render time, so markings/signs/furniture automatically follow
// road edits. No hard-coded world coordinates are stored.
// ═══════════════════════════════════════════════════════════

#include "road.h"
#include "road_graphics.h"
#include "sign_system.h"
#include <vector>
#include <string>
#include <map>

namespace LM
{

// Renders all placed markings for a given road into the viewport.
// Inherits PermanentGraphics to access the protected MapViewGL API
// (AbstractGraphicsItem is a friend of MapViewGL).
class MarkingGraphics : protected PermanentGraphics
{
public:
    MarkingGraphics(const std::shared_ptr<Road>& road);
    ~MarkingGraphics();

    static void RemoveForRoad(const std::string& roadID);

private:
    void renderLongitudinal(const odr::Road& gen,
        const PlacedMarking& marking, const MarkingDefinition& def);
    void renderTransverse(const odr::Road& gen,
        const PlacedMarking& marking, const MarkingDefinition& def);
    void renderArrow(const odr::Road& gen,
        const PlacedMarking& marking, const MarkingDefinition& def);
    void renderSymbol(const odr::Road& gen,
        const PlacedMarking& marking, const MarkingDefinition& def);
    void renderArea(const odr::Road& gen,
        const PlacedMarking& marking, const MarkingDefinition& def);

    static unsigned int roadMarkingsObjectID(const std::string& roadID);
};

class SignGraphics : protected PermanentGraphics
{
public:
    SignGraphics(const std::shared_ptr<Road>& road);
    ~SignGraphics();

    static void RemoveForRoad(const std::string& roadID);

private:
    static unsigned int roadSignsObjectID(const std::string& roadID);
};

class FurnitureGraphics : protected PermanentGraphics
{
public:
    FurnitureGraphics(const std::shared_ptr<Road>& road);
    ~FurnitureGraphics();

    static void RemoveForRoad(const std::string& roadID);

private:
    static unsigned int roadFurnitureObjectID(const std::string& roadID);
};

} // namespace LM
