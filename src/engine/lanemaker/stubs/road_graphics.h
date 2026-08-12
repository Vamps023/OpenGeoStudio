#pragma once
// Stub for Qt-dependent road_graphics.h — geometry only, no rendering
#include <vector>
#include <string>
#include <memory>
#include "../libOpenDRIVE/include/Road.h"
#include "../libOpenDRIVE/include/Math.hpp"

namespace LM
{
    typedef uint64_t FaceIndex_t;

    class AbstractGraphicsItem
    {
    public:
        virtual void AddQuads(const odr::Line3D&, const odr::Line3D&, void*) {}
        virtual void AddLine(const odr::Line3D&, double, void*) {}
        virtual void AddPoly(const odr::Line3D&, void*, double = 0) {}
        void Clear() {}
        virtual ~AbstractGraphicsItem() = default;
    protected:
        AbstractGraphicsItem() = default;
        std::vector<int> graphicsIndex;
        unsigned int objectID = 0;
    };

    class TemporaryGraphics : public AbstractGraphicsItem
    {
    public:
        TemporaryGraphics() {}
    };

    class PermanentGraphics : public AbstractGraphicsItem
    {
    public:
        PermanentGraphics(unsigned int id) { objectID = id; }
        void UpdateObjectID(unsigned int id) { objectID = id; }
        void RemoveObject() {}
        void UpdateObject(uint8_t) {}
    };

    class SectionGraphics : protected PermanentGraphics
    {
    public:
        SectionGraphics(std::shared_ptr<class Road>, const odr::LaneSection&, double, double)
            : PermanentGraphics(0) {}
        ~SectionGraphics() {}
        void updateIndexingInfo(std::string, int, double) {}
        double Length() const { return sMax - sMin; }
        double sMin = 0, sMax = 0;
        std::vector<FaceIndex_t> allSpatialIndice;
    };

    class JunctionGraphics : protected PermanentGraphics
    {
    public:
        JunctionGraphics(const odr::Line2D&, double, std::string) : PermanentGraphics(0) {}
        JunctionGraphics(const std::vector<std::pair<odr::Line3D, odr::Line3D>>&, std::string) : PermanentGraphics(0) {}
        void Hide(bool) {}
        ~JunctionGraphics() {}
    };
}
