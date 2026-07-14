#include "Editor/EditorPanels.h"

#include "Editor/EditorContext.h"
#include "Editor/EditorAssetRegistry.h"
#include "Editor/EditorImportService.h"
#include "Editor/EditorOperators.h"
#include "Editor/EditorDragDrop.h"
#include "Editor/EditorUI/EditorAngelScriptDomain.h"
#include "Editor/UI/EditorIcons.h"
#include "Editor/UI/EditorWidgets.h"
#include "Scene/Actor.h"
#include "Scene/Scene.h"
#include "Core/Logger.h"
#include "UI/Core/UICanvasComponent.h"
#include "UI/Core/UIComponents.h"

#include <algorithm>
#include <cctype>
#include <vector>
#include <cstring>

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#endif

namespace {
constexpr const char kActorPayload[] = "MYENGINE_ACTOR_ID";
constexpr const char kPrefabPayload[] = "MYENGINE_PREFAB_PATH";
namespace EditorIcons = Editor::UI::EditorIcons;
namespace EditorWidgets = Editor::UI::EditorWidgets;

#if defined(MYENGINE_ENABLE_IMGUI)
void DrawDropHighlight(ImVec2 min, ImVec2 max, ImU32 color, float thickness = 2.0f) {
    auto* dl = ImGui::GetWindowDrawList();
    dl->AddRect(min, max, color, 0.0f, 0, thickness);
}

enum class ActorDropZone {
    Before,
    Into,
    After,
};

ActorDropZone GetActorDropZone(ImVec2 min, ImVec2 max) {
    const float height = std::max(1.0f, max.y - min.y);
    const float y = ImGui::GetMousePos().y;
    if (y < min.y + height * 0.25f)
        return ActorDropZone::Before;
    if (y > max.y - height * 0.25f)
        return ActorDropZone::After;
    return ActorDropZone::Into;
}

void DrawActorDropCue(ImVec2 min, ImVec2 max, ActorDropZone zone, bool delivery) {
    const ImU32 color = IM_COL32(80, 200, 120, 160);
    const float thickness = delivery ? 3.0f : 2.0f;
    auto* dl = ImGui::GetWindowDrawList();
    if (zone == ActorDropZone::Into) {
        dl->AddRect(min, max, color, 0.0f, 0, thickness);
        return;
    }
    const float y = zone == ActorDropZone::Before ? min.y : max.y;
    dl->AddLine(ImVec2(min.x, y), ImVec2(max.x, y), color, thickness);
}

void DrawActorFilterMatchHighlight(ImVec2 min, ImVec2 max) {
    auto* dl = ImGui::GetWindowDrawList();
    const ImU32 fill = IM_COL32(80, 160, 255, 28);
    const ImU32 stripe = IM_COL32(80, 160, 255, 190);
    dl->AddRectFilled(min, max, fill, 2.0f);
    dl->AddRectFilled(min, ImVec2(min.x + 3.0f, max.y), stripe, 1.0f);
}
#endif

Actor* GetNextSibling(Scene& scene, const Actor& actor, const Actor* exclude = nullptr) {
    const std::vector<Actor*> siblings = actor.GetParent() ? actor.GetParent()->GetChildren() : scene.GetRootActors();
    bool found = false;
    for (Actor* sibling : siblings) {
        if (!sibling || sibling == exclude)
            continue;
        if (found)
            return sibling;
        if (sibling == &actor)
            found = true;
    }
    return nullptr;
}

Actor* GetPreviousSibling(Scene& scene, const Actor& actor) {
    const std::vector<Actor*> siblings = actor.GetParent() ? actor.GetParent()->GetChildren() : scene.GetRootActors();
    Actor* previous = nullptr;
    for (Actor* sibling : siblings) {
        if (sibling == &actor)
            return previous;
        if (sibling)
            previous = sibling;
    }
    return nullptr;
}

Actor* ResolveSelectedActor(EditorContext& context) {
    Scene* scene = context.GetScene();
    return scene ? context.GetSelection().ResolveActor(*scene) : nullptr;
}

bool IsDescendantOf(const Actor& candidate, const Actor& ancestor) {
    for (Actor* parent = candidate.GetParent(); parent; parent = parent->GetParent()) {
        if (parent == &ancestor)
            return true;
    }
    return false;
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool ContainsCaseInsensitive(std::string_view text, std::string_view filter) {
    if (filter.empty())
        return true;
    std::string textString(text);
    std::string filterString(filter);
    return ToLower(std::move(textString)).find(ToLower(std::move(filterString))) != std::string::npos;
}

bool ExecuteMoveActorDrop(EditorContext& context, Scene& scene, Actor& source, Actor* afterParent,
                          Actor* afterNextSibling) {
    if (&source == afterParent || (&source == afterNextSibling))
        return false;
    if (afterParent && IsDescendantOf(*afterParent, source))
        return false;
    if (afterNextSibling && afterNextSibling->GetParent() != afterParent)
        return false;

    Actor* beforeNextSibling = GetNextSibling(scene, source);
    const uint64_t beforeParentID = source.GetParent() ? source.GetParent()->GetID() : uint64_t(0);
    const uint64_t beforeNextID = beforeNextSibling ? beforeNextSibling->GetID() : uint64_t(0);
    const uint64_t afterParentID = afterParent ? afterParent->GetID() : uint64_t(0);
    const uint64_t afterNextID = afterNextSibling ? afterNextSibling->GetID() : uint64_t(0);
    if (beforeParentID == afterParentID && beforeNextID == afterNextID)
        return false;

    if (EditorOperators* operators = context.GetOperators()) {
        return operators->DragDrop().ApplyActorDrop(context, source.GetID(), afterParentID, afterNextID);
    }
    EditorDragDropOperator dragDropOperator;
    return dragDropOperator.ApplyActorDrop(context, source.GetID(), afterParentID, afterNextID);
}

void AddUIActorMenu(EditorContext& context, Actor* parent, EditorContextMenu& menu) {
    auto addPreset = [&context, parent](const char* presetID) {
        const uint64_t parentID = parent ? parent->GetID() : 0;
        if (auto* operators = context.GetOperators()) {
            operators->Commands().CreateUIActor(context, presetID, parentID);
        }
    };
    if (parent == nullptr) {
        menu.AddAction("UI/Create Canvas", [addPreset]() { addPreset("canvas"); });
        return;
    }
    if (!parent->GetComponent<UICanvasComponent>() && !parent->GetComponent<UIRectTransformComponent>()) {
        return;
    }
    menu.AddSeparator();
    menu.AddAction("UI/Text", [addPreset]() { addPreset("text"); });
    menu.AddAction("UI/Image", [addPreset]() { addPreset("image"); });
    menu.AddAction("UI/Button", [addPreset]() { addPreset("button"); });
    menu.AddAction("UI/Slider", [addPreset]() { addPreset("slider"); });
    menu.AddAction("UI/Progress Bar", [addPreset]() { addPreset("progressBar"); });
    menu.AddAction("UI/Scroll View", [addPreset]() { addPreset("scrollView"); });
    menu.AddAction("Layout/Vertical", [addPreset]() { addPreset("verticalLayout"); });
    menu.AddAction("Layout/Horizontal", [addPreset]() { addPreset("horizontalLayout"); });
    menu.AddAction("Layout/Grid", [addPreset]() { addPreset("gridLayout"); });
}
} // namespace

SceneHierarchyPanel::SceneHierarchyPanel() : EditorPanel("sceneHierarchy", "Scene Outliner") {
    RegisterContextMenuHandler([this](const ContextMenuContext& ctx, EditorContextMenu& menu) {
        if (ctx.target != ContextMenuContext::Target::Actor)
            return;
        auto* actor = static_cast<Actor*>(ctx.pointer);
        if (!actor)
            return;
        auto* context = GetContext();
        Scene* scene = context ? context->GetScene() : nullptr;
        if (!scene || !context->IsEditing())
            return;

        menu.AddAction("Create Child Actor", [this, context, scene, actor]() {
            if (auto* operators = context->GetOperators()) {
                operators->Commands().CreateChildActor(*context, "Actor", actor->GetID());
            }
        });

        AddUIActorMenu(*context, actor, menu);

        menu.AddSeparator();

        menu.AddAction("Create Empty Parent", [context, actor]() {
            if (auto* operators = context->GetOperators()) {
                operators->Selection().SelectActor(*context, actor->GetID());
                operators->Commands().CreateEmptyParent(*context, actor->GetID());
            }
        });

        if (actor->GetParent()) {
            menu.AddAction("Unparent", [context, actor]() {
                if (auto* operators = context->GetOperators()) {
                    operators->Selection().SelectActor(*context, actor->GetID());
                    operators->Commands().UnparentActor(*context, actor->GetID());
                }
            });
        }

        if (GetPreviousSibling(*scene, *actor)) {
            menu.AddAction("Move Up", [context, actor]() {
                if (auto* operators = context->GetOperators()) {
                    operators->Selection().SelectActor(*context, actor->GetID());
                    operators->Commands().MoveActorUp(*context, actor->GetID());
                }
            });
        }

        if (GetNextSibling(*scene, *actor)) {
            menu.AddAction("Move Down", [context, actor]() {
                if (auto* operators = context->GetOperators()) {
                    operators->Selection().SelectActor(*context, actor->GetID());
                    operators->Commands().MoveActorDown(*context, actor->GetID());
                }
            });
        }

        menu.AddSeparator();

        if (actor->GetParent()) {
            menu.AddAction("Select Parent", [context, actor]() {
                if (auto* operators = context->GetOperators()) {
                    operators->Selection().SelectActor(*context, actor->GetParent()->GetID());
                }
            });
        }

        if (Actor* previous = GetPreviousSibling(*scene, *actor)) {
            menu.AddAction("Select Previous Sibling", [context, previous]() {
                if (auto* operators = context->GetOperators()) {
                    operators->Selection().SelectActor(*context, previous->GetID());
                }
            });
        }

        if (Actor* next = GetNextSibling(*scene, *actor)) {
            menu.AddAction("Select Next Sibling", [context, next]() {
                if (auto* operators = context->GetOperators()) {
                    operators->Selection().SelectActor(*context, next->GetID());
                }
            });
        }

        if (!actor->GetChildren().empty()) {
            menu.AddAction("Select Subtree", [context, actor]() {
                if (auto* operators = context->GetOperators()) {
                    operators->Selection().SelectActorSubtree(*context, actor->GetID(), true);
                }
            });
            menu.AddAction("Select Children", [context, actor]() {
                if (auto* operators = context->GetOperators()) {
                    operators->Selection().SelectActorSubtree(*context, actor->GetID(), false);
                }
            });
        }

        menu.AddSeparator();

        menu.AddAction("Duplicate", [this, context, scene, actor]() {
            if (auto* operators = context->GetOperators()) {
                if (!context->GetSelection().IsSelected(actor->GetID())) {
                    operators->Selection().SelectActor(*context, actor->GetID());
                }
                operators->Commands().DuplicateSelection(*context);
            }
        });

        menu.AddAction("Rename", [this, context, scene, actor]() {
            m_PendingRenameID = actor->GetID();
            std::strncpy(m_RenameBuffer, actor->GetName().c_str(), sizeof(m_RenameBuffer) - 1);
            m_RenameBuffer[sizeof(m_RenameBuffer) - 1] = '\0';
        });

        menu.AddSeparator();

        const char* activeLabel = actor->IsActiveSelf() ? "Set Inactive" : "Set Active";
        menu.AddAction(activeLabel, [this, context, actor]() {
            if (auto* operators = context->GetOperators()) {
                operators->Commands().SetActorActive(*context, actor->GetID(), !actor->IsActiveSelf());
            }
        });

        menu.AddSeparator();

        menu.AddAction("Delete", [this, context, scene, actor]() {
            if (auto* operators = context->GetOperators()) {
                operators->Commands().DeleteActor(*context, actor->GetID());
            }
        });

        menu.AddAction("Create Prefab", [this, context, scene, actor]() {
            if (auto* operators = context->GetOperators()) {
                operators->Prefabs().CreatePrefabFromActor(*context, actor->GetID());
            }
        });
    });

    RegisterContextMenuHandler([this](const ContextMenuContext& ctx, EditorContextMenu& menu) {
        if (ctx.target != ContextMenuContext::Target::None)
            return;
        auto* context = GetContext();
        Scene* scene = context ? context->GetScene() : nullptr;
        if (!scene || !context->IsEditing())
            return;

        menu.AddAction("Create Actor", [this, context, scene]() {
            if (auto* operators = context->GetOperators()) {
                operators->Commands().CreateActor(*context, "Actor");
            }
        });
        AddUIActorMenu(*context, nullptr, menu);
    });

    RegisterContextMenuHandler([this](const ContextMenuContext& ctx, EditorContextMenu& menu) {
        if (ctx.target != ContextMenuContext::Target::Actor)
            return;
        auto* context = GetContext();
        EditorAngelScriptDomain* domain = context ? context->GetEditorScriptDomain() : nullptr;
        if (!context || !domain || !domain->IsLoaded() || !domain->GetConfig().enableContextMenuExtensions)
            return;
        const auto& extensions = domain->GetRegistry().GetActorContextMenus();
        if (extensions.empty())
            return;
        menu.AddSeparator();
        for (const auto& extension : extensions) {
            std::string error;
            if (!domain->ExecuteExtension(extension.callback, "actorContext", *context, &error) && !error.empty()) {
                Logger::Warn("[EditorScript] Actor context menu failed: ", error);
            }
        }
    });
}

bool SceneHierarchyPanel::HandleEditorAction(EditorContext& context, std::string_view actionID) {
    if (actionID == "edit.delete" || actionID == "edit.duplicate" || actionID == "edit.copy" ||
        actionID == "edit.paste") {
        if (!context.IsEditing())
            return false;
        auto* operators = context.GetOperators();
        if (!operators)
            return false;
        if (actionID == "edit.paste") {
            if (!operators->Commands().HasActorClipboard())
                return false;
            return operators->Commands().PasteSelection(context);
        }
        if (!context.GetSelection().HasActor())
            return false;
        if (actionID == "edit.delete") {
            return operators->Commands().DeleteSelection(context);
        }
        if (actionID == "edit.duplicate") {
            return operators->Commands().DuplicateSelection(context);
        }
        return operators->Commands().CopySelection(context);
    }

    if (actionID == "edit.selectAll") {
        if (!context.IsEditing())
            return false;
        Scene* scene = context.GetScene();
        if (!scene)
            return false;

        if (m_SearchFilter[0]) {
            m_SearchMatches.clear();
            m_SearchMatches.reserve(scene->ActorCount());
            for (Actor* actor : scene->GetRootActors()) {
                if (actor && !actor->GetParent())
                    RebuildSearchCache(actor);
            }
        }

        auto* operators = context.GetOperators();
        if (!operators)
            return false;
        bool selectedAny = false;
        scene->ForEach([&](Actor& actor) {
            if (!ActorMatchesSearch(actor))
                return;
            if (!selectedAny) {
                operators->Selection().SelectActor(context, actor.GetID(), EditorSelectionIntentMode::Replace);
                selectedAny = true;
            } else {
                operators->Selection().SelectActor(context, actor.GetID(), EditorSelectionIntentMode::Add);
            }
        });
        return selectedAny;
    }

    if (actionID == "hierarchy.expandAll" || actionID == "hierarchy.collapseAll") {
        if (!context.IsEditing() || !context.GetScene())
            return false;
        m_OpenRequest = actionID == "hierarchy.expandAll" ? 1 : -1;
        return true;
    }

    if (actionID == "hierarchy.createEmptyParent") {
        Actor* actor = ResolveSelectedActor(context);
        if (!actor)
            return false;
        if (auto* operators = context.GetOperators()) {
            return operators->Commands().CreateEmptyParent(context, actor->GetID()) != 0;
        }
        return false;
    }

    if (actionID == "hierarchy.unparent") {
        Actor* actor = ResolveSelectedActor(context);
        if (!actor)
            return false;
        if (auto* operators = context.GetOperators()) {
            return operators->Commands().UnparentActor(context, actor->GetID());
        }
        return false;
    }

    if (actionID == "hierarchy.moveUp") {
        Actor* actor = ResolveSelectedActor(context);
        if (!actor)
            return false;
        if (auto* operators = context.GetOperators()) {
            return operators->Commands().MoveActorUp(context, actor->GetID());
        }
        return false;
    }

    if (actionID == "hierarchy.moveDown") {
        Actor* actor = ResolveSelectedActor(context);
        if (!actor)
            return false;
        if (auto* operators = context.GetOperators()) {
            return operators->Commands().MoveActorDown(context, actor->GetID());
        }
        return false;
    }

    if (actionID == "hierarchy.selectChildren") {
        Actor* actor = ResolveSelectedActor(context);
        if (!actor || actor->GetChildren().empty())
            return false;
        if (auto* operators = context.GetOperators()) {
            return operators->Selection().SelectActorSubtree(context, actor->GetID(), false);
        }
        return false;
    }

    if (actionID == "hierarchy.selectSubtree") {
        Actor* actor = ResolveSelectedActor(context);
        if (!actor || actor->GetChildren().empty())
            return false;
        if (auto* operators = context.GetOperators()) {
            return operators->Selection().SelectActorSubtree(context, actor->GetID(), true);
        }
        return false;
    }

    if (actionID == "hierarchy.selectParent") {
        Actor* actor = ResolveSelectedActor(context);
        Actor* parent = actor ? actor->GetParent() : nullptr;
        if (!parent)
            return false;
        if (auto* operators = context.GetOperators()) {
            return operators->Selection().SelectActor(context, parent->GetID());
        }
        return false;
    }

    if (actionID == "hierarchy.selectPreviousSibling") {
        Scene* scene = context.GetScene();
        Actor* actor = ResolveSelectedActor(context);
        Actor* previous = scene && actor ? GetPreviousSibling(*scene, *actor) : nullptr;
        if (!previous)
            return false;
        if (auto* operators = context.GetOperators()) {
            return operators->Selection().SelectActor(context, previous->GetID());
        }
        return false;
    }

    if (actionID == "hierarchy.selectNextSibling") {
        Scene* scene = context.GetScene();
        Actor* actor = ResolveSelectedActor(context);
        Actor* next = scene && actor ? GetNextSibling(*scene, *actor) : nullptr;
        if (!next)
            return false;
        if (auto* operators = context.GetOperators()) {
            return operators->Selection().SelectActor(context, next->GetID());
        }
        return false;
    }

    if (actionID != "edit.rename" || !context.CanEditSelection())
        return false;
    Scene* scene = context.GetScene();
    Actor* actor = scene ? context.GetSelection().ResolveActor(*scene) : nullptr;
    if (!actor)
        return false;
    m_PendingRenameID = actor->GetID();
    std::strncpy(m_RenameBuffer, actor->GetName().c_str(), sizeof(m_RenameBuffer) - 1);
    m_RenameBuffer[sizeof(m_RenameBuffer) - 1] = '\0';
    return true;
}

bool SceneHierarchyPanel::CanHandleEditorAction(const EditorContext& context, std::string_view actionID) const {
    if (!context.IsEditing())
        return false;
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(context.GetSelection().GetActorID()) : nullptr;
    if (actionID == "edit.delete" || actionID == "edit.duplicate" || actionID == "edit.copy") {
        return actor != nullptr && context.GetOperators() != nullptr;
    }
    if (actionID == "edit.paste") {
        auto* operators = context.GetOperators();
        return scene && operators && context.CanEditScene() && operators->Commands().HasActorClipboard();
    }
    if (actionID == "edit.selectAll") {
        return scene && scene->ActorCount() > 0;
    }
    if (actionID == "hierarchy.expandAll" || actionID == "hierarchy.collapseAll") {
        return scene != nullptr;
    }
    if (actionID == "edit.rename" || actionID == "hierarchy.createEmptyParent") {
        return actor != nullptr && context.CanEditSelection();
    }
    if (actionID == "hierarchy.unparent") {
        return actor && actor->GetParent() && context.CanEditSelection();
    }
    if (actionID == "hierarchy.moveUp") {
        return scene && actor && GetPreviousSibling(*scene, *actor) && context.CanEditSelection();
    }
    if (actionID == "hierarchy.moveDown") {
        return scene && actor && GetNextSibling(*scene, *actor) && context.CanEditSelection();
    }
    if (actionID == "hierarchy.selectChildren") {
        return scene && actor && !actor->GetChildren().empty();
    }
    if (actionID == "hierarchy.selectSubtree") {
        return scene && actor && !actor->GetChildren().empty();
    }
    if (actionID == "hierarchy.selectParent") {
        return actor && actor->GetParent();
    }
    if (actionID == "hierarchy.selectPreviousSibling") {
        return scene && actor && GetPreviousSibling(*scene, *actor);
    }
    if (actionID == "hierarchy.selectNextSibling") {
        return scene && actor && GetNextSibling(*scene, *actor);
    }
    return false;
}

void SceneHierarchyPanel::DrawToolbar() {
#if defined(MYENGINE_ENABLE_IMGUI)
    auto* ctx = GetContext();
    if (ctx) {
        EditorWidgets::SvgIcon(*ctx, EditorIcons::Search, 14.0f);
        ImGui::SameLine();
    }
    ImGui::InputTextWithHint("##Search", "Search...", m_SearchFilter, sizeof(m_SearchFilter));
    ImGui::SameLine();
    if (ctx && EditorWidgets::IconButton(*ctx, "CreateActor", EditorIcons::Actor, "Create Actor")) {
        if (ctx->IsEditing()) {
            if (auto* operators = ctx->GetOperators()) {
                operators->Commands().CreateActor(*ctx, "Actor");
            }
        }
    }
    ImGui::SameLine();
    if (ctx && EditorWidgets::IconButton(*ctx, "CreateUICanvas", EditorIcons::Input, "Create UI Canvas")) {
        if (ctx->IsEditing()) {
            if (auto* operators = ctx->GetOperators()) {
                operators->Commands().CreateUIActor(*ctx, "canvas");
            }
        }
    }
    ImGui::SameLine();
    if (ctx && EditorWidgets::IconButton(*ctx, "ExpandAll", "+", "Expand All")) {
        HandleEditorAction(*ctx, "hierarchy.expandAll");
    }
    ImGui::SameLine();
    if (ctx && EditorWidgets::IconButton(*ctx, "CollapseAll", "-", "Collapse All")) {
        HandleEditorAction(*ctx, "hierarchy.collapseAll");
    }
    ImGui::Separator();

    ImGui::TextUnformatted("Filters");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputTextWithHint("##TagFilter", "Tag", m_TagFilter, sizeof(m_TagFilter));
    ImGui::SameLine();
    ImGui::Checkbox("Layer", &m_LayerFilterEnabled);
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_LayerFilterEnabled);
    ImGui::SetNextItemWidth(72.0f);
    ImGui::InputScalar("##LayerFilter", ImGuiDataType_U32, &m_LayerFilter);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);
    ImGui::InputTextWithHint("##ComponentFilter", "Component", m_ComponentFilter, sizeof(m_ComponentFilter));
    const bool hasExtraFilters = m_TagFilter[0] || m_ComponentFilter[0] || m_LayerFilterEnabled;
    if (hasExtraFilters) {
        ImGui::SameLine();
        if (ctx && EditorWidgets::IconButton(*ctx, "ClearHierarchyFilters", "x", "Clear filters")) {
            m_TagFilter[0] = '\0';
            m_ComponentFilter[0] = '\0';
            m_LayerFilter = 0;
            m_LayerFilterEnabled = false;
        }
    }
    ImGui::Separator();
