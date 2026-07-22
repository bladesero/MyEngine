#include "Editor/EditorViewportControllers.h"

#include "Assets/MeshAsset.h"
#include "Camera/Camera.h"
#include "DebugDraw/DebugDraw.h"
#include "Editor/EditorCommand.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorPanelHelpers.h"
#include "Game/SceneRenderLayer.h"
#include "Game/SceneViewportController.h"
#include "Math/Mat4Inverse.h"
#include "Renderer/LightComponent.h"
#include "Scene/Actor.h"
#include "Scene/MeshRendererComponent.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <vector>

namespace {

constexpr float kPointMarkerRadiusPixels = 10.0f;
constexpr float kArrowLengthPixels = 64.0f;
constexpr float kArrowHeadLengthPixels = 14.0f;
constexpr float kArrowHeadRadiusPixels = 6.0f;
constexpr float kArrowPickRadiusPixels = 8.0f;

DebugDrawMeshHandle BuildWireCone(uint32_t segments, bool apexAtTip, uint32_t spokeCount) {
    if (segments < 3u || spokeCount == 0u)
        return {};

    std::vector<Vec3> positions;
    positions.reserve(segments + 1u);
    positions.push_back(apexAtTip ? Vec3{0.0f, 0.0f, 1.0f} : Vec3::Zero());
    const float ringZ = apexAtTip ? 0.0f : 1.0f;
    for (uint32_t segment = 0; segment < segments; ++segment) {
        const float angle = kTwoPi * static_cast<float>(segment) / static_cast<float>(segments);
        positions.push_back({std::cos(angle), std::sin(angle), ringZ});
    }

    std::vector<uint32_t> lineIndices;
    lineIndices.reserve((segments + spokeCount) * 2u);
    for (uint32_t segment = 0; segment < segments; ++segment) {
        lineIndices.push_back(segment + 1u);
        lineIndices.push_back((segment + 1u) % segments + 1u);
    }
    for (uint32_t spoke = 0; spoke < spokeCount; ++spoke) {
        lineIndices.push_back(0u);
        lineIndices.push_back((spoke * segments) / spokeCount + 1u);
    }
    return std::make_shared<DebugDrawMesh>(std::move(positions), std::move(lineIndices));
}

Color LightGizmoColor(const Vec3& source, bool selected, float alpha) {
    Vec3 rgb{
        std::clamp(source.x, 0.0f, 1.0f),
        std::clamp(source.y, 0.0f, 1.0f),
        std::clamp(source.z, 0.0f, 1.0f),
    };
    const float luminance = rgb.x * 0.2126f + rgb.y * 0.7152f + rgb.z * 0.0722f;
    if (luminance < 0.05f)
        rgb = {1.0f, 0.72f, 0.18f};
    if (selected)
        rgb = Vec3::Lerp(rgb, Vec3::One(), 0.15f);
    return {rgb.x, rgb.y, rgb.z, alpha};
}

} // namespace

void EditorPickingController::Pick(EditorContext& context, float screenX, float screenY) const {
    Math::Ray ray{};
    auto* sceneViewport = context.GetSceneViewport();
    if (!sceneViewport || !sceneViewport->BuildRayFromScreen(screenX, screenY, ray)) {
        return;
    }

    Scene* scene = context.GetSceneViewScene();
    if (!scene)
        return;

    Actor* closestActor = nullptr;
    float closestDistance = FLT_MAX;
    scene->ForEach([&](Actor& actor) {
        auto* renderer = actor.GetComponent<MeshRendererComponent>();
        if (!renderer || !renderer->IsValid())
            return;

        MeshAsset* mesh = renderer->GetMesh().Get();
        if (!mesh)
            return;

        float nearDistance = 0.0f;
        float farDistance = 0.0f;
        const AABB worldBounds = TransformAABB(mesh->GetAABB(), actor.GetWorldMatrix());
        if (!worldBounds.IntersectRay(ray, nearDistance, farDistance) || farDistance < 0.0f)
            return;

        const float hitDistance = std::max(nearDistance, 0.0f);
        if (hitDistance < closestDistance) {
            closestDistance = hitDistance;
            closestActor = &actor;
        }
    });
    EditorLightGizmoController::FindClosestLightHit(context, ray, closestActor, closestDistance);

    if (closestActor) {
        const EditorSelectionWorldKind world =
            context.IsInspectingPlayWorld() ? EditorSelectionWorldKind::Play : EditorSelectionWorldKind::Editor;
        context.GetSelection().Select(
            EditorSelectObject::MakeActor(closestActor->GetHandle(), closestActor->GetID(), world));
    } else
        context.GetSelection().Clear();
}

