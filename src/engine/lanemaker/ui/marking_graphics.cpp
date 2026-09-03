#include "marking_graphics.h"
#include "map_view_gl.h"
#include "world.h"
#include "constants.h"
#include "road_curvature.h"

#include <cmath>

namespace LM
{

// ─── Helpers ───────────────────────────────────────────────

static QColor colorFromString(const QString& name)
{
    if (name == "yellow") return Qt::yellow;
    if (name == "white")  return Qt::white;
    if (name == "red")    return Qt::red;
    if (name == "blue")   return Qt::blue;
    if (name == "green")  return Qt::green;
    if (name == "gray" || name == "grey") return Qt::gray;
    if (name == "silver") return QColor(192, 192, 192);
    if (name == "brown")  return QColor(139, 69, 19);
    return Qt::white;
}

// ═══════════════════════════════════════════════════════════
// MarkingGraphics
// ═══════════════════════════════════════════════════════════

unsigned int MarkingGraphics::roadMarkingsObjectID(const std::string& roadID)
{
    if (roadID.empty()) return 900000;
    try {
        return static_cast<unsigned int>(std::stoul(roadID)) + 900000;
    } catch (...) {
        // For nonnumeric road IDs, use a hash to avoid collisions
        // between different roads. Use FNV-1a for simplicity.
        unsigned int hash = 2166136261u;
        for (char c : roadID) {
            hash ^= static_cast<unsigned char>(c);
            hash *= 16777619u;
        }
        // Map to range [900000, 910000) to avoid collision with numeric IDs
        return 900000 + (hash % 100000);
    }
}

MarkingGraphics::MarkingGraphics(const std::shared_ptr<Road>& road)
    : PermanentGraphics(roadMarkingsObjectID(road->ID()))
{
#ifndef G_TEST
    if (!g_mapViewGL) return;
    if (!road) return;

    const std::string roadID = road->ID();
    auto* reg = MarkingRegistry::Instance();
    const auto& markings = reg->placedMarkings();
    const odr::Road& gen = road->generated;

    for (const auto& marking : markings)
    {
        if (marking.roadID != roadID) continue;
        const auto* def = reg->get(marking.type);
        if (!def) continue;

        if (def->category == "line" || (def->isLongitudinal && def->category != "area"))
            renderLongitudinal(gen, marking, *def);
        else if (def->category == "transverse")
            renderTransverse(gen, marking, *def);
        else if (def->category == "arrow")
            renderArrow(gen, marking, *def);
        else if (def->category == "symbol")
            renderSymbol(gen, marking, *def);
        else if (def->category == "area")
            renderArea(gen, marking, *def);
    }
#endif
}

MarkingGraphics::~MarkingGraphics()
{
    // PermanentGraphics destructor handles cleanup via Clear()
}

void MarkingGraphics::RemoveForRoad(const std::string& roadID)
{
#ifndef G_TEST
    if (g_mapViewGL)
        g_mapViewGL->RemoveObject(roadMarkingsObjectID(roadID));
#endif
}

void MarkingGraphics::renderLongitudinal(const odr::Road& gen,
    const PlacedMarking& marking, const MarkingDefinition& def)
{
#ifndef G_TEST
    if (!g_mapViewGL) return;

    QColor color = colorFromString(marking.color);
    double sStart = std::max(0.0, marking.sStart);
    double sEnd = std::min(gen.ref_line.length, marking.sEnd);
    if (sEnd <= sStart) return;

    double width = marking.width;
    double tOffset = marking.tOffset;
    double halfW = width / 2.0;

    auto buildLine = [&](double sA, double sB, double tCenter, double w) {
        odr::Line3D left, right;
        double hw = w / 2.0;
        for (double s = sA; s <= sB; s += 0.5)
        {
            left.push_back(gen.get_xyz(s, tCenter - hw, 0.02));
            right.push_back(gen.get_xyz(s, tCenter + hw, 0.02));
        }
        left.push_back(gen.get_xyz(sB, tCenter - hw, 0.02));
        right.push_back(gen.get_xyz(sB, tCenter + hw, 0.02));
        if (left.size() > 1)
            AddQuads(left, right, color);
    };

    if (def.pattern == MarkingPattern::Dashed)
    {
        double dashLen = def.dashLength > 0 ? def.dashLength : 3.0;
        double gapLen = def.gapLength > 0 ? def.gapLength : 6.0;
        double cycle = dashLen + gapLen;
        for (double s = sStart; s < sEnd; s += cycle)
        {
            double segEnd = std::min(s + dashLen, sEnd);
            if (segEnd > s) buildLine(s, segEnd, tOffset, width);
        }
    }
    else if (def.pattern == MarkingPattern::DoubleLine)
    {
        double spacing = width * 1.5;
        buildLine(sStart, sEnd, tOffset - spacing, width * 0.5);
        buildLine(sStart, sEnd, tOffset + spacing, width * 0.5);
    }
    else if (def.pattern == MarkingPattern::SolidDashed)
    {
        double spacing = width * 1.5;
        buildLine(sStart, sEnd, tOffset - spacing, width * 0.5);
        double dashLen = def.dashLength > 0 ? def.dashLength : 3.0;
        double gapLen = def.gapLength > 0 ? def.gapLength : 6.0;
        for (double s = sStart; s < sEnd; s += dashLen + gapLen)
        {
            double segEnd = std::min(s + dashLen, sEnd);
            if (segEnd > s) buildLine(s, segEnd, tOffset + spacing, width * 0.5);
        }
    }
    else if (def.pattern == MarkingPattern::DashedSolid)
    {
        double spacing = width * 1.5;
        buildLine(sStart, sEnd, tOffset + spacing, width * 0.5);
        double dashLen = def.dashLength > 0 ? def.dashLength : 3.0;
        double gapLen = def.gapLength > 0 ? def.gapLength : 6.0;
        for (double s = sStart; s < sEnd; s += dashLen + gapLen)
        {
            double segEnd = std::min(s + dashLen, sEnd);
            if (segEnd > s) buildLine(s, segEnd, tOffset - spacing, width * 0.5);
        }
    }
    else // Continuous
    {
        buildLine(sStart, sEnd, tOffset, width);
    }
#endif
}

void MarkingGraphics::renderTransverse(const odr::Road& gen,
    const PlacedMarking& marking, const MarkingDefinition& def)
{
#ifndef G_TEST
    if (!g_mapViewGL) return;

    QColor color = colorFromString(marking.color);
    double s = marking.sStart;
    if (s < 0 || s > gen.ref_line.length) return;

    double halfWidth = marking.width / 2.0;
    double roadHalfWidth = 7.0;

    auto buildCross = [&](double sCenter, double w) {
        odr::Line3D left, right;
        for (double t = -roadHalfWidth; t <= roadHalfWidth; t += 1.0)
        {
            left.push_back(gen.get_xyz(sCenter - w/2, t, 0.02));
            right.push_back(gen.get_xyz(sCenter + w/2, t, 0.02));
        }
        left.push_back(gen.get_xyz(sCenter - w/2, roadHalfWidth, 0.02));
        right.push_back(gen.get_xyz(sCenter + w/2, roadHalfWidth, 0.02));
        if (left.size() > 1)
            AddQuads(left, right, color);
    };

    if (def.type == MarkingType::ZebraCrossing)
    {
        double stripeW = 0.4, gap = 0.4;
        for (double ds = 0; ds < 4.0; ds += stripeW + gap)
            buildCross(s + ds, stripeW);
    }
    else if (def.type == MarkingType::Crosswalk || def.type == MarkingType::BicycleCrossing)
    {
        buildCross(s - 1.5, marking.width);
        buildCross(s + 1.5, marking.width);
    }
    else // StopLine, YieldLine, generic
    {
        buildCross(s, marking.width);
    }
#endif
}

void MarkingGraphics::renderArrow(const odr::Road& gen,
    const PlacedMarking& marking, const MarkingDefinition& def)
{
#ifndef G_TEST
    if (!g_mapViewGL) return;

    QColor color = colorFromString(marking.color);
    double s = marking.sStart;
    if (s < 0 || s > gen.ref_line.length) return;

    double t = marking.tOffset;
    auto tp = [&](double ds, double dt) -> odr::Vec3D {
        double ws = s + ds, wt = t + dt;
        if (ws < 0) ws = 0;
        if (ws > gen.ref_line.length) ws = gen.ref_line.length;
        return gen.get_xyz(ws, wt, 0.03);
    };

    odr::Line3D poly;
    switch (def.type)
    {
    case MarkingType::ArrowStraight:
        poly = { tp(-1.5,-0.15), tp(1.0,-0.15), tp(1.0,-0.4), tp(2.0,0),
                 tp(1.0,0.4), tp(1.0,0.15), tp(-1.5,0.15) };
        break;
    case MarkingType::ArrowLeft:
        poly = { tp(-1.5,-0.15), tp(0.5,-0.15), tp(0.5,-0.5), tp(1.5,-0.5),
                 tp(1.5,0.5), tp(0.5,0.5), tp(0.5,0.15), tp(-1.5,0.15) };
        break;
    case MarkingType::ArrowRight:
        poly = { tp(-1.5,0.15), tp(0.5,0.15), tp(0.5,0.5), tp(1.5,0.5),
                 tp(1.5,-0.5), tp(0.5,-0.5), tp(0.5,-0.15), tp(-1.5,-0.15) };
        break;
    case MarkingType::ArrowStraightLeft:
        poly = { tp(-1.5,-0.15), tp(1.0,-0.15), tp(1.0,-0.4), tp(2.0,0),
                 tp(1.0,0.4), tp(1.0,0.15), tp(-1.5,0.15) };
        break;
    case MarkingType::ArrowStraightRight:
        poly = { tp(-1.5,-0.15), tp(1.0,-0.15), tp(1.0,-0.4), tp(2.0,0),
                 tp(1.0,0.4), tp(1.0,0.15), tp(-1.5,0.15) };
        break;
    case MarkingType::ArrowUTurn:
        poly = { tp(-1.0,-0.15), tp(1.0,-0.15), tp(1.0,-0.5), tp(1.5,-0.5),
                 tp(1.5,0.5), tp(0.5,0.5), tp(0.5,0.15), tp(-1.0,0.15) };
        break;
    case MarkingType::ArrowMerge:
        poly = { tp(-1.5,-0.15), tp(0.5,-0.15), tp(1.5,-0.5), tp(2.0,-0.5),
                 tp(2.0,0.5), tp(1.5,0.5), tp(0.5,0.15), tp(-1.5,0.15) };
        break;
    case MarkingType::ArrowDiverge:
        poly = { tp(-2.0,-0.5), tp(-1.5,-0.5), tp(-0.5,-0.15), tp(1.5,-0.15),
                 tp(1.5,0.15), tp(-0.5,0.15), tp(-1.5,0.5), tp(-2.0,0.5) };
        break;
    default: return;
    }
    if (poly.size() >= 3)
        AddPoly(poly, color);
#endif
}

void MarkingGraphics::renderSymbol(const odr::Road& gen,
    const PlacedMarking& marking, const MarkingDefinition& def)
{
#ifndef G_TEST
    if (!g_mapViewGL) return;

    QColor color = colorFromString(marking.color);
    double s = marking.sStart;
    if (s < 0 || s > gen.ref_line.length) return;

    double t = marking.tOffset;
    auto tp = [&](double ds, double dt) -> odr::Vec3D {
        return gen.get_xyz(s + ds, t + dt, 0.03);
    };

    odr::Line3D poly;
    switch (def.type)
    {
    case MarkingType::SymbolBus:
        poly = { tp(-0.8,-0.4), tp(0.8,-0.4), tp(0.8,0.4), tp(-0.8,0.4) };
        break;
    case MarkingType::SymbolBicycle:
        {
            odr::Line3D p1 = { tp(-0.8,-0.3), tp(-0.4,-0.3), tp(-0.4,0.3), tp(-0.8,0.3) };
            if (p1.size() >= 3) AddPoly(p1, color);
            poly = { tp(0.4,-0.3), tp(0.8,-0.3), tp(0.8,0.3), tp(0.4,0.3) };
        }
        break;
    case MarkingType::SymbolAccessibility:
        poly = { tp(-0.5,-0.5), tp(0.5,-0.5), tp(0.5,0.5), tp(-0.5,0.5) };
        break;
    case MarkingType::SymbolParking:
        poly = { tp(-0.4,-0.5), tp(0.4,-0.5), tp(0.4,0.5), tp(-0.4,0.5) };
        break;
    default: return;
    }
    if (poly.size() >= 3)
        AddPoly(poly, color);
#endif
}

void MarkingGraphics::renderArea(const odr::Road& gen,
    const PlacedMarking& marking, const MarkingDefinition& def)
{
#ifndef G_TEST
    if (!g_mapViewGL) return;

    QColor color = colorFromString(marking.color);
    double sStart = std::max(0.0, marking.sStart);
    double sEnd = std::min(gen.ref_line.length, marking.sEnd);
    if (sEnd <= sStart) return;

    double tCenter = marking.tOffset;
    double halfW = marking.width / 2.0;

    auto buildStrip = [&](double sA, double sB, double tC, double w) {
        odr::Line3D left, right;
        double hw = w / 2.0;
        for (double s = sA; s <= sB; s += 0.5)
        {
            left.push_back(gen.get_xyz(s, tC - hw, 0.02));
            right.push_back(gen.get_xyz(s, tC + hw, 0.02));
        }
        left.push_back(gen.get_xyz(sB, tC - hw, 0.02));
        right.push_back(gen.get_xyz(sB, tC + hw, 0.02));
        if (left.size() > 1)
            AddQuads(left, right, color);
    };

    if (def.type == MarkingType::HatchedArea || def.type == MarkingType::GoreArea)
    {
        double stripeLen = 1.0, gap = 1.5;
        for (double s = sStart; s < sEnd; s += stripeLen + gap)
        {
            double segEnd = std::min(s + stripeLen, sEnd);
            if (segEnd > s) buildStrip(s, segEnd, tCenter, marking.width);
        }
    }
    else if (def.type == MarkingType::ChevronArea)
    {
        double chevSpacing = 2.0;
        for (double s = sStart; s < sEnd; s += chevSpacing)
        {
            odr::Line3D poly = {
                gen.get_xyz(s, tCenter - halfW, 0.02),
                gen.get_xyz(s + 1.0, tCenter, 0.02),
                gen.get_xyz(s, tCenter + halfW, 0.02)
            };
            if (poly.size() >= 3) AddPoly(poly, color);
        }
    }
    else if (def.type == MarkingType::ParkingBay || def.type == MarkingType::BusStopMarking)
    {
        double dashLen = def.dashLength > 0 ? def.dashLength : 1.0;
        double gapLen = def.gapLength > 0 ? def.gapLength : 1.0;
        for (double tSide : {-halfW, halfW})
        {
            for (double s = sStart; s < sEnd; s += dashLen + gapLen)
            {
                double segEnd = std::min(s + dashLen, sEnd);
                if (segEnd > s) buildStrip(s, segEnd, tCenter + tSide, 0.1);
            }
        }
    }
    else
    {
        buildStrip(sStart, sEnd, tCenter, marking.width);
    }
#endif
}

// ═══════════════════════════════════════════════════════════
// SignGraphics
// ═══════════════════════════════════════════════════════════

unsigned int SignGraphics::roadSignsObjectID(const std::string& roadID)
{
    if (roadID.empty()) return 910000;
    try {
        return static_cast<unsigned int>(std::stoul(roadID)) + 910000;
    } catch (...) {
        // For nonnumeric road IDs, use a hash to avoid collisions
        unsigned int hash = 2166136261u;
        for (char c : roadID) {
            hash ^= static_cast<unsigned char>(c);
            hash *= 16777619u;
        }
        return 910000 + (hash % 100000);
    }
}

SignGraphics::SignGraphics(const std::shared_ptr<Road>& road)
    : PermanentGraphics(roadSignsObjectID(road->ID()))
{
#ifndef G_TEST
    if (!g_mapViewGL || !road) return;

    const std::string roadID = road->ID();
    auto* reg = SignRegistry::Instance();
    const auto& signs = reg->placedSigns();
    const odr::Road& gen = road->generated;

    for (const auto& sign : signs)
    {
        if (sign.roadID != roadID) continue;
        const auto* def = reg->get(sign.signType);
        if (!def) continue;

        double s = sign.s;
        if (s < 0 || s > gen.ref_line.length) continue;

        auto basePos = gen.get_xyz(s, sign.tOffset, 0);
        double hdg = gen.ref_line.get_hdg(s);

        // Pole
        double poleW = 0.1;
        odr::Line3D poleBase = {
            {basePos[0] - poleW, basePos[1] - poleW, basePos[2]},
            {basePos[0] + poleW, basePos[1] - poleW, basePos[2]},
            {basePos[0] + poleW, basePos[1] + poleW, basePos[2]},
            {basePos[0] - poleW, basePos[1] + poleW, basePos[2]}
        };
        AddPoly(poleBase, QColor(100, 100, 100), sign.height);

        // Sign plate
        QColor signColor = colorFromString(def->primaryColor);
        double plateW = def->defaultWidth / 2.0;
        double plateH = def->defaultHeight / 2.0;
        double topZ = basePos[2] + sign.height;
        double perpX = -std::sin(hdg), perpY = std::cos(hdg);
        double alongX = std::cos(hdg), alongY = std::sin(hdg);

        odr::Line3D plate = {
            {basePos[0] - plateW * perpX - plateH * alongX,
             basePos[1] - plateW * perpY - plateH * alongY, topZ},
            {basePos[0] + plateW * perpX - plateH * alongX,
             basePos[1] + plateW * perpY - plateH * alongY, topZ},
            {basePos[0] + plateW * perpX + plateH * alongX,
             basePos[1] + plateW * perpY + plateH * alongY, topZ},
            {basePos[0] - plateW * perpX + plateH * alongX,
             basePos[1] - plateW * perpY + plateH * alongY, topZ}
        };
        AddPoly(plate, signColor);
    }
#endif
}

SignGraphics::~SignGraphics()
{
}

void SignGraphics::RemoveForRoad(const std::string& roadID)
{
#ifndef G_TEST
    if (g_mapViewGL)
        g_mapViewGL->RemoveObject(roadSignsObjectID(roadID));
#endif
}

// ═══════════════════════════════════════════════════════════
// FurnitureGraphics
// ═══════════════════════════════════════════════════════════

unsigned int FurnitureGraphics::roadFurnitureObjectID(const std::string& roadID)
{
    if (roadID.empty()) return 920000;
    try {
        return static_cast<unsigned int>(std::stoul(roadID)) + 920000;
    } catch (...) {
        // For nonnumeric road IDs, use a hash to avoid collisions
        unsigned int hash = 2166136261u;
        for (char c : roadID) {
            hash ^= static_cast<unsigned char>(c);
            hash *= 16777619u;
        }
        return 920000 + (hash % 100000);
    }
}

FurnitureGraphics::FurnitureGraphics(const std::shared_ptr<Road>& road)
    : PermanentGraphics(roadFurnitureObjectID(road->ID()))
{
#ifndef G_TEST
    if (!g_mapViewGL || !road) return;

    const std::string roadID = road->ID();
    auto* reg = FurnitureRegistry::Instance();
    const auto& items = reg->placedFurniture();
    const odr::Road& gen = road->generated;

    for (const auto& item : items)
    {
        if (item.roadID != roadID) continue;
        const auto* def = reg->get(item.furnitureType);
        if (!def) continue;

        QColor color = colorFromString(def->color);

        if (def->isLinear)
        {
            double sStart = std::max(0.0, item.sStart);
            double sEnd = std::min(gen.ref_line.length, item.sEnd);
            if (sEnd <= sStart) continue;

            double halfW = def->defaultWidth / 2.0;
            odr::Line3D left, right;
            for (double s = sStart; s <= sEnd; s += 0.5)
            {
                left.push_back(gen.get_xyz(s, item.tOffset - halfW, 0));
                right.push_back(gen.get_xyz(s, item.tOffset + halfW, 0));
            }
            left.push_back(gen.get_xyz(sEnd, item.tOffset - halfW, 0));
            right.push_back(gen.get_xyz(sEnd, item.tOffset + halfW, 0));
            if (left.size() > 1)
                AddQuads(left, right, color);
        }
        else
        {
            int count = std::max(1, item.repeatCount);
            for (int i = 0; i < count; i++)
            {
                double s = item.sStart + i * item.repeatSpacing;
                if (s < 0 || s > gen.ref_line.length) break;

                // Turn-radius filter (simple-road-system style): skip
                // instances on curves tighter than the configured radius.
                if (item.minTurnRadius > 0.0 &&
                    turnRadiusAt(gen.ref_line, s) < item.minTurnRadius)
                    continue;

                auto pos = gen.get_xyz(s, item.tOffset, 0);
                double w = def->defaultWidth / 2.0;
                odr::Line3D base = {
                    {pos[0] - w, pos[1] - w, pos[2]},
                    {pos[0] + w, pos[1] - w, pos[2]},
                    {pos[0] + w, pos[1] + w, pos[2]},
                    {pos[0] - w, pos[1] + w, pos[2]}
                };
                AddPoly(base, color, item.height);
            }
        }
    }
#endif
}

FurnitureGraphics::~FurnitureGraphics()
{
}

void FurnitureGraphics::RemoveForRoad(const std::string& roadID)
{
#ifndef G_TEST
    if (g_mapViewGL)
        g_mapViewGL->RemoveObject(roadFurnitureObjectID(roadID));
#endif
}

} // namespace LM
