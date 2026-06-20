#include "Editor/EditorPanels.h"

#include "Editor/EditorCommand.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorAssetRegistry.h"
#include "Editor/EditorImportService.h"
#include "Editor/EditorLayout.h"
#include "Editor/EditorUndoUtil.h"
#include "Editor/EditorContextMenu.h"
#include "Scene/Actor.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"
#include "Scene/PrefabSystem.h"
#include "Core/Logger.h"

#include <filesystem>

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#endif

namespace {
constexpr const char kActorPayload[] = "MYENGINE_ACTOR_ID";
}

SceneHierarchyPanel::SceneHierarchyPanel():EditorPanel("sceneHierarchy","Scene Outliner"){
    // Register context-menu handlers so items are contributed declaratively.
    RegisterContextMenuHandler([this](const ContextMenuContext& ctx,
                                      EditorContextMenu& menu){
        if(ctx.target!=ContextMenuContext::Target::Actor)return;
        auto* actor=static_cast<Actor*>(ctx.pointer);
        if(!actor)return;
        auto* context=GetContext();
        Scene* scene=context?context->GetScene():nullptr;
        if(!scene||!context->IsEditing())return;

        menu.AddAction("Create Child Actor",[this,context,scene,actor](){
            const std::string before=SceneSerializer::SaveToString(*scene);
            const uint64_t oldId=context->GetSelection().GetActorID();
            Actor* child=scene->CreateActor("Actor");
            child->SetParent(actor);
            const uint64_t newId=child->GetID();
            const std::string after=SceneSerializer::SaveToString(*scene);
            SceneSerializer::LoadFromString(*scene,before);
            context->GetCommandStack()->ExecuteCommand(
                EditorUndoUtil::MakeSceneSnapshotCommand("Create Child Actor",before,after,oldId,newId),*context);
        });

        menu.AddSeparator();

        menu.AddAction("Delete",[this,context,scene,actor](){
            const std::string before=SceneSerializer::SaveToString(*scene);
            const uint64_t oldId=actor->GetID();
            scene->DestroyActor(actor);
            const std::string after=SceneSerializer::SaveToString(*scene);
            SceneSerializer::LoadFromString(*scene,before);
            context->GetCommandStack()->ExecuteCommand(
                EditorUndoUtil::MakeSceneSnapshotCommand("Delete Actor",before,after,oldId,0),*context);
        });

        menu.AddAction("Create Prefab",[this,context,scene,actor](){
            const std::string before=SceneSerializer::SaveToString(*scene);
            const uint64_t id=actor->GetID();
            const ActorHandle parentHandle=actor->GetParent()?actor->GetParent()->GetHandle():ActorHandle{};
            const Transform transform=actor->GetTransform();
            const auto directory=context->GetContentRoot()/"Prefabs";
            std::filesystem::create_directories(directory);
            const auto path=EditorImportService::MakeUniqueContentPath(directory,actor->GetName(),".prefab.json");
            std::string error;
            if(PrefabSystem::SaveSubtree(*actor,path,&error)){
                scene->QueueDestroyActor(actor->GetHandle());
                scene->FlushCommands();
                PrefabInstantiateOptions options;
                options.parent=parentHandle;options.rootTransform=transform;options.persistentRootID=id;
                Actor* instance=PrefabSystem::Instantiate(*scene,path,options,&error);
                if(instance){
                    const std::string after=SceneSerializer::SaveToString(*scene);
                    SceneSerializer::LoadFromString(*scene,before);
                    context->GetCommandStack()->ExecuteCommand(
                        EditorUndoUtil::MakeSceneSnapshotCommand("Create Prefab",before,after,id,id),*context);
                    if(context->GetAssetRegistry())context->GetAssetRegistry()->Refresh();
                }else Logger::Warn("[Editor] Prefab instance creation failed: ",error);
            }else Logger::Warn("[Editor] Prefab creation failed: ",error);
        });
    });

    RegisterContextMenuHandler([this](const ContextMenuContext& ctx,
                                      EditorContextMenu& menu){
        if(ctx.target!=ContextMenuContext::Target::None)return;
        auto* context=GetContext();
        Scene* scene=context?context->GetScene():nullptr;
        if(!scene||!context->IsEditing())return;

        menu.AddAction("Create Actor",[this,context,scene](){
            const std::string before=SceneSerializer::SaveToString(*scene);
            const uint64_t oldId=context->GetSelection().GetActorID();
            Actor* actor=scene->CreateActor("Actor");
            const uint64_t newId=actor->GetID();
            const std::string after=SceneSerializer::SaveToString(*scene);
            SceneSerializer::LoadFromString(*scene,before);
            context->GetCommandStack()->ExecuteCommand(
                EditorUndoUtil::MakeSceneSnapshotCommand("Create Actor",before,after,oldId,newId),*context);
        });
    });
}
void SceneHierarchyPanel::OnImGui(){if(IsVisible())DrawContent();}

