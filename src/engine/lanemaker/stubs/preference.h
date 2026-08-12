#pragma once
// Stub for preference.h
namespace LM {
    struct LanePlan { int numLanesLeft = 1; int numLanesRight = 1; };
    class Preference {
    public:
        static Preference* Instance() { static Preference inst; return &inst; }
        LanePlan defaultLanePlan() const { return {}; }
    };
}