float EditorLightGizmoController::WorldUnitsPerPixel(const Camera& camera, const Vec3& worldPosition,
                                                     int viewportHeight) {
    if (viewportHeight <= 0)
        return 0.0f;
    if (camera.GetProjectionMode() == ProjectionMode::Orthographic)
        return camera.GetOrthoHeight() / static_cast<float>(viewportHeight);

    const float depth = (worldPosition - camera.GetPosition()).Dot(camera.GetForward());
    if (depth <= 0.0f)
        return 0.0f;
    const float visibleDepth = (std::max)(depth, camera.GetNear());
    return 2.0f * visibleDepth * std::tan(camera.GetFovY() * 0.5f * kDeg2Rad) / static_cast<float>(viewportHeight);
}

Mat4 EditorLightGizmoController::BuildAxialTransform(const Vec3& origin, const Vec3& direction, float radialScale,
                                                     float axialScale) {
    Vec3 forward = direction.Normalized();
    if (forward.LengthSq() <= 1e-12f)
        forward = Vec3::Forward();
    const Vec3 reference = std::fabs(forward.Dot(Vec3::Up())) < 0.999f ? Vec3::Up() : Vec3::Right();
    const Vec3 right = reference.Cross(forward).Normalized();
    const Vec3 up = forward.Cross(right).Normalized();

    Mat4 transform = Mat4::Identity();
    const Vec3 scaledRight = right * radialScale;
    const Vec3 scaledUp = up * radialScale;
    const Vec3 scaledForward = forward * axialScale;
    transform.m[0][0] = scaledRight.x;
    transform.m[0][1] = scaledRight.y;
    transform.m[0][2] = scaledRight.z;
    transform.m[1][0] = scaledUp.x;
    transform.m[1][1] = scaledUp.y;
    transform.m[1][2] = scaledUp.z;
    transform.m[2][0] = scaledForward.x;
    transform.m[2][1] = scaledForward.y;
    transform.m[2][2] = scaledForward.z;
    transform.m[3][0] = origin.x;
    transform.m[3][1] = origin.y;
    transform.m[3][2] = origin.z;
    return transform;
}

bool EditorLightGizmoController::RaySphereHit(const Ray& ray, const Vec3& center, float radius, float& outDistance) {
    if (radius <= 0.0f)
        return false;
    const Vec3 offset = ray.origin - center;
    const float b = offset.Dot(ray.direction);
    const float c = offset.Dot(offset) - radius * radius;
    const float discriminant = b * b - c;
    if (discriminant < 0.0f)
        return false;
    const float root = std::sqrt(discriminant);
    const float nearDistance = -b - root;
    const float farDistance = -b + root;
    if (farDistance < 0.0f)
        return false;
    outDistance = nearDistance >= 0.0f ? nearDistance : farDistance;
    return true;
}

bool EditorLightGizmoController::RayCapsuleHit(const Ray& ray, const Vec3& pointA, const Vec3& pointB, float radius,
                                               float& outDistance) {
    if (radius <= 0.0f)
        return false;
    const Vec3 segment = pointB - pointA;
    const float segmentLengthSq = segment.Dot(segment);
    if (segmentLengthSq <= 1e-12f)
        return RaySphereHit(ray, pointA, radius, outDistance);

    const Vec3 offset = ray.origin - pointA;
    const float a = ray.direction.Dot(ray.direction);
    const float b = ray.direction.Dot(segment);
    const float c = segmentLengthSq;
    const float d = ray.direction.Dot(offset);
    const float e = segment.Dot(offset);
    const float denominator = a * c - b * b;
    float rayDistance = denominator > 1e-8f ? (b * e - c * d) / denominator : 0.0f;
    float segmentFraction = denominator > 1e-8f ? (a * e - b * d) / denominator : e / c;

    if (rayDistance < 0.0f) {
        rayDistance = 0.0f;
        segmentFraction = std::clamp(e / c, 0.0f, 1.0f);
    } else if (segmentFraction < 0.0f) {
        segmentFraction = 0.0f;
        rayDistance = (std::max)(0.0f, -d / a);
    } else if (segmentFraction > 1.0f) {
        segmentFraction = 1.0f;
        rayDistance = (std::max)(0.0f, (b - d) / a);
    }

    const Vec3 rayPoint = ray.At(rayDistance);
    const Vec3 segmentPoint = pointA + segment * segmentFraction;
    const float distanceSq = (rayPoint - segmentPoint).LengthSq();
    if (distanceSq > radius * radius)
        return false;
    outDistance = (std::max)(0.0f, rayDistance - std::sqrt((std::max)(0.0f, radius * radius - distanceSq)));
    return true;
}