void SceneHierarchyPanel::DrawToolbar() {
#if defined(MYENGINE_ENABLE_IMGUI)
    EditorContext* context=GetContext();Scene* scene=context?context->GetScene():nullptr;
    if(!scene||!context->IsEditing())return;

    if(ImGui::Button("Create Actor")){
        const std::string before=SceneSerializer::SaveToString(*scene);
        const uint64_t old=context->GetSelection().GetActorID();
        Actor* actor=scene->CreateActor("Actor");
        const uint64_t id=actor->GetID();
        const std::string after=SceneSerializer::SaveToString(*scene);
        SceneSerializer::LoadFromString(*scene,before);
        context->GetCommandStack()->ExecuteCommand(
            EditorUndoUtil::MakeSceneSnapshotCommand("Create Actor",before,after,old,id),*context);
    }
    ImGui::Separator();
#endif
}

void SceneHierarchyPanel::DrawActor(Actor* actor){
#if defined(MYENGINE_ENABLE_IMGUI)
    if(!actor)return;
    EditorContext* context=GetContext();
    Scene* scene=context?context->GetScene():nullptr;
    if(!scene)return;

    const uint64_t actorId=actor->GetID();
    const bool selected=context->GetSelection().GetActorID()==actorId;
    auto flags=ImGuiTreeNodeFlags_OpenOnArrow|ImGuiTreeNodeFlags_SpanAvailWidth
              |(selected?ImGuiTreeNodeFlags_Selected:0);
    if(actor->GetChildren().empty())flags|=ImGuiTreeNodeFlags_Leaf;
    const bool open=ImGui::TreeNodeEx(
        reinterpret_cast<void*>(static_cast<uintptr_t>(actorId)),flags,
        "%s",actor->GetName().c_str());

    // Left-click: select
    if(ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        context->GetSelection().SelectActorID(actorId);

    // Right-click: select actor first, then show context menu
    if(ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)){
        context->GetSelection().SelectActorID(actorId);
        m_ActorRightClicked=true;
        ImGui::OpenPopup("##ActorCtxMenu");
    }

    // Context menu popup -- built from registered handlers.
    // Scoped so ~EditorContextMenu calls EndPopup before drag-drop / TreePop.
    {
        EditorContextMenu menu("##ActorCtxMenu");
        if(menu.IsOpen()){
            ContextMenuContext ctxCtx;
            ctxCtx.target=ContextMenuContext::Target::Actor;
            ctxCtx.pointer=actor;
            ctxCtx.id=actorId;
            ShowContextMenu(ctxCtx,menu);
        }
    }

    // Drag source
    if(ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)){
        m_DraggedActor=actor->GetHandle();
        ImGui::SetDragDropPayload(kActorPayload,&actorId,sizeof(actorId));
        ImGui::TextUnformatted(actor->GetName().c_str());
        ImGui::EndDragDropSource();
    }

    // Drag target
    HandleDragDropTarget(actor);

    if(open){
        for(Actor* child:actor->GetChildren())DrawActor(child);
        ImGui::TreePop();
    }
#endif
}

