#pragma once

#include "Core/EngineMath.h"

struct NavMeshBakeSettings {
    AABB bounds{{-10, 0, -10}, {10, 2, 10}};
    float cellSize = 0.5f;
    float agentRadius = 0.4f;
};