bool EditorLightGizmoController::FindClosestLightHit(EditorContext& context, const Ray& ray, Actor*& closestActor,
                                                     float& closestDistance) {
    Scene* scene = context.GetSceneViewScene();
    SceneViewport* viewport = context.GetSceneViewport();
    if (!scene || !viewport)
        return false;
    int viewportX = 0, viewportY = 0, viewportWidth = 0, viewportHeight = 0;
    viewport->GetViewportRect(viewportX, viewportY, viewportWidth, viewportHeight);
    (void)viewportX;
    (void)viewportY;
    (void)viewportWidth;

    bool found = false;
    const Camera& camera = viewport->GetCamera();
    scene->ForEach([&](Actor& actor) {
        auto* light = actor.GetComponent<LightComponent>();
        if (!actor.IsActive() || !light || !light->IsEnabled())
            return;
        const Vec3 origin = actor.GetWorldPosition();
        const float unitsPerPixel = WorldUnitsPerPixel(camera, origin, viewportHeight);
        if (unitsPerPixel <= 0.0f)
            return;

        float hitDistance = 0.0f;
        bool hit = false;
        if (light->GetLightType() == LightType::Point) {
            hit = RaySphereHit(ray, origin, kPointMarkerRadiusPixels * unitsPerPixel, hitDistance);
        } else {
            const Vec3 tip = origin + light->GetDirection() * (kArrowLengthPixels * unitsPerPixel);
            hit = RayCapsuleHit(ray, origin, tip, kArrowPickRadiusPixels * unitsPerPixel, hitDistance);
        }
        if (hit && hitDistance < closestDistance) {
            closestDistance = hitDistance;
            closestActor = &actor;
            found = true;
        }
    });
    return found;
}

void EditorLightGizmoController::EnsureMeshes() {
    if (!m_ArrowHeadMesh)
        m_ArrowHeadMesh = BuildWireCone(8u, true, 4u);
    if (!m_SpotConeMesh)
        m_SpotConeMesh = BuildWireCone(24u, false, 4u);
}

size_t EditorLightGizmoController::Submit(EditorContext& context) {
    Scene* scene = context.GetSceneViewScene();
    SceneViewport* viewport = context.GetSceneViewport();
    SceneRenderLayer* layer = context.GetSceneLayer();
    if (!scene || !viewport || (layer && !layer->IsSceneViewportActive()))
        return 0u;

    int viewportX = 0, viewportY = 0, viewportWidth = 0, viewportHeight = 0;
    viewport->GetViewportRect(viewportX, viewportY, viewportWidth, viewportHeight);
    (void)viewportX;
    (void)viewportY;
    (void)viewportWidth;
    if (viewportHeight <= 0)
        return 0u;

    EnsureMeshes();
    Actor* selectedActor = context.GetSelection().ResolveActor(*scene);
    const Camera& camera = viewport->GetCamera();
    size_t submitted = 0u;
    scene->ForEach([&](Actor& actor) {
        auto* light = actor.GetComponent<LightComponent>();
        if (!actor.IsActive() || !light || !light->IsEnabled())
            return;

        const Vec3 origin = actor.GetWorldPosition();
        const float unitsPerPixel = WorldUnitsPerPixel(camera, origin, viewportHeight);
        if (unitsPerPixel <= 0.0f)
            return;
        const bool selected = selectedActor == &actor;
        const Color compactColor = LightGizmoColor(light->GetColor(), selected, selected ? 1.0f : 0.85f);

        if (light->GetLightType() == LightType::Point) {
            submitted += DebugDraw::DrawSphere(*scene, origin, kPointMarkerRadiusPixels * unitsPerPixel, compactColor,
                                               0.0f, DebugDrawDepthMode::Always, DebugDrawViewMask::Authoring)
                             ? 1u
                             : 0u;
            if (selected) {
                submitted += DebugDraw::DrawSphere(*scene, origin, light->GetRange(),
                                                   LightGizmoColor(light->GetColor(), true, 0.72f), 0.0f,
                                                   DebugDrawDepthMode::Test, DebugDrawViewMask::Authoring)
                                 ? 1u
                                 : 0u;
                submitted += DebugDraw::DrawSphere(*scene, origin, light->GetRange(),
                                                   LightGizmoColor(light->GetColor(), true, 0.18f), 0.0f,
                                                   DebugDrawDepthMode::Always, DebugDrawViewMask::Authoring)
                                 ? 1u
                                 : 0u;
            }
            return;
        }

        const Vec3 direction = light->GetDirection();
        const float arrowLength = kArrowLengthPixels * unitsPerPixel;
        const float headLength = kArrowHeadLengthPixels * unitsPerPixel;
        const Vec3 headBase = origin + direction * (arrowLength - headLength);
        submitted += DebugDraw::DrawLine(*scene, origin, headBase, compactColor, 0.0f, DebugDrawDepthMode::Always,
                                         DebugDrawViewMask::Authoring)
                         ? 1u
                         : 0u;
        const Mat4 arrowHeadTransform =
            BuildAxialTransform(headBase, direction, kArrowHeadRadiusPixels * unitsPerPixel, headLength);
        submitted += DebugDraw::DrawMesh(*scene, m_ArrowHeadMesh, arrowHeadTransform, compactColor, 0.0f,
                                         DebugDrawDepthMode::Always, DebugDrawViewMask::Authoring)
                         ? 1u
                         : 0u;

        if (selected && light->GetLightType() == LightType::Spot) {
            const float range = light->GetRange();
            const float radius = range * std::tan(light->GetOuterConeAngle() * kDeg2Rad);
            const Mat4 coneTransform = BuildAxialTransform(origin, direction, radius, range);
            submitted += DebugDraw::DrawMesh(*scene, m_SpotConeMesh, coneTransform,
                                             LightGizmoColor(light->GetColor(), true, 0.72f), 0.0f,
                                             DebugDrawDepthMode::Test, DebugDrawViewMask::Authoring)
                             ? 1u
                             : 0u;
            submitted += DebugDraw::DrawMesh(*scene, m_SpotConeMesh, coneTransform,
                                             LightGizmoColor(light->GetColor(), true, 0.18f), 0.0f,
                                             DebugDrawDepthMode::Always, DebugDrawViewMask::Authoring)
                             ? 1u
                             : 0u;
        }
    });
    return submitted;
}

