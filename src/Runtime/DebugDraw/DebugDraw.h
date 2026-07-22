#pragma once

#include "DebugDraw/DebugDrawCommand.h"
#include "Physics/CollisionShapes.h"

class ColliderComponent;

namespace DebugDraw {

inline constexpr Color kDefaultColor{0.2f, 1.0f, 0.2f, 1.0f};

bool DrawLine(Scene& scene, const Vec3& start, const Vec3& end, const Color& color = kDefaultColor,
              float durationSeconds = 0.0f, DebugDrawDepthMode depthMode = DebugDrawDepthMode::Test,
              DebugDrawViewMask viewMask = DebugDrawViewMask::All);
bool DrawSphere(Scene& scene, const Vec3& center, float radius, const Color& color = kDefaultColor,
                float durationSeconds = 0.0f, DebugDrawDepthMode depthMode = DebugDrawDepthMode::Test,
                DebugDrawViewMask viewMask = DebugDrawViewMask::All);
bool DrawSphere(Scene& scene, const SphereShape& sphere, const Color& color = kDefaultColor,
                float durationSeconds = 0.0f, DebugDrawDepthMode depthMode = DebugDrawDepthMode::Test,
                DebugDrawViewMask viewMask = DebugDrawViewMask::All);
bool DrawBox(Scene& scene, const Vec3& center, const Vec3& halfExtents, const Quat& rotation = Quat::Identity(),
             const Color& color = kDefaultColor, float durationSeconds = 0.0f,
             DebugDrawDepthMode depthMode = DebugDrawDepthMode::Test,
             DebugDrawViewMask viewMask = DebugDrawViewMask::All);
bool DrawBox(Scene& scene, const OrientedBox& box, const Color& color = kDefaultColor, float durationSeconds = 0.0f,
             DebugDrawDepthMode depthMode = DebugDrawDepthMode::Test,
             DebugDrawViewMask viewMask = DebugDrawViewMask::All);
bool DrawCapsule(Scene& scene, const CapsuleShape& capsule, const Color& color = kDefaultColor,
                 float durationSeconds = 0.0f, DebugDrawDepthMode depthMode = DebugDrawDepthMode::Test,
                 DebugDrawViewMask viewMask = DebugDrawViewMask::All);
bool DrawMesh(Scene& scene, const MeshHandle& mesh, const Mat4& transform, const Color& color = kDefaultColor,
              float durationSeconds = 0.0f, DebugDrawDepthMode depthMode = DebugDrawDepthMode::Test,
              DebugDrawViewMask viewMask = DebugDrawViewMask::All);
DebugDrawMeshHandle CreateMesh(const std::vector<Vec3>& positions, const std::vector<uint32_t>& triangleIndices);
bool DrawMesh(Scene& scene, DebugDrawMeshHandle mesh, const Mat4& transform, const Color& color = kDefaultColor,
              float durationSeconds = 0.0f, DebugDrawDepthMode depthMode = DebugDrawDepthMode::Test,
              DebugDrawViewMask viewMask = DebugDrawViewMask::All);
bool DrawCollider(const ColliderComponent& collider, const Color& color = kDefaultColor, float durationSeconds = 0.0f,
                  DebugDrawDepthMode depthMode = DebugDrawDepthMode::Test,
                  DebugDrawViewMask viewMask = DebugDrawViewMask::All);

} // namespace DebugDraw
