#pragma once
// Stub for Qt-dependent spatial_indexer.h — simple brute-force overlap detection
#include <vector>
#include <string>
#include <unordered_set>
#include "../libOpenDRIVE/include/Road.h"

namespace LM
{
    typedef uint64_t FaceIndex_t;

    struct RayCastQuery
    {
        odr::Vec3D origin;
        odr::Vec3D direction;
        std::unordered_set<FaceIndex_t> skip;
    };

    struct RayCastResult
    {
        bool hit = false;
        odr::Vec3D hitPos;
        std::string roadID;
        int lane = 0;
        double s = 0;
    };

    struct Quad
    {
        std::string roadID;
        const int laneIDNormal, laneIDReversed;
        double sBegin, sEnd;
        odr::Vec2D pointOnSBegin, pointOnSEnd;
        bool magneticArea;
        int LaneID() const { return sBegin < sEnd ? laneIDNormal : laneIDReversed; }
    };

    class SpatialIndexer
    {
    public:
        static SpatialIndexer* Instance();
        FaceIndex_t Index(odr::Road road, odr::Lane lane, double sBegin, double sEnd);
        RayCastResult RayCast(RayCastQuery ray);
        std::vector<RayCastResult> AllOverlaps(odr::Vec3D origin, double zRange = 0.01);
        void UnIndex(FaceIndex_t index);
        void RebuildTree();
        Quad& FaceInfo(FaceIndex_t);
        void Clear();
        static uint32_t InvalidFace;
    private:
        SpatialIndexer();
        static SpatialIndexer* _instance;
        std::vector<Quad> quads;
    };

    class SpatialIndexerDynamic
    {
    public:
        static SpatialIndexerDynamic* Instance();
        void Index(unsigned int id, double transform[16], double lwh[3]);
        unsigned int RayCast(odr::Vec3D origin, odr::Vec3D direction);
        void UnIndex(unsigned int id);
    private:
        static SpatialIndexerDynamic* _instance;
        SpatialIndexerDynamic();
    };
}
