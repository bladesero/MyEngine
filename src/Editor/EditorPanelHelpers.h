#pragma once

#include "Assets/AssetManager.h"
#include "Assets/MaterialAsset.h"
#include "Assets/MeshAsset.h"
#include "Core/EngineMath.h"
#include "Physics/ColliderComponent.h"
#include "Scene/Component.h"
#include "Scene/Transform.h"

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace EditorPanelHelpers {
inline bool DrawVec3(const char* label, Vec3& value, float speed) {
    float values[3] = {value.x, value.y, value.z};
    if (!ImGui::DragFloat3(label, values, speed))
        return false;
    value = {values[0], values[1], values[2]};
    return true;
}
inline bool SameTransform(const Transform& a, const Transform& b) {
    return a.position.x == b.position.x && a.position.y == b.position.y && a.position.z == b.position.z &&
           a.rotation.x == b.rotation.x && a.rotation.y == b.rotation.y && a.rotation.z == b.rotation.z &&
           a.scale.x == b.scale.x && a.scale.y == b.scale.y && a.scale.z == b.scale.z;
}
inline void MatToFloat(const Mat4& value, float output[16]) {
    std::memcpy(output, value.m, sizeof(float) * 16);
}
inline void FloatToMat(const float input[16], Mat4& output) {
    std::memcpy(output.m, input, sizeof(float) * 16);
}
inline void DecomposeLocal(const Mat4& m, Vec3& position, Vec3& rotation, Vec3& scale) {
    position = {m.m[3][0], m.m[3][1], m.m[3][2]};
    auto length = [&](int row) {
        return std::sqrt(m.m[row][0] * m.m[row][0] + m.m[row][1] * m.m[row][1] + m.m[row][2] * m.m[row][2]);
    };
    scale = {length(0), length(1), length(2)};
    const float sx = std::max(scale.x, 1e-8f), sy = std::max(scale.y, 1e-8f), sz = std::max(scale.z, 1e-8f);
    const float pitch = std::asin(std::clamp(m.m[1][2] / sy, -1.0f, 1.0f));
    float yaw = 0, roll = 0;
    if (std::fabs(std::cos(pitch)) > 1e-4f) {
        yaw = std::atan2(-m.m[0][2] / sx, m.m[2][2] / sz);
        roll = std::atan2(-m.m[1][0] / sy, m.m[1][1] / sy);
    } else
        yaw = std::atan2(m.m[0][1] / sx, m.m[0][0] / sx);
    rotation = {pitch * kRad2Deg, yaw * kRad2Deg, roll * kRad2Deg};
}
inline std::string LowerExtension(const std::string& path) {
    std::string value = std::filesystem::path(path).extension().string();
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}
inline bool IsModel(const std::string& path) {
    const auto e = LowerExtension(path);
    return e == ".obj" || e == ".gltf" || e == ".glb";
}
inline bool IsTexture(const std::string& path) {
    const auto e = LowerExtension(path);
    return e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".bmp" || e == ".tga" || e == ".hdr";
}
inline bool IsMaterial(const std::string& path) {
    return LowerExtension(path) == ".mat";
}
inline bool DrawEnabled(Component& component) {
    bool enabled = component.IsEnabled();
    if (!ImGui::Checkbox("Enabled", &enabled))
        return false;
    component.SetEnabled(enabled);
    return true;
}
inline bool DrawCollider(ColliderComponent& collider) {
    bool changed = false;
    bool trigger = collider.IsTrigger();
    if (ImGui::Checkbox("Is Trigger", &trigger)) {
        collider.SetTrigger(trigger);
        changed = true;
    }
    return changed;
}
inline MeshHandle ResolveMesh(const std::string& path) {
    auto& assets = AssetManager::Get();
    if (path == "__builtin__/Cube")
        return assets.GetCubeMesh();
    if (path == "__builtin__/Quad")
        return assets.GetQuadMesh();
    if (path == "__builtin__/Triangle")
        return assets.GetTriangleMesh();
    auto value = assets.GetByPath<MeshAsset>(path);
    return value.IsValid() ? value : assets.Load<MeshAsset>(path);
}
inline MaterialHandle ResolveMaterial(const std::string& path) {
    return AssetManager::Get().ResolveMaterialReference(path);
}
} // namespace EditorPanelHelpers