#endif
}

bool SceneHierarchyPanel::RebuildSearchCache(Actor* actor) {
    if (!actor)
        return false;
    bool matches = ActorMatchesOwnFilters(*actor);
    for (Actor* child : actor->GetChildren()) {
        matches = RebuildSearchCache(child) || matches;
    }
    m_SearchMatches[actor->GetID()] = matches;
    return matches;
}

bool SceneHierarchyPanel::HasHierarchyFilters() const {
    return m_SearchFilter[0] != '\0' || m_TagFilter[0] != '\0' || m_ComponentFilter[0] != '\0' || m_LayerFilterEnabled;
}

bool SceneHierarchyPanel::ActorMatchesOwnFilters(const Actor& actor) const {
    if (m_SearchFilter[0] && !ContainsCaseInsensitive(actor.GetName(), m_SearchFilter)) {
        return false;
    }
    if (m_TagFilter[0] && !ContainsCaseInsensitive(actor.GetTag(), m_TagFilter)) {
        return false;
    }
    if (m_LayerFilterEnabled && actor.GetLayer() != m_LayerFilter) {
        return false;
    }
    if (m_ComponentFilter[0]) {
        bool hasMatchingComponent = false;
        actor.ForEachComponent([&](const Component& component) {
            if (ContainsCaseInsensitive(component.GetTypeName(), m_ComponentFilter)) {
                hasMatchingComponent = true;
            }
        });
        if (!hasMatchingComponent)
            return false;
    }
    return true;
}