bool EditorGizmoController::ComputeLocalMatrix(const Mat4& world, const Mat4* parentWorld, Mat4& local) {
    local = world;
    if (!parentWorld)
        return true;

    Mat4 inverseParent;
    if (!Mat4Invert(*parentWorld, inverseParent))
        return false;
    // Row-vector convention: world = local * parentWorld.
    local = world * inverseParent;
    return true;
}

void EditorGizmoController::DrawAndApply(EditorContext& context, Actor& actor, const EditorPanelRect& viewportRect,
                                         const EditorGizmoState& state) {
#if defined(MYENGINE_ENABLE_IMGUI)
    using namespace EditorPanelHelpers;

    if (m_ActiveActorID != 0 && m_ActiveActorID != actor.GetID())
        Commit(context);

    float view[16]{};
    float projection[16]{};
    float world[16]{};
    auto* sceneViewport = context.GetSceneViewport();
    if (!sceneViewport)
        return;
    MatToFloat(sceneViewport->GetCamera().GetView(), view);
    MatToFloat(sceneViewport->GetCamera().GetProj(), projection);
    MatToFloat(actor.GetWorldMatrix(), world);

    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(viewportRect.x, viewportRect.y, viewportRect.width, viewportRect.height);
    ImGuizmo::Manipulate(view, projection, state.operation, state.mode, world);

    const bool isUsing = ImGuizmo::IsUsing();
    if (isUsing) {
        if (!m_WasUsing) {
            m_ActiveActorID = actor.GetID();
            m_InitialTransform = actor.GetTransform();
        }

        Mat4 editedWorld;
        FloatToMat(world, editedWorld);
        Mat4 editedLocal;
        if (Actor* parent = actor.GetParent()) {
            const Mat4 parentWorld = parent->GetWorldMatrix();
            if (!ComputeLocalMatrix(editedWorld, &parentWorld, editedLocal))
                return;
        } else {
            ComputeLocalMatrix(editedWorld, nullptr, editedLocal);
        }

        Transform editedTransform = actor.GetTransform();
        DecomposeLocal(editedLocal, editedTransform.position, editedTransform.rotation, editedTransform.scale);
        actor.GetTransform() = editedTransform;
    } else if (m_WasUsing) {
        Commit(context);
    }

    m_WasUsing = isUsing;
#else
    (void)context;
    (void)actor;
    (void)viewportRect;
    (void)state;
#endif
}

void EditorGizmoController::FinishInteraction(EditorContext& context) {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (m_WasUsing && !ImGuizmo::IsUsing())
        Commit(context);
#else
    (void)context;
#endif
}

bool EditorGizmoController::Commit(EditorContext& context) {
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(m_ActiveActorID) : nullptr;
    if (!actor) {
        m_ActiveActorID = 0;
        m_WasUsing = false;
        return false;
    }

    const Transform finalTransform = actor->GetTransform();
    if (EditorPanelHelpers::SameTransform(m_InitialTransform, finalTransform)) {
        m_ActiveActorID = 0;
        m_WasUsing = false;
        return false;
    }

    actor->GetTransform() = m_InitialTransform;
    const bool executed =
        context.GetCommandStack() &&
        context.GetCommandStack()->ExecuteCommand(
            std::make_unique<SetActorTransformCommand>(actor->GetID(), m_InitialTransform, finalTransform), context);
    m_ActiveActorID = 0;
    m_WasUsing = false;
    return executed;
}
