#include "Editor/EditorPanels.h"

#include "Assets/AssetManager.h"
#include "Assets/ModelAsset.h"
#include "Camera/Camera.h"
#include "Core/Logger.h"
#include "Editor/EditorCommand.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorImGuiBackend.h"
#include "Editor/EditorLayout.h"
#include "Editor/EditorPanelHelpers.h"
#include "Editor/EditorUndoUtil.h"
#include "Game/SceneRenderLayer.h"
#include "Scene/Actor.h"
#include "Scene/MeshRendererComponent.h"
#include "Scene/PrefabSystem.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#endif

#include <cmath>
#include <filesystem>

namespace {
constexpr const char kModelPayload[] = "MYENGINE_MODEL_PATH";
constexpr const char kPrefabPayload[] = "MYENGINE_PREFAB_PATH";
}

void ViewportPanel::DropPrefab(const std::string& path, float screenX, float screenY)
{
    EditorContext* context=GetContext();if(!context||!context->IsEditing())return;
    Scene* scene=context->GetScene();const std::string before=SceneSerializer::SaveToString(*scene);const uint64_t old=context->GetSelection().GetActorID();
    Transform placement;Math::Ray ray{};float distance=0.0f;
    if(context->GetSceneLayer()->BuildRayFromScreen(screenX,screenY,ray)&&std::fabs(ray.direction.y)>1e-5f&&(distance=-ray.origin.y/ray.direction.y)>0.0f)placement.position=ray.At(distance);
    else {Camera& camera=context->GetSceneLayer()->GetCamera();placement.position=camera.GetPosition()+camera.GetForward()*8.0f;}
    PrefabInstantiateOptions options;options.rootTransform=placement;std::string error;Actor* actor=PrefabSystem::Instantiate(*scene,path,options,&error);
    if(!actor){Logger::Warn("[Editor] Failed to instantiate prefab: ",error);return;}
    const uint64_t id=actor->GetID();const std::string after=SceneSerializer::SaveToString(*scene);SceneSerializer::LoadFromString(*scene,before);
    context->GetCommandStack()->ExecuteCommand(EditorUndoUtil::MakeSceneSnapshotCommand("Drop Prefab",before,after,old,id),*context);
}

ViewportPanel::ViewportPanel(std::shared_ptr<EditorGizmoState> state)
    : EditorPanel("viewport", "Scene View"), m_State(std::move(state))
{}

void ViewportPanel::OnImGui()
{
    if (IsVisible()) DrawContent();
}

void ViewportPanel::DropModel(const std::string& path, float screenX, float screenY)
{
    using namespace EditorPanelHelpers;

    EditorContext* context = GetContext();
    if (!context || !context->IsEditing() || !IsModel(path)) return;

    ModelHandle model = AssetManager::Get().Load<ModelAsset>(path);
    if (!model || !model->GetMesh()) {
        Logger::Warn("[Editor] Failed to load model: ", path);
        return;
    }

    Scene* scene = context->GetScene();
    const std::string before = SceneSerializer::SaveToString(*scene);
    const uint64_t previousSelection = context->GetSelection().GetActorID();

    std::string actorName = std::filesystem::path(path).stem().string();
    Actor* actor = scene->CreateActor(actorName.empty() ? "Mesh" : actorName);
    auto* renderer = actor->AddComponent<MeshRendererComponent>();
    renderer->SetMesh(model->GetMesh());
    const MaterialHandle material = model->GetMaterial(0);
    renderer->SetMaterial(material ? material : AssetManager::Get().GetDefaultMaterial());

    Math::Ray ray {};
    float distance = 0.0f;
    if (context->GetSceneLayer()->BuildRayFromScreen(screenX, screenY, ray) &&
        std::fabs(ray.direction.y) > 1e-5f &&
        (distance = -ray.origin.y / ray.direction.y) > 0.0f) {
        actor->GetTransform().position = ray.At(distance);
    } else {
        Camera& camera = context->GetSceneLayer()->GetCamera();
        actor->GetTransform().position = camera.GetPosition() + camera.GetForward() * 8.0f;
    }

    const uint64_t actorID = actor->GetID();
    const std::string after = SceneSerializer::SaveToString(*scene);
    SceneSerializer::LoadFromString(*scene, before);
    context->GetCommandStack()->ExecuteCommand(
        EditorUndoUtil::MakeSceneSnapshotCommand("Drop Model", before, after,
                                                 previousSelection, actorID),
        *context);
}

void ViewportPanel::DrawContent()
{
#if defined(MYENGINE_ENABLE_IMGUI)
    EditorContext* context = GetContext();
    if (!context || !m_State) return;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const EditorPanelRect rect = EditorLayout::Compute(
        viewport->WorkPos.x, viewport->WorkPos.y,
        viewport->WorkSize.x, viewport->WorkSize.y).viewport;

    ImGui::SetNextWindowPos({rect.x, rect.y});
    ImGui::SetNextWindowSize({rect.width, rect.height});
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground;
    if (!ImGui::Begin("Scene View", nullptr, flags)) {
        ImGui::End();
        ImGui::PopStyleVar();
        return;
    }

    const ImVec2 imageMin = ImGui::GetCursorScreenPos();
    const ImVec2 imageSize = ImGui::GetContentRegionAvail();
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
    const EditorPanelRect imageRect {
        imageMin.x, imageMin.y, imageSize.x, imageSize.y
    };

    context->GetSceneLayer()->SetEditorViewportRect(
        static_cast<int>(imageMin.x), static_cast<int>(imageMin.y),
        static_cast<int>(imageSize.x), static_cast<int>(imageSize.y));
    context->GetSceneLayer()->SetViewportInputEnabled(hovered);

    if (GpuTextureView* view = context->GetSceneLayer()->GetSceneColorView()) {
        void* texture = nullptr;
        if (auto* backend = context->GetImGuiBackend())
            texture = backend->GetTextureId(view);
        if (texture) ImGui::Image(reinterpret_cast<ImTextureID>(texture), imageSize);
    }

    if (context->IsEditing() && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kModelPayload)) {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            DropModel(static_cast<const char*>(payload->Data), mouse.x, mouse.y);
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kPrefabPayload)) {
            const ImVec2 mouse=ImGui::GetIO().MousePos;DropPrefab(static_cast<const char*>(payload->Data),mouse.x,mouse.y);
        }
        ImGui::EndDragDropTarget();
    }

    Actor* actor = context->GetSelection().ResolveActor(*context->GetScene());
    if (context->IsEditing() && actor) {
        m_GizmoController.DrawAndApply(*context, *actor, imageRect, *m_State);
    } else {
        m_GizmoController.FinishInteraction(*context);
    }

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !ImGuizmo::IsOver() && !ImGuizmo::IsUsing()) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        m_PickingController.Pick(*context, mouse.x, mouse.y);
    }

    ImGui::End();
    ImGui::PopStyleVar();
#endif
}