bool SceneHierarchyPanel::ActorMatchesSearch(const Actor& actor) const {
    if (!HasHierarchyFilters()) {
        return true;
    }
    auto found = m_SearchMatches.find(actor.GetID());
    return found != m_SearchMatches.end() && found->second;
}

void SceneHierarchyPanel::CollectVisibleActorOrder(Actor* actor, std::vector<uint64_t>& order) const {
    if (!actor || !ActorMatchesSearch(*actor))
        return;
    order.push_back(actor->GetID());
    for (Actor* child : actor->GetChildren()) {
        CollectVisibleActorOrder(child, order);
    }
}

void SceneHierarchyPanel::DrawActor(Actor* actor) {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!actor)
        return;

    // Search filter
    if (!ActorMatchesSearch(*actor))
        return;

    auto* context = GetContext();
    Scene* scene = context ? context->GetScene() : nullptr;
    const bool isSelected = context && context->GetSelection().IsSelected(actor->GetID());
    const bool hasChildren = !actor->GetChildren().empty();
    const bool isActive = actor->IsActiveSelf();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if (isSelected)
        flags |= ImGuiTreeNodeFlags_Selected;
    if (!hasChildren)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

    // Inactive actors shown greyed out
    if (!isActive)
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));

    // Eye icon toggle for active state
    ImGui::PushID(static_cast<int>(actor->GetID()));
    if (context && EditorWidgets::IconButton(*context, isActive ? "SetInactive" : "SetActive",
                                             isActive ? EditorIcons::Success : EditorIcons::Error,
                                             isActive ? "Set Inactive" : "Set Active")) {
        if (auto* operators = context->GetOperators()) {
            operators->Commands().SetActorActive(*context, actor->GetID(), !isActive);
        }
    }
    ImGui::SameLine();
    if (context) {
        EditorWidgets::SvgIcon(*context, EditorIcons::Actor, 14.0f);
        ImGui::SameLine();
    }

    // Inline rename
    if (m_PendingRenameID == actor->GetID()) {
        ImGui::SetKeyboardFocusHere();
        if (ImGui::InputText("##rename", m_RenameBuffer, sizeof(m_RenameBuffer),
                             ImGuiInputTextFlags_EnterReturnsTrue)) {
            std::string newName(m_RenameBuffer);
            if (!newName.empty() && newName != actor->GetName()) {
                if (auto* operators = context->GetOperators()) {
                    operators->Commands().RenameActor(*context, actor->GetID(), newName);
                }
            }
            m_PendingRenameID = 0;
        }
        if (!ImGui::IsItemActive() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            m_PendingRenameID = 0;
        }
    } else {
        if (hasChildren && m_OpenRequest != 0) {
            ImGui::SetNextItemOpen(m_OpenRequest > 0, ImGuiCond_Always);
        }
        bool open = ImGui::TreeNodeEx(actor->GetName().c_str(), flags);
        if (HasHierarchyFilters() && ActorMatchesOwnFilters(*actor)) {
            DrawActorFilterMatchHighlight(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        }

        // Selection handling
        if (ImGui::IsItemClicked()) {
            auto* operators = context ? context->GetOperators() : nullptr;
            if (operators && ImGui::GetIO().KeyShift && m_LastClickedActorID != 0) {
                std::vector<uint64_t> order;
                if (scene) {
                    for (Actor* root : scene->GetRootActors()) {
                        CollectVisibleActorOrder(root, order);
                    }
                }
                if (!operators->Selection().SelectActorRange(*context, m_LastClickedActorID, actor->GetID(), order)) {
                    operators->Selection().SelectActor(*context, actor->GetID());
                    m_LastClickedActorID = actor->GetID();
                }
            } else if (ImGui::GetIO().KeyCtrl) {
                if (operators) {
                    operators->Selection().SelectActor(*context, actor->GetID(), EditorSelectionIntentMode::Toggle);
                }
                m_LastClickedActorID = actor->GetID();
            } else {
                if (operators)
                    operators->Selection().SelectActor(*context, actor->GetID());
                m_LastClickedActorID = actor->GetID();
            }
        }

        // Drag source
        {
            ActorDragDropSource dragSource(actor->GetID(), actor->GetName());
            if (dragSource.Draw())
                m_DraggedActor = actor->GetHandle();
        }

        // Context menu
        if (EditorContextMenu::DetectItem("##ActorCtx")) {
            m_ActorRightClicked = true;
            if (!isSelected) {
                if (auto* operators = context->GetOperators()) {
                    operators->Selection().SelectActor(*context, actor->GetID());
                }
            }
        }
        {
            EditorContextMenu actorMenu("##ActorCtx");
            if (actorMenu.IsOpen()) {
                ContextMenuContext ctxCtx;
                ctxCtx.target = ContextMenuContext::Target::Actor;
                ctxCtx.pointer = actor;
                ShowContextMenu(ctxCtx, actorMenu);
            }
        }

        // Drop target for actor re-parenting and sibling insertion.
        if (ImGui::BeginDragDropTarget()) {
            const ImGuiPayload* previewPayload = ImGui::GetDragDropPayload();
            const ImVec2 areaMin(ImGui::GetItemRectMin());
            const ImVec2 areaMax(ImGui::GetItemRectMax());
            const ActorDropZone dropZone = GetActorDropZone(areaMin, areaMax);
            if (previewPayload) {
                if (previewPayload->IsDataType(kActorPayload))
                    DrawActorDropCue(areaMin, areaMax, dropZone, previewPayload->IsDelivery());
                else if (previewPayload->IsDataType(kPrefabPayload) && dropZone == ActorDropZone::Into)
                    DrawDropHighlight(areaMin, areaMax, IM_COL32(200, 160, 80, 120),
                                      previewPayload->IsDelivery() ? 3.0f : 2.0f);
            }

            if (scene && context->IsEditing()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kActorPayload)) {
                    if (payload->DataSize >= sizeof(uint64_t)) {
                        uint64_t sourceId = 0;
                        std::memcpy(&sourceId, payload->Data, sizeof(sourceId));
                        Actor* source = scene->FindByID(sourceId);
                        if (source && source != actor) {
                            Actor* afterParent = actor;
                            Actor* afterNextSibling = nullptr;
                            if (dropZone == ActorDropZone::Before) {
                                afterParent = actor->GetParent();
                                afterNextSibling = actor;
                            } else if (dropZone == ActorDropZone::After) {
                                afterParent = actor->GetParent();
                                afterNextSibling = GetNextSibling(*scene, *actor, source);
                            }
                            ExecuteMoveActorDrop(*context, *scene, *source, afterParent, afterNextSibling);
                        }
                    }
                }
                if (dropZone == ActorDropZone::Into) {
                    if (const ImGuiPayload* prefabPayload = ImGui::AcceptDragDropPayload(kPrefabPayload)) {
                        if (auto* operators = context->GetOperators()) {
                            operators->Prefabs().InstantiatePrefab(
                                *context, static_cast<const char*>(prefabPayload->Data), actor->GetID());
                        }
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        if (open && hasChildren) {
            for (Actor* child : actor->GetChildren()) {
                DrawActor(child);
            }
            ImGui::TreePop();
        }
    }

    if (!isActive)
        ImGui::PopStyleColor();
    ImGui::PopID();
#endif
}

void SceneHierarchyPanel::HandleDragDropTarget(Actor* targetParent) {
#if defined(MYENGINE_ENABLE_IMGUI)
    auto* context = GetContext();
    Scene* scene = context ? context->GetScene() : nullptr;
    if (!scene || !context->IsEditing())
        return;
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

        // Actor drop: move to target parent and append after existing children/root actors.
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kActorPayload)) {
            if (payload->DataSize >= sizeof(uint64_t)) {
                uint64_t sourceId = 0;
                std::memcpy(&sourceId, payload->Data, sizeof(sourceId));
                Actor* source = scene->FindByID(sourceId);
                if (source) {
                    ExecuteMoveActorDrop(*context, *scene, *source, targetParent, nullptr);
                }
            }
        }

        // Prefab drop: instantiate as child
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kPrefabPayload)) {
            if (auto* operators = context->GetOperators()) {
                operators->Prefabs().InstantiatePrefab(*context, static_cast<const char*>(payload->Data),
                                                       targetParent ? targetParent->GetID() : 0);
            }
        }
        ImGui::EndDragDropTarget();
    }
