#pragma once

#include "DebugDraw/DebugDrawCommand.h"
#include "Editor/EditorLayout.h"
#include "Scene/Transform.h"

#include <cstddef>
#include <cstdint>

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#include <ImGuizmo.h>
#endif

class Actor;
class Camera;
class EditorContext;

struct EditorGizmoState {
#if defined(MYENGINE_ENABLE_IMGUI)
    ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE mode = ImGuizmo::LOCAL;
#endif
};

class EditorPickingController {
public:
    void Pick(EditorContext& context, float screenX, float screenY) const;
};

class EditorLightGizmoController {
public:
    static float WorldUnitsPerPixel(const Camera& camera, const Vec3& worldPosition, int viewportHeight);
    static Mat4 BuildAxialTransform(const Vec3& origin, const Vec3& direction, float radialScale, float axialScale);
    static bool RaySphereHit(const Ray& ray, const Vec3& center, float radius, float& outDistance);
    static bool RayCapsuleHit(const Ray& ray, const Vec3& pointA, const Vec3& pointB, float radius, float& outDistance);
    static bool FindClosestLightHit(EditorContext& context, const Ray& ray, Actor*& closestActor,
                                    float& closestDistance);

    size_t Submit(EditorContext& context);

private:
    void EnsureMeshes();

    DebugDrawMeshHandle m_ArrowHeadMesh;
    DebugDrawMeshHandle m_SpotConeMesh;
};

class EditorGizmoController {
public:
    static bool ComputeLocalMatrix(const Mat4& world, const Mat4* parentWorld, Mat4& local);
    void DrawAndApply(EditorContext& context, Actor& actor, const EditorPanelRect& viewportRect,
                      const EditorGizmoState& state);
    void FinishInteraction(EditorContext& context);

private:
    bool Commit(EditorContext& context);

    uint64_t m_ActiveActorID = 0;
    Transform m_InitialTransform;
    bool m_WasUsing = false;
};
