#pragma once

#include "Core/EngineMath.h"

class Actor;

enum class CollisionEventPhase {
    Enter,
    Stay,
    Exit,
};

struct CollisionEvent {
    Actor* other = nullptr;
    Vec3 point = Vec3::Zero();
    Vec3 normal = Vec3::Up();
    float depth = 0.0f;
    bool trigger = false;
    CollisionEventPhase phase = CollisionEventPhase::Enter;
};
