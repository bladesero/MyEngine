#include "DebugDraw/DebugDraw.h"

#include "DebugDraw/DebugDrawService.h"
#include "Physics/BoxColliderComponent.h"
#include "Physics/CapsuleColliderComponent.h"
#include "Physics/ColliderComponent.h"
#include "Physics/SphereColliderComponent.h"
#include "Scene/Actor.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_set>

namespace {

DebugDrawCommand MakeCommand(Scene& scene, DebugDrawGeometryKind geometry, const Mat4& transform, const Color& color,
                             float durationSeconds, DebugDrawDepthMode depthMode, DebugDrawViewMask viewMask) {
    DebugDrawCommand command;
    command.scene = scene.GetLifetimeToken();
    command.sceneGeneration = scene.GetLifetimeGeneration();
    command.geometry = geometry;
    command.transform = transform;
    command.color = color;
    command.durationSeconds = (std::max)(0.0f, durationSeconds);
    command.depthMode = depthMode;
    command.viewMask = viewMask;
    return command;
}

Mat4 BasisTransform(const Vec3& xAxis, const Vec3& yAxis, const Vec3& zAxis, const Vec3& translation) {
    Mat4 result = Mat4::Identity();
    result.m[0][0] = xAxis.x;
    result.m[0][1] = xAxis.y;
    result.m[0][2] = xAxis.z;
    result.m[1][0] = yAxis.x;
    result.m[1][1] = yAxis.y;
    result.m[1][2] = yAxis.z;
    result.m[2][0] = zAxis.x;
    result.m[2][1] = zAxis.y;
    result.m[2][2] = zAxis.z;
    result.m[3][0] = translation.x;
    result.m[3][1] = translation.y;
    result.m[3][2] = translation.z;
    return result;
}

void AddTriangleEdge(std::unordered_set<uint64_t>& edges, uint32_t a, uint32_t b) {
    const uint32_t lo = (std::min)(a, b);
    const uint32_t hi = (std::max)(a, b);
    edges.insert((static_cast<uint64_t>(lo) << 32u) | hi);
}

} // namespace

