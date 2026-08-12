#pragma once
// Stub for Qt-dependent map_view_gl.h — no OpenGL rendering in native engine
#include <string>
#include <cstdint>

namespace LM
{
    class MapViewGL
    {
    public:
        void update() {}
        void SetRoadHighlight(const std::string&, bool) {}
        void SetRoadHidden(const std::string&, bool) {}
        void SetRoadGreenLight(const std::string&, bool) {}
        void RemoveObject(unsigned int) {}
        uint8_t GetObjectFlag(unsigned int) { return 0; }
        void UpdateObject(unsigned int, uint8_t) {}
    };

    // Global instance used by LaneMaker code
    extern MapViewGL* g_mapViewGL;
}
