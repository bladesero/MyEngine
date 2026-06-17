#pragma once

#include "Core/EngineMath.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct Bone {
    std::string name;
    int parent = -1;
    Mat4 inverseBind = Mat4::Identity();
    Vec3 bindTranslation = Vec3::Zero();
    Quat bindRotation = Quat::Identity();
    Vec3 bindScale = Vec3::One();
};

struct SkinWeight {
    std::array<uint16_t, 4> boneIndices = { 0, 0, 0, 0 };
    std::array<float, 4> weights = { 1.0f, 0.0f, 0.0f, 0.0f };
};

struct BoneKeyframe {
    float time = 0.0f;
    Vec3 translation = Vec3::Zero();
    Quat rotation = Quat::Identity();
    Vec3 scale = Vec3::One();
};

struct BoneTrack {
    uint16_t boneIndex = 0;
    std::vector<BoneKeyframe> keys;
};

struct AnimationClip {
    std::string name;
    float duration = 0.0f;
    bool looping = true;
    std::vector<BoneTrack> tracks;
};
