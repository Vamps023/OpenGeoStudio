#pragma once
// Stub for util.h
#include <string>
#include "../libOpenDRIVE/include/Math.hpp"
namespace LM {
    class Util {
    public:
        static std::string RoadName(const std::string& id) { return id; }
        static double SnapAngle(double a) { return a; }
    };
}