void SceneHierarchyPanel::HandleDragDropTarget(Actor* targetParent) {
#if defined(MYENGINE_ENABLE_IMGUI)
    EditorContext* context=GetContext();
    Scene* scene=context?context->GetScene():nullptr;
    if(!scene||!context->IsEditing())return;

    if(!ImGui::BeginDragDropTarget())return;

    if(const ImGuiPayload* payload=ImGui::AcceptDragDropPayload(kActorPayload)){
        if(payload->DataSize>=sizeof(uint64_t)){
            uint64_t sourceId=0;
            std::memcpy(&sourceId,payload->Data,sizeof(sourceId));
            Actor* source=scene->FindByID(sourceId);
            if(source && source!=targetParent){
                bool valid=true;
                for(Actor* parent=targetParent;parent;parent=parent->GetParent())
                    if(parent==source){valid=false;break;}
                if(valid){
                    const std::string before=SceneSerializer::SaveToString(*scene);
                    const uint64_t old=context->GetSelection().GetActorID();
                    scene->QueueSetParent(source->GetHandle(),targetParent->GetHandle());
                    scene->FlushCommands();
                    const std::string after=SceneSerializer::SaveToString(*scene);
                    SceneSerializer::LoadFromString(*scene,before);
                    context->GetCommandStack()->ExecuteCommand(
                        EditorUndoUtil::MakeSceneSnapshotCommand("Re-parent Actor",before,after,old,sourceId),*context);
                }
            }
        }
    }
    ImGui::EndDragDropTarget();
#endif
}

void SceneHierarchyPanel::DrawContent(){
#if defined(MYENGINE_ENABLE_IMGUI)
    auto* context=GetContext();Scene* scene=context?context->GetScene():nullptr;
    if(!scene)return;
    const auto* viewport=ImGui::GetMainViewport();
    const auto rect=EditorLayout::Compute(
        viewport->WorkPos.x,viewport->WorkPos.y,
        viewport->WorkSize.x,viewport->WorkSize.y).outliner;
    ImGui::SetNextWindowPos({rect.x,rect.y});
    ImGui::SetNextWindowSize({rect.width,rect.height});
    ImGui::Begin("Scene Outliner");

    ImGui::BeginDisabled(!context->IsEditing());
    DrawToolbar();

    m_DraggedActor={};
    m_ActorRightClicked=false;
    for(Actor* actor:scene->GetRootActors())DrawActor(actor);

    // Right-click on empty area: built from registered handlers.
    // Scoped so ~EditorContextMenu calls EndPopup before drag-drop targets.
    if(!m_ActorRightClicked && EditorContextMenu::DetectWindow("##EmptyCtxMenu")){
        // opened by the static helper
    }
    {
        EditorContextMenu emptyMenu("##EmptyCtxMenu");
        if(emptyMenu.IsOpen()){
            ContextMenuContext ctxCtx;
            ShowContextMenu(ctxCtx,emptyMenu);
        }
    }

    // Prefab & actor drag-drop targets on empty area
    if(ImGui::BeginDragDropTarget()){
        if(const ImGuiPayload* payload=ImGui::AcceptDragDropPayload("MYENGINE_PREFAB_PATH")){
            const std::string before=SceneSerializer::SaveToString(*scene);
            const uint64_t old=context->GetSelection().GetActorID();
            std::string error;
            Actor* actor=PrefabSystem::Instantiate(*scene,static_cast<const char*>(payload->Data),{},&error);
            if(actor){
                const uint64_t id=actor->GetID();
                const std::string after=SceneSerializer::SaveToString(*scene);
                SceneSerializer::LoadFromString(*scene,before);
                context->GetCommandStack()->ExecuteCommand(
                    EditorUndoUtil::MakeSceneSnapshotCommand("Instantiate Prefab",before,after,old,id),*context);
            }else Logger::Warn("[Editor] Prefab instance creation failed: ",error);
        }
        if(const ImGuiPayload* payload=ImGui::AcceptDragDropPayload(kActorPayload)){
            if(payload->DataSize>=sizeof(uint64_t)){
                uint64_t sourceId=0;
                std::memcpy(&sourceId,payload->Data,sizeof(sourceId));
                Actor* source=scene->FindByID(sourceId);
                if(source && source->GetParent()){
                    const std::string before=SceneSerializer::SaveToString(*scene);
                    const uint64_t old=context->GetSelection().GetActorID();
                    scene->QueueSetParent(source->GetHandle(),ActorHandle{});
                    scene->FlushCommands();
                    const std::string after=SceneSerializer::SaveToString(*scene);
                    SceneSerializer::LoadFromString(*scene,before);
                    context->GetCommandStack()->ExecuteCommand(
                        EditorUndoUtil::MakeSceneSnapshotCommand("Un-parent Actor",before,after,old,sourceId),*context);
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::EndDisabled();ImGui::End();
#endif
}
