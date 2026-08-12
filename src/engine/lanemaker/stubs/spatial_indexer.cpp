// Stub implementation for spatial_indexer
#include "spatial_indexer.h"

namespace LM
{
    uint32_t SpatialIndexer::InvalidFace = 0xFFFFFFFF;
    SpatialIndexer* SpatialIndexer::_instance = nullptr;

    SpatialIndexer::SpatialIndexer() {}
    SpatialIndexer* SpatialIndexer::Instance()
    {
        if (!_instance) _instance = new SpatialIndexer();
        return _instance;
    }

    FaceIndex_t SpatialIndexer::Index(odr::Road, odr::Lane, double, double) { return 0; }
    RayCastResult SpatialIndexer::RayCast(RayCastQuery) { return {}; }
    std::vector<RayCastResult> SpatialIndexer::AllOverlaps(odr::Vec3D, double) { return {}; }
    void SpatialIndexer::UnIndex(FaceIndex_t) {}
    void SpatialIndexer::RebuildTree() {}
    Quad& SpatialIndexer::FaceInfo(FaceIndex_t) { static Quad dummy{"",0,0,0,0,{},{},false}; return dummy; }
    void SpatialIndexer::Clear() { quads.clear(); }

    SpatialIndexerDynamic* SpatialIndexerDynamic::_instance = nullptr;
    SpatialIndexerDynamic::SpatialIndexerDynamic() {}
    SpatialIndexerDynamic* SpatialIndexerDynamic::Instance()
    {
        if (!_instance) _instance = new SpatialIndexerDynamic();
        return _instance;
    }
    void SpatialIndexerDynamic::Index(unsigned int, double[16], double[3]) {}
    unsigned int SpatialIndexerDynamic::RayCast(odr::Vec3D, odr::Vec3D) { return static_cast<unsigned int>(-1); }
    void SpatialIndexerDynamic::UnIndex(unsigned int) {}
}
