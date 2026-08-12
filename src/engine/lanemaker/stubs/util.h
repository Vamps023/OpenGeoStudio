#pragma once
// Stub for util.h
#include <string>
#include "../libOpenDRIVE/include/Math.hpp"

// TQDM stub — just returns the container as-is (no progress bar in headless mode)
template<typename T>
const T& TQDM(const T& container) { return container; }

namespace LM {
    class Util {
    public:
        static std::string RoadName(const std::string& id) { return id; }
        static double SnapAngle(double a) { return a; }
    };
}