#endif
}

void SceneHierarchyPanel::DrawContent() {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (TryDrawScriptedBody("sceneHierarchy"))
        return;

    auto* context = GetContext();
    Scene* scene = context ? context->GetScene() : nullptr;
    if (!scene)
        return;

    ImGui::BeginDisabled(!context->IsEditing());
    DrawToolbar();

    m_DraggedActor = {};
    m_ActorRightClicked = false;
    std::vector<Actor*> rootActors = scene->GetRootActors();
    m_SearchMatches.clear();
    if (HasHierarchyFilters()) {
        m_SearchMatches.reserve(scene->ActorCount());
        for (Actor* actor : rootActors) {
            if (!actor->GetParent())
                RebuildSearchCache(actor);
        }
    }
    for (Actor* actor : rootActors) {
        if (!actor->GetParent())
            DrawActor(actor);
    }
    m_OpenRequest = 0;

    if (!m_ActorRightClicked && EditorContextMenu::DetectWindow("##EmptyCtxMenu")) {}
    {
        EditorContextMenu emptyMenu("##EmptyCtxMenu");
        if (emptyMenu.IsOpen()) {
            ContextMenuContext ctxCtx;
            ShowContextMenu(ctxCtx, emptyMenu);
        }
    }

    {
        const float availY = ImGui::GetContentRegionAvail().y;
        if (availY > 2.0f)
            ImGui::Dummy({ImGui::GetContentRegionAvail().x, availY - 2.0f});
    }
    HandleDragDropTarget(nullptr);

    ImGui::EndDisabled();
#endif
}
