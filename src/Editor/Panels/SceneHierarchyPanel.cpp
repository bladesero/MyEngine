#include "Editor/EditorPanels.h"

#include "Editor/EditorCommand.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorLayout.h"
#include "Editor/EditorUndoUtil.h"
#include "Scene/Actor.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#endif

SceneHierarchyPanel::SceneHierarchyPanel():EditorPanel("sceneHierarchy","Scene Outliner"){}
void SceneHierarchyPanel::OnImGui(){if(IsVisible())DrawContent();}
void SceneHierarchyPanel::DrawActor(Actor* actor){
#if defined(MYENGINE_ENABLE_IMGUI)
    if(!actor)return;const bool selected=GetContext()->GetSelection().GetActorID()==actor->GetID();auto flags=ImGuiTreeNodeFlags_OpenOnArrow|ImGuiTreeNodeFlags_SpanAvailWidth|(selected?ImGuiTreeNodeFlags_Selected:0);
    if(actor->GetChildren().empty())flags|=ImGuiTreeNodeFlags_Leaf;const bool open=ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(actor->GetID())),flags,"%s",actor->GetName().c_str());
    if(ImGui::IsItemClicked())GetContext()->GetSelection().SelectActorID(actor->GetID());
    if(open){for(Actor* child:actor->GetChildren())DrawActor(child);ImGui::TreePop();}
#endif
}
void SceneHierarchyPanel::DrawContent(){
#if defined(MYENGINE_ENABLE_IMGUI)
    auto* context=GetContext();Scene* scene=context?context->GetScene():nullptr;if(!scene)return;const auto* viewport=ImGui::GetMainViewport();const auto rect=EditorLayout::Compute(viewport->WorkPos.x,viewport->WorkPos.y,viewport->WorkSize.x,viewport->WorkSize.y).outliner;
    ImGui::SetNextWindowPos({rect.x,rect.y});ImGui::SetNextWindowSize({rect.width,rect.height});ImGui::Begin("Scene Outliner");
    ImGui::BeginDisabled(!context->IsEditing());
    if(ImGui::Button("Create Actor")){const std::string before=SceneSerializer::SaveToString(*scene);const uint64_t old=context->GetSelection().GetActorID();Actor* actor=scene->CreateActor("Actor");const uint64_t id=actor->GetID();const std::string after=SceneSerializer::SaveToString(*scene);SceneSerializer::LoadFromString(*scene,before);context->GetCommandStack()->ExecuteCommand(EditorUndoUtil::MakeSceneSnapshotCommand("Create Actor",before,after,old,id),*context);}
    ImGui::SameLine();Actor* selected=context->GetSelection().ResolveActor(*scene);ImGui::BeginDisabled(!selected);if(ImGui::Button("Delete")){const std::string before=SceneSerializer::SaveToString(*scene);const uint64_t old=selected->GetID();scene->DestroyActor(selected);const std::string after=SceneSerializer::SaveToString(*scene);SceneSerializer::LoadFromString(*scene,before);context->GetCommandStack()->ExecuteCommand(EditorUndoUtil::MakeSceneSnapshotCommand("Delete Actor",before,after,old,0),*context);}ImGui::EndDisabled();
    for(Actor* actor:scene->GetRootActors())DrawActor(actor);ImGui::EndDisabled();ImGui::End();
#endif
}
