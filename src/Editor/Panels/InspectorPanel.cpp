#include "Editor/EditorPanels.h"

#include "Editor/EditorContext.h"
#include "Editor/EditorCommand.h"
#include "Editor/EditorInspectorSection.h"
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

#if defined(MYENGINE_ENABLE_IMGUI)
bool ShouldCaptureInspectorEditSnapshot(bool hasActorSelection,
                                        bool canEditSelection,
                                        bool transactionActive)
{
    if (!hasActorSelection || !canEditSelection || transactionActive) return false;
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)) {
        return false;
    }

    const ImGuiIO& io = ImGui::GetIO();
    return ImGui::IsAnyItemActive() ||
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
        ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
        ImGui::IsMouseClicked(ImGuiMouseButton_Middle) ||
        io.InputQueueCharacters.Size > 0 ||
        ImGui::IsKeyPressed(ImGuiKey_Backspace) ||
        ImGui::IsKeyPressed(ImGuiKey_Delete) ||
        ImGui::IsKeyPressed(ImGuiKey_Enter) ||
        ImGui::IsKeyPressed(ImGuiKey_Space) ||
        ImGui::IsKeyPressed(ImGuiKey_Tab) ||
        ImGui::IsKeyPressed(ImGuiKey_LeftArrow) ||
        ImGui::IsKeyPressed(ImGuiKey_RightArrow) ||
        ImGui::IsKeyPressed(ImGuiKey_UpArrow) ||
        ImGui::IsKeyPressed(ImGuiKey_DownArrow);
}
#endif
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

void InspectorPanel::DrawContent()
{
#if defined(MYENGINE_ENABLE_IMGUI)
    EditorContext* context = GetContext();
    Scene* scene = context ? context->GetInspectorScene() : nullptr;
    if (!scene) return;

    Actor* actor = m_SelectedObject.IsActor()
        ? context->GetSelection().ResolveActor(*scene) : nullptr;
    const uint64_t selection = actor ? actor->GetID() : 0;
    bool directCommand=false;

    const bool canEditSelection = context->CanEditSelection();
    const bool captureInspectorSnapshot = ShouldCaptureInspectorEditSnapshot(
        actor != nullptr, canEditSelection, m_Transaction.IsActive());
    const std::string before = captureInspectorSnapshot
        ? SceneSerializer::SaveToString(*scene) : std::string{};

    if (m_SelectedObject.IsActor() &&
        m_SelectedObject.GetWorldKind() == EditorSelectionWorldKind::Play) {
        ImGui::TextColored({1.0f, 0.75f, 0.25f, 1.0f},
                           "PlayWorld runtime object - read-only");
        ImGui::Separator();
    }

    ImGui::BeginDisabled(!canEditSelection);
    if(actor && actor->IsPrefabRoot()){
        ImGui::Text("Prefab: %s",actor->GetPrefabAssetPath().c_str());
        std::string error;bool refreshed=false;
        if(ImGui::Button("Apply All")){
            const std::string commandBefore = SceneSerializer::SaveToString(*scene);
            const auto sourcePath=PrefabSystem::ResolvePrefabPath(actor->GetPrefabAssetPath());const std::string sourceBefore=ReadTextFile(sourcePath);
            refreshed=PrefabSystem::ApplyAll(*actor,&error);
            if(!refreshed)Logger::Warn("[Editor] Apply prefab failed: ",error);
            else {const std::string sourceAfter=ReadTextFile(sourcePath),sceneAfter=SceneSerializer::SaveToString(*scene);WriteTextFile(sourcePath,sourceBefore);SceneSerializer::LoadFromString(*scene,commandBefore);
                auto apply=[sourcePath,sourceAfter,sceneAfter,selection](EditorContext& value){if(!WriteTextFile(sourcePath,sourceAfter)||!value.GetScene()||!SceneSerializer::LoadFromString(*value.GetScene(),sceneAfter))return false;value.GetSelection().SelectActorID(selection);return true;};
                auto undo=[sourcePath,sourceBefore,commandBefore,selection](EditorContext& value){if(!WriteTextFile(sourcePath,sourceBefore)||!value.GetScene()||!SceneSerializer::LoadFromString(*value.GetScene(),commandBefore))return false;value.GetSelection().SelectActorID(selection);return true;};
                directCommand=context->GetCommandStack()->ExecuteCommand(std::make_unique<LambdaEditorCommand>("Apply Prefab",apply,undo),*context);refreshed=directCommand;
            }
        }
        ImGui::SameLine();if(ImGui::Button("Revert All")){refreshed=PrefabSystem::RevertAll(*actor,&error);if(!refreshed)Logger::Warn("[Editor] Revert prefab failed: ",error);}
        ImGui::SameLine();if(ImGui::Button("Unpack")){if(!PrefabSystem::Unpack(*actor,&error))Logger::Warn("[Editor] Unpack prefab failed: ",error);else context->MarkSceneDirty();}
        ImGui::SameLine();if(ImGui::Button("Select Source"))context->GetSelection().SelectAssetPath(PrefabSystem::ResolvePrefabPath(actor->GetPrefabAssetPath()).string());
        if(refreshed){context->MarkSceneDirty();actor=context->GetSelection().ResolveActor(*scene);if(!actor){ImGui::EndDisabled();return;}}
        ImGui::Separator();
    }
    if (canEditSelection && actor) {
        std::array<char, 256> actorName {};
        std::strncpy(actorName.data(), actor->GetName().c_str(), actorName.size() - 1);
        if (ImGui::InputText("Name", actorName.data(), actorName.size())) {
            actor->SetName(actorName.data());
        }
        ImGui::Text("ID: %llu", static_cast<unsigned long long>(actor->GetID()));
    }

    for (const auto& section : m_SectionRegistry.GetSections()) {
        if (section->CanDraw(m_SelectedObject, *context)) section->Draw(*context);
    }
    ImGui::EndDisabled();

    if (actor && captureInspectorSnapshot) {
        const std::string after = SceneSerializer::SaveToString(*scene);
        if (!directCommand && before != after && !m_Transaction.IsActive()) {
            m_Transaction.Begin("Inspector Edit", before, selection);
        }
    }
    if (actor && m_Transaction.IsActive() && !ImGui::IsAnyItemActive()) {
        m_Transaction.Commit(*context);
    }
#endif
}