namespace DebugDraw {

bool DrawLine(Scene& scene, const Vec3& start, const Vec3& end, const Color& color, float durationSeconds,
              DebugDrawDepthMode depthMode, DebugDrawViewMask viewMask) {
    const Vec3 delta = end - start;
    if (delta.LengthSq() <= 1e-12f)
        return false;
    Mat4 transform = Mat4::Identity();
    transform.m[0][0] = delta.x;
    transform.m[0][1] = delta.y;
    transform.m[0][2] = delta.z;
    transform.m[3][0] = start.x;
    transform.m[3][1] = start.y;
    transform.m[3][2] = start.z;
    return DebugDrawService::Get().Submit(
        MakeCommand(scene, DebugDrawGeometryKind::Line, transform, color, durationSeconds, depthMode, viewMask));
}

bool DrawSphere(Scene& scene, const Vec3& center, float radius, const Color& color, float durationSeconds,
                DebugDrawDepthMode depthMode, DebugDrawViewMask viewMask) {
    if (!std::isfinite(radius) || radius <= 0.0f)
        return false;
    const Mat4 transform = Mat4::Scale(radius) * Mat4::Translation(center);
    return DebugDrawService::Get().Submit(
        MakeCommand(scene, DebugDrawGeometryKind::Sphere, transform, color, durationSeconds, depthMode, viewMask));
}

bool DrawSphere(Scene& scene, const SphereShape& sphere, const Color& color, float durationSeconds,
                DebugDrawDepthMode depthMode, DebugDrawViewMask viewMask) {
    return DrawSphere(scene, sphere.center, sphere.radius, color, durationSeconds, depthMode, viewMask);
}

bool DrawBox(Scene& scene, const Vec3& center, const Vec3& halfExtents, const Quat& rotation, const Color& color,
             float durationSeconds, DebugDrawDepthMode depthMode, DebugDrawViewMask viewMask) {
    if (halfExtents.x <= 0.0f || halfExtents.y <= 0.0f || halfExtents.z <= 0.0f)
        return false;
    const Mat4 transform = Mat4::Scale(halfExtents) * rotation.ToMat4() * Mat4::Translation(center);
    return DebugDrawService::Get().Submit(
        MakeCommand(scene, DebugDrawGeometryKind::Box, transform, color, durationSeconds, depthMode, viewMask));
}

bool DrawBox(Scene& scene, const OrientedBox& box, const Color& color, float durationSeconds,
             DebugDrawDepthMode depthMode, DebugDrawViewMask viewMask) {
    if (box.halfExtents.x <= 0.0f || box.halfExtents.y <= 0.0f || box.halfExtents.z <= 0.0f)
        return false;
    const Mat4 orientation =
        BasisTransform(box.axes[0].Normalized(), box.axes[1].Normalized(), box.axes[2].Normalized(), box.center);
    const Mat4 transform = Mat4::Scale(box.halfExtents) * orientation;
    return DebugDrawService::Get().Submit(
        MakeCommand(scene, DebugDrawGeometryKind::Box, transform, color, durationSeconds, depthMode, viewMask));
}

bool DrawCapsule(Scene& scene, const CapsuleShape& capsule, const Color& color, float durationSeconds,
                 DebugDrawDepthMode depthMode, DebugDrawViewMask viewMask) {
    if (!std::isfinite(capsule.radius) || capsule.radius <= 0.0f)
        return false;
    const Vec3 axisVector = capsule.pointB - capsule.pointA;
    const float axisLength = axisVector.Length();
    const Vec3 axis = axisLength > 1e-6f ? axisVector * (1.0f / axisLength) : Vec3::Up();
    const Vec3 reference = std::fabs(axis.y) < 0.999f ? Vec3::Up() : Vec3::Forward();
    const Vec3 right = reference.Cross(axis).Normalized();
    const Vec3 forward = axis.Cross(right).Normalized();
    const Vec3 center = (capsule.pointA + capsule.pointB) * 0.5f;
    DebugDrawCommand command =
        MakeCommand(scene, DebugDrawGeometryKind::Capsule, BasisTransform(right, axis, forward, center), color,
                    durationSeconds, depthMode, viewMask);
    command.shapeParameters = {capsule.radius, axisLength * 0.5f, 0.0f, 1.0f};
    return DebugDrawService::Get().Submit(std::move(command));
}

bool DrawMesh(Scene& scene, const MeshHandle& mesh, const Mat4& transform, const Color& color, float durationSeconds,
              DebugDrawDepthMode depthMode, DebugDrawViewMask viewMask) {
    if (!mesh.IsValid() || mesh->GetVertices().empty())
        return false;
    DebugDrawCommand command =
        MakeCommand(scene, DebugDrawGeometryKind::MeshAsset, transform, color, durationSeconds, depthMode, viewMask);
    command.mesh = mesh;
    return DebugDrawService::Get().Submit(std::move(command));
}

DebugDrawMeshHandle CreateMesh(const std::vector<Vec3>& positions, const std::vector<uint32_t>& triangleIndices) {
    if (positions.empty() || triangleIndices.empty() || triangleIndices.size() % 3u != 0u)
        return {};
    std::unordered_set<uint64_t> edges;
    edges.reserve(triangleIndices.size());
    for (size_t i = 0; i < triangleIndices.size(); i += 3) {
        const uint32_t a = triangleIndices[i + 0];
        const uint32_t b = triangleIndices[i + 1];
        const uint32_t c = triangleIndices[i + 2];
        if (a >= positions.size() || b >= positions.size() || c >= positions.size())
            return {};
        AddTriangleEdge(edges, a, b);
        AddTriangleEdge(edges, b, c);
        AddTriangleEdge(edges, c, a);
    }
    std::vector<uint64_t> sortedEdges(edges.begin(), edges.end());
    std::sort(sortedEdges.begin(), sortedEdges.end());
    std::vector<uint32_t> lineIndices;
    lineIndices.reserve(sortedEdges.size() * 2u);
    for (uint64_t edge : sortedEdges) {
        lineIndices.push_back(static_cast<uint32_t>(edge >> 32u));
        lineIndices.push_back(static_cast<uint32_t>(edge));
    }
    return std::make_shared<DebugDrawMesh>(positions, std::move(lineIndices));
}

bool DrawMesh(Scene& scene, DebugDrawMeshHandle mesh, const Mat4& transform, const Color& color, float durationSeconds,
              DebugDrawDepthMode depthMode, DebugDrawViewMask viewMask) {
    if (!mesh || !mesh->IsValid())
        return false;
    DebugDrawCommand command = MakeCommand(scene, DebugDrawGeometryKind::ProceduralMesh, transform, color,
                                           durationSeconds, depthMode, viewMask);
    command.proceduralMesh = std::move(mesh);
    return DebugDrawService::Get().Submit(std::move(command));
}

bool DrawCollider(const ColliderComponent& collider, const Color& color, float durationSeconds,
                  DebugDrawDepthMode depthMode, DebugDrawViewMask viewMask) {
    const Actor* owner = collider.GetOwner();
    Scene* scene = owner ? owner->GetScene() : nullptr;
    if (!scene)
        return false;
    if (const auto* box = dynamic_cast<const BoxColliderComponent*>(&collider))
        return DrawBox(*scene, box->GetWorldShape(), color, durationSeconds, depthMode, viewMask);
    if (const auto* sphere = dynamic_cast<const SphereColliderComponent*>(&collider))
        return DrawSphere(*scene, sphere->GetWorldShape(), color, durationSeconds, depthMode, viewMask);
    if (const auto* capsule = dynamic_cast<const CapsuleColliderComponent*>(&collider))
        return DrawCapsule(*scene, capsule->GetWorldShape(), color, durationSeconds, depthMode, viewMask);
    return false;
}

} // namespace DebugDraw
