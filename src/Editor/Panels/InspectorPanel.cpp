#include "Editor/EditorPanels.h"

#include "Editor/EditorContext.h"
#include "Editor/EditorCommand.h"
#include "Editor/EditorInspectorSection.h"
#include "Editor/EditorLayout.h"
#include "Editor/InspectorSections.h"
#include "Core/Logger.h"
#include "Scene/Actor.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"
#include "Scene/PrefabSystem.h"

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#endif

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <sstream>

namespace {
std::string ReadTextFile(const std::filesystem::path& path){std::ifstream input(path,std::ios::binary);return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};}
bool WriteTextFile(const std::filesystem::path& path,const std::string& text){std::ofstream output(path,std::ios::binary|std::ios::trunc);output.write(text.data(),static_cast<std::streamsize>(text.size()));return output.good();}
}

InspectorPanel::InspectorPanel(std::shared_ptr<EditorGizmoState> state)
    : EditorPanel("inspector", "Inspector"),
      m_State(std::move(state)),
      m_SectionRegistry()
{
    auto sections = CreateDefaultInspectorSections();
    for (auto& section : sections) m_SectionRegistry.Register(std::move(section));
}

InspectorPanel::~InspectorPanel() = default;

void InspectorPanel::OnAttach(EditorContext& context)
{
    EditorPanel::OnAttach(context);
    m_SelectedObject = context.GetSelection().GetPrimaryObject();
    m_SelectionListenerID = context.GetSelection().SubscribeSelectionChanged(
        [this](const EditorSelectionChangedEvent& event) {
            OnSelectionChanged(event);
        });
}

void InspectorPanel::OnDetach()
{
    if (EditorContext* context = GetContext()) {
        context->GetSelection().UnsubscribeSelectionChanged(m_SelectionListenerID);
    }
    m_SelectionListenerID = 0;
    m_SelectedObject = {};
    m_Transaction.Cancel();
    EditorPanel::OnDetach();
}

void InspectorPanel::OnSelectionChanged(const EditorSelectionChangedEvent& event)
{
    m_SelectedObject = event.current;
    m_Transaction.Cancel();
}

void InspectorPanel::OnImGui()
{
    if (IsVisible()) DrawContent();
}

void InspectorPanel::DrawContent()
{
#if defined(MYENGINE_ENABLE_IMGUI)
    EditorContext* context = GetContext();
    Scene* scene = context ? context->GetScene() : nullptr;
    if (!scene) return;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const EditorPanelRect rect = EditorLayout::Compute(
        viewport->WorkPos.x, viewport->WorkPos.y,
        viewport->WorkSize.x, viewport->WorkSize.y).inspector;
    ImGui::SetNextWindowPos({rect.x, rect.y});
    ImGui::SetNextWindowSize({rect.width, rect.height});
    ImGui::Begin("Inspector");

    Actor* actor = m_SelectedObject.IsActor()
        ? context->GetSelection().ResolveActor(*scene) : nullptr;
    const uint64_t selection = actor ? actor->GetID() : 0;
    const std::string before = actor ? SceneSerializer::SaveToString(*scene) : std::string{};
    bool directCommand=false;

    ImGui::BeginDisabled(!context->IsEditing());
    if(actor && actor->IsPrefabRoot()){
        ImGui::Text("Prefab: %s",actor->GetPrefabAssetPath().c_str());
        std::string error;bool refreshed=false;
        if(ImGui::Button("Apply All")){
            const auto sourcePath=PrefabSystem::ResolvePrefabPath(actor->GetPrefabAssetPath());const std::string sourceBefore=ReadTextFile(sourcePath);
            refreshed=PrefabSystem::ApplyAll(*actor,&error);
            if(!refreshed)Logger::Warn("[Editor] Apply prefab failed: ",error);
            else {const std::string sourceAfter=ReadTextFile(sourcePath),sceneAfter=SceneSerializer::SaveToString(*scene);WriteTextFile(sourcePath,sourceBefore);SceneSerializer::LoadFromString(*scene,before);
                auto apply=[sourcePath,sourceAfter,sceneAfter,selection](EditorContext& value){if(!WriteTextFile(sourcePath,sourceAfter)||!value.GetScene()||!SceneSerializer::LoadFromString(*value.GetScene(),sceneAfter))return false;value.GetSelection().SelectActorID(selection);return true;};
                auto undo=[sourcePath,sourceBefore,before,selection](EditorContext& value){if(!WriteTextFile(sourcePath,sourceBefore)||!value.GetScene()||!SceneSerializer::LoadFromString(*value.GetScene(),before))return false;value.GetSelection().SelectActorID(selection);return true;};
                directCommand=context->GetCommandStack()->ExecuteCommand(std::make_unique<LambdaEditorCommand>("Apply Prefab",apply,undo),*context);refreshed=directCommand;
            }
        }
        ImGui::SameLine();if(ImGui::Button("Revert All")){refreshed=PrefabSystem::RevertAll(*actor,&error);if(!refreshed)Logger::Warn("[Editor] Revert prefab failed: ",error);}
        ImGui::SameLine();if(ImGui::Button("Unpack")){if(!PrefabSystem::Unpack(*actor,&error))Logger::Warn("[Editor] Unpack prefab failed: ",error);else context->MarkSceneDirty();}
        ImGui::SameLine();if(ImGui::Button("Select Source"))context->GetSelection().SelectAssetPath(PrefabSystem::ResolvePrefabPath(actor->GetPrefabAssetPath()).string());
        if(refreshed){context->MarkSceneDirty();actor=context->GetSelection().ResolveActor(*scene);if(!actor){ImGui::EndDisabled();ImGui::End();return;}}
        ImGui::Separator();
    }
    if (actor) {
        std::array<char, 256> actorName {};
        std::strncpy(actorName.data(), actor->GetName().c_str(), actorName.size() - 1);
        if (ImGui::InputText("Name", actorName.data(), actorName.size())) {
            actor->SetName(actorName.data());
        }
        ImGui::Text("ID: %llu", static_cast<unsigned long long>(actor->GetID()));

        ImGui::Separator();
        ImGui::TextUnformatted("Gizmo");
        if (ImGui::RadioButton("Translate", m_State->operation == ImGuizmo::TRANSLATE)) {
            m_State->operation = ImGuizmo::TRANSLATE;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Rotate", m_State->operation == ImGuizmo::ROTATE)) {
            m_State->operation = ImGuizmo::ROTATE;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Scale", m_State->operation == ImGuizmo::SCALE)) {
            m_State->operation = ImGuizmo::SCALE;
        }
    }

    for (const auto& section : m_SectionRegistry.GetSections()) {
        if (section->CanDraw(m_SelectedObject, *context)) section->Draw(*context);
    }
    ImGui::EndDisabled();

    if (actor) {
        const std::string after = SceneSerializer::SaveToString(*scene);
        if (!directCommand && before != after && !m_Transaction.IsActive()) {
            m_Transaction.Begin("Inspector Edit", before, selection);
        }
        if (m_Transaction.IsActive() && !ImGui::IsAnyItemActive()) {
            m_Transaction.Commit(*context);
        }
    }

    ImGui::End();
#endif
}
