#pragma once

#include "Core/EngineMath.h"
#include "Scene/ActorHandle.h"

// Collision callbacks are a Scene contract implemented by Runtime/Physics.

class Actor;

enum class CollisionEventPhase {
    Enter,
    Stay,
    Exit,
};

struct CollisionEvent {
    ActorHandle otherHandle;
    // Frame-local compatibility pointer. Persist otherHandle instead.
    Actor* other = nullptr;
    Vec3 point = Vec3::Zero();
    Vec3 normal = Vec3::Up();
    float depth = 0.0f;
    bool trigger = false;
    CollisionEventPhase phase = CollisionEventPhase::Enter;
};
