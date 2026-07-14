#include "Editor/EditorViewportControllers.h"

#include "Assets/MeshAsset.h"
#include "Camera/Camera.h"
#include "Editor/EditorCommand.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorPanelHelpers.h"
#include "Game/SceneViewportController.h"
#include "Math/Mat4Inverse.h"
#include "Scene/Actor.h"
#include "Scene/MeshRendererComponent.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <cfloat>

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

    if (closestActor) {
        const EditorSelectionWorldKind world =
            context.IsInspectingPlayWorld() ? EditorSelectionWorldKind::Play : EditorSelectionWorldKind::Editor;
        context.GetSelection().Select(
            EditorSelectObject::MakeActor(closestActor->GetHandle(), closestActor->GetID(), world));
    } else
        context.GetSelection().Clear();
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
