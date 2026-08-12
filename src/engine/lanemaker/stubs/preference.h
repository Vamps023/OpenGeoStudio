#pragma once
// Stub for preference.h
// Note: LanePlan is already defined in road_profile.h, don't redefine it here
namespace LM {
    class Preference {
    public:
        Preference() = default;
        static Preference* Instance() { static Preference inst; return &inst; }
        bool autoGenerateJunction = true;
        bool autoGenerateGraphics = true;
        bool alwaysVerify = false;
    };
}

// UserPreference is the global preference type used by change_tracker.cpp
using UserPreference = LM::Preference;

// Global preference instance (extern-declared in change_tracker.cpp)
inline UserPreference g_preference;
