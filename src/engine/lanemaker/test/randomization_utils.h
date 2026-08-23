#pragma once

#include "road_profile.h"
#include <vector>
#include <cstdint>

// Inclusive
int RandomIntBetween(int low, int hi);

// Sorted, Containing low and hi
std::vector<int> RandomSortedVector(int low, int hi, uint32_t count);

LM::LaneProfile GenerateConfig(int seed, uint32_t length);
