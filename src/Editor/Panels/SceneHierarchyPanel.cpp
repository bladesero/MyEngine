#include "Editor/EditorPanels.h"

#include "Editor/EditorCommand.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorAssetRegistry.h"
#include "Editor/EditorImportService.h"
#include "Editor/EditorLayout.h"
#include "Editor/EditorUndoUtil.h"
#include "Editor/EditorDragDrop.h"
#include "Scene/Actor.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"
#include "Scene/PrefabSystem.h"
#include "Core/Logger.h"

#include <filesystem>
#include <vector>
#include <cstring>

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#endif

namespace {
constexpr const char kActorPayload[] = "MYENGINE_ACTOR_ID";
constexpr const char kPrefabPayload[] = "MYENGINE_PREFAB_PATH";

#if defined(MYENGINE_ENABLE_IMGUI)
void DrawDropHighlight(ImVec2 min, ImVec2 max, ImU32 color, float thickness = 2.0f)
{
    auto* dl = ImGui::GetWindowDrawList();
    dl->AddRect(min, max, color, 0.0f, 0, thickness);
}
#endif
}

SceneHierarchyPanel::SceneHierarchyPanel():EditorPanel("sceneHierarchy","Scene Outliner"){
    RegisterContextMenuHandler([this](const ContextMenuContext& ctx,
                                      EditorContextMenu& menu){
        if(ctx.target!=ContextMenuContext::Target::Actor)return;
        auto* actor=static_cast<Actor*>(ctx.pointer);
        if(!actor)return;
        auto* context=GetContext();
        Scene* scene=context?context->GetScene():nullptr;
        if(!scene||!context->IsEditing())return;

        menu.AddAction("Create Child Actor",[this,context,scene,actor](){
            ActorCreateDesc desc;
            desc.name = "Actor";
            auto cmd = EditorUndoUtil::MakeCreateActorCommand(desc, 0);
            if (cmd) {
                auto* createCmd = static_cast<CreateActorCommand*>(cmd.get());
                context->GetCommandStack()->ExecuteCommand(std::move(cmd), *context);
                if (Actor* newActor = context->GetSelection().ResolveActor(*scene)) {
                    newActor->SetParent(actor);
                }
            }
        });

        menu.AddSeparator();

        menu.AddAction("Duplicate",[this,context,scene,actor](){
            const std::string json = EditorUndoUtil::SerializeActorSubtree(*actor);
            uint64_t newID = 0; // auto-assign
            ActorCreateDesc desc;
            desc.name = actor->GetName() + " (Copy)";
            desc.transform = actor->GetTransform();
            if (actor->GetParent()) {
                // Will be parented after creation
            }
            auto cmd = std::make_unique<CreateActorCommand>(desc, newID);
            // Use full snapshot for subtree clone (simplest correct approach)
            const std::string before = SceneSerializer::SaveToString(*scene);
            const uint64_t oldId = context->GetSelection().GetActorID();
            Actor* clone = scene->CreateActor(desc.name);
            if (clone) {
                clone->GetTransform() = actor->GetTransform();
                if (actor->GetParent()) clone->SetParent(actor->GetParent());
                const uint64_t cloneId = clone->GetID();
                const std::string after = SceneSerializer::SaveToString(*scene);
                SceneSerializer::LoadFromString(*scene, before);
                context->GetCommandStack()->ExecuteCommand(
                    EditorUndoUtil::MakeSceneSnapshotCommand("Duplicate Actor", before, after, oldId, cloneId), *context);
            }
        });

        menu.AddAction("Rename", [this,context,scene,actor](){
            m_PendingRenameID = actor->GetID();
            std::strncpy(m_RenameBuffer, actor->GetName().c_str(), sizeof(m_RenameBuffer) - 1);
            m_RenameBuffer[sizeof(m_RenameBuffer) - 1] = '\0';
        });

        menu.AddSeparator();

        const char* activeLabel = actor->IsActiveSelf() ? "Set Inactive" : "Set Active";
        menu.AddAction(activeLabel, [this,context,actor](){
            context->GetCommandStack()->ExecuteCommand(
                EditorUndoUtil::MakeSetActiveCommand(*actor, !actor->IsActiveSelf()), *context);
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

void SceneHierarchyPanel::OnImGui()
{
    if (IsVisible()) DrawContent();
}

void SceneHierarchyPanel::DrawToolbar(){
#if defined(MYENGINE_ENABLE_IMGUI)
    ImGui::InputTextWithHint("##Search","Search...",m_SearchFilter,sizeof(m_SearchFilter));
    ImGui::SameLine();
    if (ImGui::Button("+")) {
        auto* ctx = GetContext();
        Scene* sc = ctx ? ctx->GetScene() : nullptr;
        if (sc && ctx->IsEditing()) {
            const std::string before = SceneSerializer::SaveToString(*sc);
            const uint64_t old = ctx->GetSelection().GetActorID();
            Actor* a = sc->CreateActor("Actor");
            const uint64_t nid = a->GetID();
            const std::string after = SceneSerializer::SaveToString(*sc);
            SceneSerializer::LoadFromString(*sc, before);
            ctx->GetCommandStack()->ExecuteCommand(
                EditorUndoUtil::MakeSceneSnapshotCommand("Create Actor", before, after, old, nid), *ctx);
        }
    }
    ImGui::Separator();
#endif
}

namespace {
// Recursively check if an actor or any of its children match the filter
bool ActorMatchesFilter(const Actor& actor, const char* filter) {
    if (!filter || !*filter) return true;
    if (actor.GetName().find(filter) != std::string::npos) return true;
    for (const auto* child : actor.GetChildren()) {
        if (ActorMatchesFilter(*child, filter)) return true;
    }
    return false;
}
}

void SceneHierarchyPanel::DrawActor(Actor* actor){
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!actor) return;

    // Search filter
    if (m_SearchFilter[0] && !ActorMatchesFilter(*actor, m_SearchFilter)) return;

    auto* context = GetContext();
    Scene* scene = context ? context->GetScene() : nullptr;
    const bool isSelected = context && context->GetSelection().IsSelected(actor->GetID());
    const bool hasChildren = !actor->GetChildren().empty();
    const bool isActive = actor->IsActiveSelf();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;
    if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

    // Inactive actors shown greyed out
    if (!isActive) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));

    // Eye icon toggle for active state
    ImGui::PushID(static_cast<int>(actor->GetID()));
    if (ImGui::SmallButton(isActive ? "v" : "x")) {
        context->GetCommandStack()->ExecuteCommand(
            EditorUndoUtil::MakeSetActiveCommand(*actor, !isActive), *context);
    }
    ImGui::SameLine();

    // Inline rename
    if (m_PendingRenameID == actor->GetID()) {
        ImGui::SetKeyboardFocusHere();
        if (ImGui::InputText("##rename", m_RenameBuffer, sizeof(m_RenameBuffer),
                              ImGuiInputTextFlags_EnterReturnsTrue)) {
            std::string newName(m_RenameBuffer);
            if (!newName.empty() && newName != actor->GetName()) {
                context->GetCommandStack()->ExecuteCommand(
                    EditorUndoUtil::MakeSetNameCommand(*actor, newName), *context);
            }
            m_PendingRenameID = 0;
        }
        if (!ImGui::IsItemActive() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            m_PendingRenameID = 0;
        }
    } else {
        bool open = ImGui::TreeNodeEx(actor->GetName().c_str(), flags);

        // Selection handling
        if (ImGui::IsItemClicked()) {
            if (ImGui::GetIO().KeyCtrl) {
                context->GetSelection().Select(
                    EditorSelectObject::MakeActor(actor->GetHandle(), actor->GetID()),
                    EditorSelectionMode::Toggle);
            } else {
                context->GetSelection().Select(
                    EditorSelectObject::MakeActor(actor->GetHandle(), actor->GetID()));
            }
        }

        // Drag source
        {
            ActorDragDropSource dragSource(actor->GetID(), actor->GetName());
            if (dragSource.Draw()) m_DraggedActor = actor->GetHandle();
        }

        // Context menu
        m_ActorRightClicked = false;
        if (EditorContextMenu::DetectItem("##ActorCtx")) m_ActorRightClicked = true;
        {
            EditorContextMenu actorMenu("##ActorCtx");
            if (actorMenu.IsOpen()) {
                ContextMenuContext ctxCtx;
                ctxCtx.target = ContextMenuContext::Target::Actor;
                ctxCtx.pointer = actor;
                ShowContextMenu(ctxCtx, actorMenu);
            }
        }

        // Drop target for re-parenting
        if (ImGui::BeginDragDropTarget()) {
            const ImGuiPayload* previewPayload = ImGui::GetDragDropPayload();
            if (previewPayload) {
                const ImVec2 areaMin(ImGui::GetItemRectMin());
                const ImVec2 areaMax(ImGui::GetItemRectMax());
                if (previewPayload->IsDataType(kActorPayload))
                    DrawDropHighlight(areaMin, areaMax, IM_COL32(80, 200, 120, 120),
                                      previewPayload->IsDelivery() ? 3.0f : 2.0f);
                else if (previewPayload->IsDataType(kPrefabPayload))
                    DrawDropHighlight(areaMin, areaMax, IM_COL32(200, 160, 80, 120),
                                      previewPayload->IsDelivery() ? 3.0f : 2.0f);
            }

            if (scene && context->IsEditing()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kActorPayload)) {
                    if (payload->DataSize >= sizeof(uint64_t)) {
                        uint64_t sourceId = 0;
                        std::memcpy(&sourceId, payload->Data, sizeof(sourceId));
                        Actor* source = scene->FindByID(sourceId);
                        if (source && source != actor && source->GetParent() != actor) {
                            const uint64_t beforeParent = source->GetParent() ? source->GetParent()->GetID() : uint64_t(0);
                            context->GetCommandStack()->ExecuteCommand(
                                EditorUndoUtil::MakeSetParentCommand(*source, beforeParent, actor->GetID()), *context);
                        }
                    }
                }
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kPrefabPayload)) {
                    const std::string path(static_cast<const char*>(payload->Data), payload->DataSize);
                    std::string error;
                    PrefabInstantiateOptions opts;
                    opts.parent = actor->GetHandle();
                    Actor* instance = PrefabSystem::Instantiate(*scene, path, opts, &error);
                    if (instance) {
                        context->GetSelection().Select(EditorSelectObject::MakeActor(
                            instance->GetHandle(), instance->GetID()));
                    } else {
                        Logger::Warn("[Editor] Instantiate prefab failed: ", error);
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        if (open && hasChildren) {
            for (Actor* child : actor->GetChildren()) { DrawActor(child); }
            ImGui::TreePop();
        }
    }

    if (!isActive) ImGui::PopStyleColor();
    ImGui::PopID();
#endif
}

void SceneHierarchyPanel::HandleDragDropTarget(Actor* targetParent){
#if defined(MYENGINE_ENABLE_IMGUI)
    auto* context=GetContext();Scene* scene=context?context->GetScene():nullptr;
    if(!scene||!context->IsEditing())return;
    if(ImGui::BeginDragDropTarget()){
        const ImGuiPayload* previewPayload = ImGui::GetDragDropPayload();
        if(previewPayload)
        {
            const ImVec2 areaMin(ImGui::GetItemRectMin());
            const ImVec2 areaMax(ImGui::GetItemRectMax());
            if(previewPayload->IsDataType(kActorPayload))
                DrawDropHighlight(areaMin, areaMax, IM_COL32(80, 200, 120, 120),
                                  previewPayload->IsDelivery() ? 3.0f : 2.0f);
            else if(previewPayload->IsDataType(kPrefabPayload))
                DrawDropHighlight(areaMin, areaMax, IM_COL32(200, 160, 80, 120),
                                  previewPayload->IsDelivery() ? 3.0f : 2.0f);
        }

        // Actor drop: re-parent
        if(const ImGuiPayload* payload=ImGui::AcceptDragDropPayload(kActorPayload)){
            if(payload->DataSize>=sizeof(uint64_t)){
                uint64_t sourceId=0;
                std::memcpy(&sourceId,payload->Data,sizeof(sourceId));
                Actor* source=scene->FindByID(sourceId);
                if(source && source->GetParent()!=targetParent){
                    const uint64_t beforeParent = source->GetParent() ? source->GetParent()->GetID() : uint64_t(0);
                    const uint64_t afterParent = targetParent ? targetParent->GetID() : uint64_t(0);
                    context->GetCommandStack()->ExecuteCommand(
                        EditorUndoUtil::MakeSetParentCommand(*source, beforeParent, afterParent), *context);
                }
            }
        }

        // Prefab drop: instantiate as child
        if(const ImGuiPayload* payload=ImGui::AcceptDragDropPayload(kPrefabPayload)){
            const std::string before=SceneSerializer::SaveToString(*scene);
            const uint64_t old=context->GetSelection().GetActorID();
            std::string error;
            PrefabInstantiateOptions opts;
            opts.parent = targetParent ? targetParent->GetHandle() : ActorHandle{};
            Actor* actor=PrefabSystem::Instantiate(*scene,static_cast<const char*>(payload->Data),opts,&error);
            if(actor){
                const uint64_t id=actor->GetID();
                const std::string after=SceneSerializer::SaveToString(*scene);
                SceneSerializer::LoadFromString(*scene,before);
                context->GetCommandStack()->ExecuteCommand(
                    EditorUndoUtil::MakeSceneSnapshotCommand("Instantiate Prefab",before,after,old,id),*context);
            context->GetSelection().Select(EditorSelectObject::MakeActor(id));
            }else Logger::Warn("[Editor] Prefab instance creation failed: ",error);
        }
        ImGui::EndDragDropTarget();
    }
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
    std::vector<Actor*> rootActors = scene->GetRootActors();
    for(Actor* actor:rootActors){if(!actor->GetParent())DrawActor(actor);}

    if(!m_ActorRightClicked && EditorContextMenu::DetectWindow("##EmptyCtxMenu")){
    }
    {
        EditorContextMenu emptyMenu("##EmptyCtxMenu");
        if(emptyMenu.IsOpen()){
            ContextMenuContext ctxCtx;
            ShowContextMenu(ctxCtx,emptyMenu);
        }
    }

    {
        const float availY = ImGui::GetContentRegionAvail().y;
        if(availY > 2.0f) ImGui::Dummy({ImGui::GetContentRegionAvail().x, availY - 2.0f});
    }
    HandleDragDropTarget(nullptr);

    ImGui::EndDisabled();ImGui::End();
#endif
}
