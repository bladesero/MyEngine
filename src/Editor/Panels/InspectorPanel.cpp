#include "Editor/EditorPanels.h"

#include "Editor/EditorContext.h"
#include "Editor/EditorInspectorSection.h"
#include "Editor/EditorLayout.h"
#include "Editor/InspectorSections.h"
#include "Scene/Actor.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#endif

#include <algorithm>
#include <array>
#include <cstring>

InspectorPanel::InspectorPanel(std::shared_ptr<EditorGizmoState> state)
    : EditorPanel("inspector", "Inspector"),
      m_State(std::move(state)),
      m_SectionRegistry()
{
    auto sections = CreateDefaultInspectorSections();
    for (auto& section : sections) m_SectionRegistry.Register(std::move(section));
}

InspectorPanel::~InspectorPanel() = default;

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

    Actor* actor = context->GetSelection().ResolveActor(*scene);
    if (!actor) {
        m_Transaction.Cancel();
        ImGui::TextDisabled("Select an actor.");
        ImGui::End();
        return;
    }

    const uint64_t selection = actor->GetID();
    const std::string before = SceneSerializer::SaveToString(*scene);

    ImGui::BeginDisabled(!context->IsEditing());
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

    for (const auto& section : m_SectionRegistry.GetSections()) {
        if (section->CanDraw(context->GetSelection())) section->Draw(*context);
    }
    ImGui::EndDisabled();

    const std::string after = SceneSerializer::SaveToString(*scene);
    if (before != after && !m_Transaction.IsActive()) {
        m_Transaction.Begin("Inspector Edit", before, selection);
    }
    if (m_Transaction.IsActive() && !ImGui::IsAnyItemActive()) {
        m_Transaction.Commit(*context);
    }

    ImGui::End();
#endif
}
