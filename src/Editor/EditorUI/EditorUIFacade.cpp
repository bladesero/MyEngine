#include "Editor/EditorUI/EditorUIFacade.h"

#include "Assets/AssetManager.h"
#include "Core/Logger.h"
#include "Core/Engine.h"
#include "Core/FrameStats.h"
#include "Scene/Scene.h"
#include "Scene/WorldFrameScheduler.h"
#include "Editor/EditorAction.h"
#include "Editor/EditorAssetRegistry.h"
#include "Editor/EditorCommand.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorImGuiBackend.h"
#include "Editor/EditorLogService.h"
#include "Editor/EditorOperators.h"
#include "Editor/EditorProfiler.h"
#include "Editor/EditorUndoUtil.h"
#include "Editor/UI/EditorWidgets.h"
#include "Game/GameViewport.h"
#include "Game/SceneRenderLayer.h"
#include "Game/SceneViewportController.h"
#include "Renderer/RHI/GpuTexture.h"
#include "Scene/Actor.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"

#include <angelscript.h>
#include <scriptstdstring.h>

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#endif

#include <algorithm>
#include <cstdio>
#include <memory>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace Editor::Scripting {
namespace {
thread_local EditorContext* g_ActiveContext = nullptr;
thread_local std::string g_ActivePanelID;

struct ScriptPanelState {
    std::unordered_map<std::string, std::string> strings;
    std::unordered_map<std::string, bool> bools;
    std::unordered_map<std::string, float> floats;
};

std::unordered_map<std::string, ScriptPanelState> g_PanelStates;
std::unordered_map<std::string, EditorSceneTransaction> g_PanelTransactions;

bool Check(int result) {
    return result >= 0;
}

EditorContext* Context() {
    return g_ActiveContext;
}

std::string ScopedPanelKey(const std::string& key) {
    return (g_ActivePanelID.empty() ? std::string{"global"} : g_ActivePanelID) + "." + key;
}

ScriptPanelState& CurrentPanelState() {
    return g_PanelStates[g_ActivePanelID.empty() ? std::string{"global"} : g_ActivePanelID];
}

Scene* EditableScene() {
    EditorContext* context = Context();
    return context ? context->GetScene() : nullptr;
}

Actor* FindActor(uint64_t actorID) {
    Scene* scene = EditableScene();
    return scene ? scene->FindByID(actorID) : nullptr;
}

const char* AssetTypeDisplayName(EditorAssetType type) {
    switch (type) {
    case EditorAssetType::Model:
        return "Model";
    case EditorAssetType::Texture:
        return "Texture";
    case EditorAssetType::Material:
        return "Material";
    case EditorAssetType::Scene:
        return "Scene";
    case EditorAssetType::Prefab:
        return "Prefab";
    case EditorAssetType::Script:
        return "Script";
    case EditorAssetType::Shader:
        return "Shader";
    case EditorAssetType::Audio:
        return "Audio";
    case EditorAssetType::UI:
        return "UI";
    case EditorAssetType::Particle:
        return "Particle";
    case EditorAssetType::Navigation:
        return "Navigation";
    default:
        return "Unknown";
    }
}

EditorAssetType ParseAssetType(const std::string& type) {
    if (type == "Model" || type == "model")
        return EditorAssetType::Model;
    if (type == "Texture" || type == "texture")
        return EditorAssetType::Texture;
    if (type == "Material" || type == "material")
        return EditorAssetType::Material;
    if (type == "Scene" || type == "scene")
        return EditorAssetType::Scene;
    if (type == "Prefab" || type == "prefab")
        return EditorAssetType::Prefab;
    if (type == "Script" || type == "script")
        return EditorAssetType::Script;
    if (type == "Shader" || type == "shader")
        return EditorAssetType::Shader;
    if (type == "Audio" || type == "audio")
        return EditorAssetType::Audio;
    if (type == "UI" || type == "ui")
        return EditorAssetType::UI;
    if (type == "Particle" || type == "particle")
        return EditorAssetType::Particle;
    if (type == "Navigation" || type == "navigation" || type == "navmesh")
        return EditorAssetType::Navigation;
    return EditorAssetType::Unknown;
}

std::string ComponentCategory(const std::string& type) {
    if (type.find("Renderer") != std::string::npos || type == "CameraComponent" || type == "LightComponent" ||
        type == "PostProcessComponent")
        return "Rendering";
    if (type.find("Audio") != std::string::npos)
        return "Audio";
    if (type.find("RigidBody") != std::string::npos || type.find("Collider") != std::string::npos ||
        type.find("CharacterController") != std::string::npos)
        return "Physics";
    if (type.find("Script") != std::string::npos)
        return "Scripting";
    if (type.rfind("UI", 0) == 0)
        return "UI";
    return "Gameplay";
}

std::string DisplayNameFromType(std::string type) {
    const std::string suffix = "Component";
    if (type.size() > suffix.size() && type.compare(type.size() - suffix.size(), suffix.size(), suffix) == 0) {
        type.resize(type.size() - suffix.size());
    }
    std::string result;
    for (size_t i = 0; i < type.size(); ++i) {
        const char ch = type[i];
        if (i > 0 && ch >= 'A' && ch <= 'Z' && (type[i - 1] >= 'a' && type[i - 1] <= 'z')) {
            result.push_back(' ');
        }
        result.push_back(ch);
    }
    return result.empty() ? type : result;
}

std::string JsonValueTypeName(const nlohmann::json& value) {
    if (value.is_boolean())
        return "bool";
    if (value.is_number_integer() || value.is_number_unsigned())
        return "int";
    if (value.is_number_float())
        return "float";
    if (value.is_string())
        return "string";
    if (value.is_array())
        return "array";
    if (value.is_object())
        return "object";
    if (value.is_null())
        return "null";
    return "unknown";
}

void UI_Text(const std::string& text) {
#if defined(MYENGINE_ENABLE_IMGUI)
    ImGui::TextUnformatted(text.c_str());
#else
    (void)text;
#endif
}

bool UI_Button(const std::string& label) {
#if defined(MYENGINE_ENABLE_IMGUI)
    return ImGui::Button(label.c_str());
#else
    (void)label;
    return false;
#endif
}

bool UI_MenuItem(const std::string& label, bool enabled) {
#if defined(MYENGINE_ENABLE_IMGUI)
    return ImGui::MenuItem(label.c_str(), nullptr, false, enabled);
#else
    (void)label;
    (void)enabled;
    return false;
#endif
}

bool UI_BeginChild(const std::string& id, float width, float height, bool border) {
#if defined(MYENGINE_ENABLE_IMGUI)
    return ImGui::BeginChild(id.c_str(), {width, height}, border);
#else
    (void)id;
    (void)width;
    (void)height;
    (void)border;
    return false;
#endif
}

void UI_EndChild() {
#if defined(MYENGINE_ENABLE_IMGUI)
    ImGui::EndChild();
#endif
}

void UI_BeginGroup() {
#if defined(MYENGINE_ENABLE_IMGUI)
    ImGui::BeginGroup();
#endif
}

void UI_EndGroup() {
#if defined(MYENGINE_ENABLE_IMGUI)
    ImGui::EndGroup();
#endif
}

bool UI_IconButton(const std::string& id, const std::string& icon, const std::string& tooltip, bool enabled) {
    EditorContext* context = Context();
    if (!context)
        return false;
    return Editor::UI::EditorWidgets::IconButton(*context, id.c_str(), icon.c_str(), tooltip.c_str(), enabled);
}

bool UI_ToolbarActionButton(const std::string& actionID) {
    EditorContext* context = Context();
    if (!context)
        return false;
    return Editor::UI::EditorWidgets::ToolbarActionButton(*context, actionID.c_str());
}

bool UI_ToolbarActionButtonEx(const std::string& actionID, const std::string& icon, int variant, bool sameLineAfter) {
    EditorContext* context = Context();
    if (!context)
        return false;
    if (variant < static_cast<int>(Editor::UI::EditorWidgetVariant::Neutral) ||
        variant > static_cast<int>(Editor::UI::EditorWidgetVariant::Warning)) {
        variant = static_cast<int>(Editor::UI::EditorWidgetVariant::Neutral);
    }
    return Editor::UI::EditorWidgets::ToolbarActionButton(
        *context, actionID.c_str(), icon.c_str(), static_cast<Editor::UI::EditorWidgetVariant>(variant), sameLineAfter);
}

void UI_Separator() {
#if defined(MYENGINE_ENABLE_IMGUI)
    ImGui::Separator();
#endif
}

void UI_SameLine() {
#if defined(MYENGINE_ENABLE_IMGUI)
    ImGui::SameLine();
#endif
}

bool UI_BeginTable(const std::string& id, int columns) {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (columns <= 0)
        return false;
    return ImGui::BeginTable(id.c_str(), columns,
                             ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                                 ImGuiTableFlags_ScrollY);
#else
    (void)id;
    (void)columns;
    return false;
#endif
}

void UI_EndTable() {
#if defined(MYENGINE_ENABLE_IMGUI)
    ImGui::EndTable();
#endif
}

void UI_TableNextRow() {
#if defined(MYENGINE_ENABLE_IMGUI)
    ImGui::TableNextRow();
#endif
}

void UI_TableSetColumnIndex(int column) {
#if defined(MYENGINE_ENABLE_IMGUI)
    ImGui::TableSetColumnIndex(column);
#else
    (void)column;
#endif
}

void UI_TableHeader(const std::string& labels) {
#if defined(MYENGINE_ENABLE_IMGUI)
    std::istringstream input(labels);
    std::string label;
    while (std::getline(input, label, '\n')) {
        ImGui::TableSetupColumn(label.c_str());
    }
    ImGui::TableHeadersRow();
#else
    (void)labels;
#endif
}

bool UI_TreeNode(const std::string& label, bool selected, bool leaf) {
#if defined(MYENGINE_ENABLE_IMGUI)
    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (selected)
        flags |= ImGuiTreeNodeFlags_Selected;
    if (leaf)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    return ImGui::TreeNodeEx(label.c_str(), flags);
#else
    (void)label;
    (void)selected;
    (void)leaf;
    return false;
#endif
}

void UI_TreePop() {
#if defined(MYENGINE_ENABLE_IMGUI)
    ImGui::TreePop();
#endif
}

bool UI_Selectable(const std::string& label, bool selected) {
#if defined(MYENGINE_ENABLE_IMGUI)
    return ImGui::Selectable(label.c_str(), selected);
#else
    (void)label;
    (void)selected;
    return false;
#endif
}

void UI_Tooltip(const std::string& text) {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("%s", text.c_str());
    }
#else
    (void)text;
#endif
}

bool UI_SectionHeader(const std::string& label, bool defaultOpen) {
    return Editor::UI::EditorWidgets::SectionHeader(label.c_str(), defaultOpen);
}

void UI_BeginPropertyGrid(const std::string& id) {
    Editor::UI::EditorWidgets::BeginPropertyGrid(id.c_str());
}

void UI_EndPropertyGrid() {
    Editor::UI::EditorWidgets::EndPropertyGrid();
}

bool UI_BeginPropertyRow(const std::string& label) {
    return Editor::UI::EditorWidgets::BeginPropertyRow(label.c_str());
}

void UI_EndPropertyRow() {
    Editor::UI::EditorWidgets::EndPropertyRow();
}

bool UI_InputText(const std::string& label, std::string& value) {
#if defined(MYENGINE_ENABLE_IMGUI)
    char buffer[512] = {};
    std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
    const bool changed = ImGui::InputText(label.c_str(), buffer, sizeof(buffer));
    if (changed)
        value = buffer;
    return changed;
#else
    (void)label;
    (void)value;
    return false;
#endif
}

bool UI_Checkbox(const std::string& label, bool& value) {
#if defined(MYENGINE_ENABLE_IMGUI)
    return ImGui::Checkbox(label.c_str(), &value);
#else
    (void)label;
    (void)value;
    return false;
#endif
}

bool UI_DragFloat(const std::string& label, float& value, float speed) {
#if defined(MYENGINE_ENABLE_IMGUI)
    return ImGui::DragFloat(label.c_str(), &value, speed);
#else
    (void)label;
    (void)value;
    (void)speed;
    return false;
#endif
}

bool UI_DragVec3(const std::string& label, float& x, float& y, float& z, float speed) {
#if defined(MYENGINE_ENABLE_IMGUI)
    float values[3] = {x, y, z};
    const bool changed = ImGui::DragFloat3(label.c_str(), values, speed);
    if (changed) {
        x = values[0];
        y = values[1];
        z = values[2];
    }
    return changed;
#else
    (void)label;
    (void)x;
    (void)y;
    (void)z;
    (void)speed;
    return false;
#endif
}

bool UI_Combo(const std::string& label, std::string& value, const std::string& options) {
#if defined(MYENGINE_ENABLE_IMGUI)
    std::vector<std::string> items;
    std::istringstream input(options);
    std::string item;
    while (std::getline(input, item, '\n')) {
        if (!item.empty())
            items.push_back(item);
    }
    int current = 0;
    for (size_t index = 0; index < items.size(); ++index) {
        if (items[index] == value) {
            current = static_cast<int>(index);
            break;
        }
    }
    if (items.empty())
        return false;
    const char* preview = items[static_cast<size_t>(current)].c_str();
    bool changed = false;
    if (ImGui::BeginCombo(label.c_str(), preview)) {
        for (size_t index = 0; index < items.size(); ++index) {
            const bool selected = static_cast<int>(index) == current;
            if (ImGui::Selectable(items[index].c_str(), selected)) {
                value = items[index];
                changed = true;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
#else
    (void)label;
    (void)value;
    (void)options;
    return false;
#endif
}

void UI_BeginDisabled(bool disabled) {
#if defined(MYENGINE_ENABLE_IMGUI)
    ImGui::BeginDisabled(disabled);
#else
    (void)disabled;
#endif
}

void UI_EndDisabled() {
#if defined(MYENGINE_ENABLE_IMGUI)
    ImGui::EndDisabled();
#endif
}

void UI_PushStyleColor(int index, float r, float g, float b, float a) {
#if defined(MYENGINE_ENABLE_IMGUI)
    ImGui::PushStyleColor(index, {r, g, b, a});
#else
    (void)index;
    (void)r;
    (void)g;
    (void)b;
    (void)a;
#endif
}

void UI_PopStyleColor(int count) {
#if defined(MYENGINE_ENABLE_IMGUI)
    ImGui::PopStyleColor(count <= 0 ? 1 : count);
#else
    (void)count;
#endif
}

bool UI_ImageViewport(const std::string& which) {
    EditorContext* context = Context();
    return context && DrawViewportImage(*context, which);
}

void UI_InlineMessage(int type, const std::string& text) {
    Editor::UI::EditorWidgets::InlineMessage(static_cast<Editor::UI::EditorNotificationType>(type), text.c_str());
}

bool Editor_IsEditing() {
    EditorContext* context = Context();
    return context && context->IsEditing();
}

bool Editor_IsPlaying() {
    EditorContext* context = Context();
    auto* layer = context ? context->GetSceneLayer() : nullptr;
    return layer && layer->IsPlaying();
}

bool Editor_ExecuteAction(const std::string& actionID) {
    EditorContext* context = Context();
    EditorOperators* operators = context ? context->GetOperators() : nullptr;
    return context && operators && operators->Commands().ExecuteAction(*context, actionID);
}

uint64_t Selection_GetActorId() {
    EditorContext* context = Context();
    EditorOperators* operators = context ? context->GetOperators() : nullptr;
    return context && operators ? operators->Selection().GetSelectionSnapshot(*context).actorID : 0;
}

void Selection_SelectActor(uint64_t actorID) {
    EditorContext* context = Context();
    EditorOperators* operators = context ? context->GetOperators() : nullptr;
    if (context && operators)
        operators->Selection().SelectActor(*context, actorID);
}

void Selection_SelectActorMode(uint64_t actorID, int mode) {
    EditorContext* context = Context();
    EditorOperators* operators = context ? context->GetOperators() : nullptr;
    if (!context || !operators)
        return;
    EditorSelectionIntentMode selectionMode = EditorSelectionIntentMode::Replace;
    if (mode == 1)
        selectionMode = EditorSelectionIntentMode::Add;
    else if (mode == 2)
        selectionMode = EditorSelectionIntentMode::Toggle;
    operators->Selection().SelectActor(*context, actorID, selectionMode);
}

void Selection_SelectAsset(const std::string& path) {
    EditorContext* context = Context();
    EditorOperators* operators = context ? context->GetOperators() : nullptr;
    if (context && operators)
        operators->Selection().SelectAsset(*context, path);
}

void Selection_Clear() {
    EditorContext* context = Context();
    EditorOperators* operators = context ? context->GetOperators() : nullptr;
    if (context && operators)
        operators->Selection().Clear(*context);
}

std::string Selection_GetAssetPath() {
    EditorContext* context = Context();
    EditorOperators* operators = context ? context->GetOperators() : nullptr;
    return context && operators ? operators->Selection().GetSelectionSnapshot(*context).assetPath : std::string{};
}

std::string Scene_GetRootActors() {
    Scene* scene = EditableScene();
    if (!scene)
        return {};
    std::ostringstream out;
    bool first = true;
    for (Actor* actor : scene->GetRootActors()) {
        if (!actor || actor->GetParent())
            continue;
        if (!first)
            out << "\n";
        first = false;
        out << actor->GetID();
    }
    return out.str();
}

std::string Scene_GetChildren(uint64_t actorID) {
    Actor* actor = FindActor(actorID);
    if (!actor)
        return {};
    std::ostringstream out;
    bool first = true;
    for (Actor* child : actor->GetChildren()) {
        if (!child)
            continue;
        if (!first)
            out << "\n";
        first = false;
        out << child->GetID();
    }
    return out.str();
}

std::string Scene_GetActorName(uint64_t actorID) {
    Actor* actor = FindActor(actorID);
    return actor ? actor->GetName() : std::string{};
}

uint64_t Scene_CreateActor(const std::string& name) {
    EditorContext* context = Context();
    EditorOperators* operators = context ? context->GetOperators() : nullptr;
    return context && operators ? operators->Commands().CreateActor(*context, name) : 0;
}

bool Scene_DeleteActor(uint64_t actorID) {
    EditorContext* context = Context();
    EditorOperators* operators = context ? context->GetOperators() : nullptr;
    return context && operators && operators->Commands().DeleteActor(*context, actorID);
}

bool Scene_SetActorName(uint64_t actorID, const std::string& name) {
    EditorContext* context = Context();
    EditorOperators* operators = context ? context->GetOperators() : nullptr;
    return context && operators && operators->Commands().RenameActor(*context, actorID, name);
}

std::string Scene_FindActorsWithComponent(const std::string& type) {
    EditorContext* context = Context();
    Scene* scene = context ? context->GetInspectorScene() : nullptr;
    if (!scene || type.empty())
        return {};
    std::ostringstream out;
    bool first = true;
    const auto appendActor = [&](auto&& self, Actor* actor) -> void {
        if (!actor)
            return;
        if (actor->HasComponentType(type)) {
            if (!first)
                out << "\n";
            first = false;
            out << actor->GetID();
        }
        for (Actor* child : actor->GetChildren())
            self(self, child);
    };
    for (Actor* root : scene->GetRootActors()) {
        if (root && !root->GetParent())
            appendActor(appendActor, root);
    }
    return out.str();
}

std::string Project_GetContentRoot() {
    EditorContext* context = Context();
    return context ? context->GetContentRoot().generic_string() : std::string{};
}

void Validation_ReportInfo(const std::string& message) {
    Logger::Info("[EditorValidation] ", message);
}

void Validation_ReportWarning(const std::string& message) {
    Logger::Warn("[EditorValidation] ", message);
}

void Validation_ReportError(const std::string& message) {
    Logger::Error("[EditorValidation] ", message);
}

bool Components_Has(uint64_t actorID, const std::string& type) {
    Actor* actor = FindActor(actorID);
    return actor && actor->HasComponentType(type);
}

std::string Components_GetJson(uint64_t actorID, const std::string& type) {
    Actor* actor = FindActor(actorID);
    Component* component = actor ? actor->GetComponentByTypeName(type) : nullptr;
    if (!component)
        return "{}";
    nlohmann::json data = nlohmann::json::object();
    component->Serialize(data);
    return data.dump();
}

bool Components_SetJson(uint64_t actorID, const std::string& type, const std::string& jsonText) {
    EditorContext* context = Context();
    Scene* scene = context ? context->GetScene() : nullptr;
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    Component* component = actor ? actor->GetComponentByTypeName(type) : nullptr;
    EditorOperators* operators = context ? context->GetOperators() : nullptr;
    if (!context || !scene || !actor || !component || !operators || !context->CanEditScene()) {
        return false;
    }
    try {
        const std::string before = SceneSerializer::SaveToString(*scene);
        component->Deserialize(nlohmann::json::parse(jsonText));
        const std::string after = SceneSerializer::SaveToString(*scene);
        return operators->Transactions().CommitSceneSnapshot(*context, "Script Component Edit", before, after, actorID,
                                                             actorID);
    } catch (...) {
        return false;
    }
}

bool Components_SetFieldJson(uint64_t actorID, const std::string& type, const std::string& fieldName,
                             const std::string& valueJsonText) {
    EditorContext* context = Context();
    Scene* scene = context ? context->GetScene() : nullptr;
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    Component* component = actor ? actor->GetComponentByTypeName(type) : nullptr;
    EditorOperators* operators = context ? context->GetOperators() : nullptr;
    if (!context || !scene || !actor || !component || !operators || fieldName.empty() || !context->CanEditScene()) {
        return false;
    }
    try {
        nlohmann::json before = nlohmann::json::object();
        component->Serialize(before);
        nlohmann::json after = before;
        after[fieldName] = nlohmann::json::parse(valueJsonText);
        if (before == after)
            return false;
        return operators->Transactions().CommitComponentProperty(*context, *actor, type, fieldName, before, after);
    } catch (...) {
        return false;
    }
}

std::string Components_GetMetadata(const std::string& type) {
    nlohmann::json metadata = {{"type", type},
                               {"displayName", DisplayNameFromType(type)},
                               {"category", ComponentCategory(type)},
                               {"registered", ComponentRegistry::Get().IsRegistered(type)},
                               {"editable", true},
                               {"fields", nlohmann::json::array()}};
    std::unique_ptr<Component> component = ComponentRegistry::Get().CreateDetached(type);
    if (!component)
        return metadata.dump();
    nlohmann::json data = nlohmann::json::object();
    component->Serialize(data);
    if (data.is_object()) {
        for (auto it = data.begin(); it != data.end(); ++it) {
            metadata["fields"].push_back({{"name", it.key()},
                                          {"type", JsonValueTypeName(it.value())},
                                          {"defaultValue", it.value()},
                                          {"editable", true}});
        }
    }
    return metadata.dump();
}

std::string Assets_List(const std::string& folder, bool recursive) {
    EditorContext* context = Context();
    EditorAssetRegistry* registry = context ? context->GetAssetRegistry() : nullptr;
    if (!registry)
        return {};
    std::ostringstream out;
    bool first = true;
    for (const EditorAssetInfo& asset : registry->GetAssetsInFolder(folder, recursive)) {
        if (!first)
            out << "\n";
        first = false;
        out << asset.relativePath;
    }
    return out.str();
}

std::string Assets_ListByType(const std::string& type) {
    EditorContext* context = Context();
    EditorAssetRegistry* registry = context ? context->GetAssetRegistry() : nullptr;
    if (!registry)
        return {};
    const EditorAssetType filter = ParseAssetType(type);
    std::ostringstream out;
    bool first = true;
    for (const EditorAssetInfo& asset : registry->GetAssets(filter)) {
        if (!first)
            out << "\n";
        first = false;
        out << asset.relativePath;
    }
    return out.str();
}

void Assets_Refresh() {
    EditorContext* context = Context();
    EditorOperators* operators = context ? context->GetOperators() : nullptr;
    if (context && operators)
        operators->Assets().Refresh(*context);
}

bool Commands_ExecuteAction(const std::string& actionID) {
    return Editor_ExecuteAction(actionID);
}

uint64_t Commands_CreateActor(const std::string& name) {
    return Scene_CreateActor(name);
}

bool Commands_DeleteActor(uint64_t actorID) {
    return Scene_DeleteActor(actorID);
}

bool Commands_RenameActor(uint64_t actorID, const std::string& name) {
    return Scene_SetActorName(actorID, name);
}

bool Commands_SetActorActive(uint64_t actorID, bool active) {
    EditorContext* context = Context();
    EditorOperators* operators = context ? context->GetOperators() : nullptr;
    return context && operators && operators->Commands().SetActorActive(*context, actorID, active);
}

bool Commands_SetActorTag(uint64_t actorID, const std::string& tag) {
    EditorContext* context = Context();
    EditorOperators* operators = context ? context->GetOperators() : nullptr;
    return context && operators && operators->Commands().SetActorTag(*context, actorID, tag);
}

bool Commands_SetActorLayer(uint64_t actorID, uint32_t layer) {
    EditorContext* context = Context();
    EditorOperators* operators = context ? context->GetOperators() : nullptr;
    return context && operators && operators->Commands().SetActorLayer(*context, actorID, layer);
}

bool Commands_MoveActor(uint64_t actorID, uint64_t parentID, uint64_t nextSiblingID) {
    EditorContext* context = Context();
    EditorOperators* operators = context ? context->GetOperators() : nullptr;
    return context && operators && operators->Commands().MoveActor(*context, actorID, parentID, nextSiblingID);
}

bool Assets_Delete(const std::string& path) {
    EditorContext* context = Context();
    EditorOperators* operators = context ? context->GetOperators() : nullptr;
    return context && operators && operators->Assets().DeleteAsset(*context, path);
}

bool Assets_Rename(const std::string& path, const std::string& newNameOrPath) {
    EditorContext* context = Context();
    EditorOperators* operators = context ? context->GetOperators() : nullptr;
    return context && operators && operators->Assets().RenameAsset(*context, path, newNameOrPath);
}

bool Assets_Duplicate(const std::string& path) {
    EditorContext* context = Context();
    EditorOperators* operators = context ? context->GetOperators() : nullptr;
    return context && operators && operators->Assets().DuplicateAsset(*context, path);
}

bool Assets_Reimport(const std::string& uuid) {
    EditorContext* context = Context();
    EditorOperators* operators = context ? context->GetOperators() : nullptr;
    return context && operators && operators->Assets().Reimport(*context, uuid);
}

bool DragDrop_BeginSource(const std::string& type, const std::string& payload, const std::string& label) {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (type.empty() || payload.empty())
        return false;
    if (!ImGui::BeginDragDropSource())
        return false;
    ImGui::SetDragDropPayload(type.c_str(), payload.data(), static_cast<int>(payload.size() + 1));
    ImGui::TextUnformatted(label.empty() ? payload.c_str() : label.c_str());
    ImGui::EndDragDropSource();
    return true;
#else
    (void)type;
    (void)payload;
    (void)label;
    return false;
#endif
}

std::string DragDrop_Accept(const std::string& type) {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (type.empty() || !ImGui::BeginDragDropTarget())
        return {};
    std::string result;
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(type.c_str())) {
        if (payload->Data && payload->DataSize > 0) {
            result.assign(static_cast<const char*>(payload->Data), static_cast<size_t>(payload->DataSize));
            if (!result.empty() && result.back() == '\0')
                result.pop_back();
        }
    }
    ImGui::EndDragDropTarget();
    return result;
#else
    (void)type;
    return {};
#endif
}

bool DragDrop_ApplyActorDrop(uint64_t actorID, uint64_t parentID, uint64_t nextSiblingID) {
    EditorContext* context = Context();
    EditorOperators* operators = context ? context->GetOperators() : nullptr;
    return context && operators && operators->DragDrop().ApplyActorDrop(*context, actorID, parentID, nextSiblingID);
}

bool DragDrop_ApplyAssetDrop(const std::string& assetPath, const std::string& targetPath) {
    EditorContext* context = Context();
    EditorOperators* operators = context ? context->GetOperators() : nullptr;
    return context && operators && operators->DragDrop().ApplyAssetDrop(*context, assetPath, targetPath);
}

bool Transaction_BeginSnapshot(const std::string& label) {
    EditorContext* context = Context();
    if (!context || !context->GetScene())
        return false;
    EditorOperators* operators = context->GetOperators();
    if (!operators)
        return false;
    const std::string beforeJson = SceneSerializer::SaveToString(*context->GetScene());
    const uint64_t selection = context->GetSelection().GetActorID();
    operators->Transactions().BeginSnapshot(g_PanelTransactions[g_ActivePanelID], label.c_str(), beforeJson, selection);
    return true;
}

bool Transaction_CommitIfChanged() {
    EditorContext* context = Context();
    EditorOperators* operators = context ? context->GetOperators() : nullptr;
    if (!context || !operators)
        return false;
    return operators->Transactions().CommitIfChanged(*context, g_PanelTransactions[g_ActivePanelID]);
}

void Transaction_Cancel() {
    EditorContext* context = Context();
    EditorOperators* operators = context ? context->GetOperators() : nullptr;
    if (operators)
        operators->Transactions().Cancel(g_PanelTransactions[g_ActivePanelID]);
}

bool Viewport_SetSceneViewportRect(float x, float y, float width, float height, bool hovered) {
    EditorContext* context = Context();
    EditorOperators* operators = context ? context->GetOperators() : nullptr;
    return context && operators && operators->Viewport().SetSceneViewportRect(*context, {x, y, width, height}, hovered);
}

bool Viewport_SetGameViewportRect(float x, float y, float width, float height) {
    EditorContext* context = Context();
    EditorOperators* operators = context ? context->GetOperators() : nullptr;
    return context && operators && operators->Viewport().SetGameViewportRect(*context, {x, y, width, height});
}

std::string PanelState_GetString(const std::string& key, const std::string& defaultValue) {
    const auto& strings = CurrentPanelState().strings;
    const auto found = strings.find(key);
    return found == strings.end() ? defaultValue : found->second;
}

void PanelState_SetString(const std::string& key, const std::string& value) {
    CurrentPanelState().strings[key] = value;
}

bool PanelState_GetBool(const std::string& key, bool defaultValue) {
    const auto& bools = CurrentPanelState().bools;
    const auto found = bools.find(key);
    return found == bools.end() ? defaultValue : found->second;
}

void PanelState_SetBool(const std::string& key, bool value) {
    CurrentPanelState().bools[key] = value;
}

float PanelState_GetFloat(const std::string& key, float defaultValue) {
    const auto& floats = CurrentPanelState().floats;
    const auto found = floats.find(key);
    return found == floats.end() ? defaultValue : found->second;
}

void PanelState_SetFloat(const std::string& key, float value) {
    CurrentPanelState().floats[key] = value;
}

bool ActorSubtreeMatches(const Actor& actor, const std::string& filter) {
    if (filter.empty() || actor.GetName().find(filter) != std::string::npos)
        return true;
    for (const Actor* child : actor.GetChildren()) {
        if (child && ActorSubtreeMatches(*child, filter))
            return true;
    }
    return false;
}

std::string Hierarchy_GetRows(const std::string& filter) {
    EditorContext* context = Context();
    Scene* scene = context ? context->GetInspectorScene() : nullptr;
    if (!context || !scene)
        return "[]";
    nlohmann::json rows = nlohmann::json::array();
    const auto appendActor = [&](auto&& self, Actor* actor, int depth) -> void {
        if (!actor)
            return;
        const bool subtreeMatch = ActorSubtreeMatches(*actor, filter);
        if (!subtreeMatch)
            return;
        rows.push_back({{"actorId", actor->GetID()},
                        {"name", actor->GetName()},
                        {"depth", depth},
                        {"hasChildren", !actor->GetChildren().empty()},
                        {"selected", context->GetSelection().IsSelected(actor->GetID())},
                        {"active", actor->IsActiveSelf()},
                        {"subtreeMatch", subtreeMatch}});
        for (Actor* child : actor->GetChildren())
            self(self, child, depth + 1);
    };
    for (Actor* root : scene->GetRootActors()) {
        if (root && !root->GetParent())
            appendActor(appendActor, root, 0);
    }
    return rows.dump();
}

std::string Inspector_GetSelection() {
    EditorContext* context = Context();
    EditorOperators* operators = context ? context->GetOperators() : nullptr;
    if (!context || !operators)
        return "{}";
    const EditorSelectionSnapshot snapshot = operators->Selection().GetSelectionSnapshot(*context);
    nlohmann::json selection = {{"actorId", snapshot.actorID},
                                {"assetPath", snapshot.assetPath},
                                {"hasActor", snapshot.hasActor},
                                {"hasAsset", snapshot.hasAsset}};
    return selection.dump();
}

std::string Inspector_GetComponentTypes(uint64_t actorID) {
    Actor* actor = FindActor(actorID);
    if (!actor)
        return "[]";
    nlohmann::json types = nlohmann::json::array();
    actor->ForEachComponent([&](Component& component) { types.push_back(component.GetTypeName()); });
    return types.dump();
}

std::string AssetTypeName(EditorAssetType type) {
    switch (type) {
    case EditorAssetType::Model:
        return "model";
    case EditorAssetType::Texture:
        return "texture";
    case EditorAssetType::Material:
        return "material";
    case EditorAssetType::Scene:
        return "scene";
    case EditorAssetType::Prefab:
        return "prefab";
    case EditorAssetType::Script:
        return "script";
    case EditorAssetType::Shader:
        return "shader";
    case EditorAssetType::Audio:
        return "audio";
    case EditorAssetType::UI:
        return "ui";
    case EditorAssetType::Particle:
        return "particle";
    case EditorAssetType::Navigation:
        return "navigation";
    default:
        return "unknown";
    }
}

std::string AssetBrowser_GetFolders() {
    EditorContext* context = Context();
    EditorAssetRegistry* registry = context ? context->GetAssetRegistry() : nullptr;
    if (!registry)
        return "[]";
    nlohmann::json folders = nlohmann::json::array();
    for (const EditorAssetFolderInfo& folder : registry->GetFolders()) {
        folders.push_back({{"relativePath", folder.relativePath},
                           {"displayName", folder.displayName},
                           {"directAssetCount", folder.directAssetCount},
                           {"assetCount", folder.assetCount}});
    }
    return folders.dump();
}

std::string AssetBrowser_GetAssets(const std::string& folder, bool recursive, const std::string& filter) {
    EditorContext* context = Context();
    EditorAssetRegistry* registry = context ? context->GetAssetRegistry() : nullptr;
    if (!registry)
        return "[]";
    nlohmann::json assets = nlohmann::json::array();
    for (const EditorAssetInfo& asset : registry->GetAssetsInFolder(folder, recursive)) {
        if (!filter.empty() && asset.relativePath.find(filter) == std::string::npos)
            continue;
        nlohmann::json diagnostics = nlohmann::json::array();
        for (const AssetDiagnostic& diagnostic : asset.diagnostics) {
            diagnostics.push_back({{"severity", diagnostic.severity}, {"message", diagnostic.message}});
        }
        assets.push_back({{"relativePath", asset.relativePath},
                          {"absolutePath", asset.absolutePath.string()},
                          {"type", AssetTypeName(asset.type)},
                          {"uuid", asset.uuid},
                          {"artifactPath", asset.artifactPath.string()},
                          {"imported", asset.imported},
                          {"importState", static_cast<int>(asset.importState)},
                          {"diagnostics", diagnostics}});
    }
    return assets.dump();
}

std::string Log_GetRows() {
    EditorContext* context = Context();
    EditorLogService* logs = context ? context->GetService<EditorLogService>() : nullptr;
    if (!logs)
        return "[]";
    nlohmann::json rows = nlohmann::json::array();
    for (const std::string& line : logs->Snapshot())
        rows.push_back(line);
    return rows.dump();
}

void Log_Clear() {
    EditorContext* context = Context();
    if (EditorLogService* logs = context ? context->GetService<EditorLogService>() : nullptr) {
        logs->Clear();
    }
}

bool Log_GetAutoScroll() {
    EditorContext* context = Context();
    EditorLogService* logs = context ? context->GetService<EditorLogService>() : nullptr;
    return logs && logs->IsAutoScroll();
}

void Log_SetAutoScroll(bool value) {
    EditorContext* context = Context();
    if (EditorLogService* logs = context ? context->GetService<EditorLogService>() : nullptr) {
        logs->SetAutoScroll(value);
    }
}

std::string Profiler_GetRows() {
    EditorContext* context = Context();
    EditorProfiler* profiler = context ? context->GetProfiler() : nullptr;
    if (!profiler)
        return "[]";
    nlohmann::json rows = nlohmann::json::array();
    auto events = profiler->Snapshot();
    std::reverse(events.begin(), events.end());
    for (const EditorProfilerEvent& event : events) {
        rows.push_back({{"category", event.category},
                        {"name", event.name},
                        {"durationMs", event.durationMs},
                        {"details", event.details},
                        {"frameIndex", event.frameIndex},
                        {"timestampSeconds", event.timestampSeconds}});
    }
    return rows.dump();
}

std::string Profiler_GetFrameStats() {
    EditorContext* context = Context();
    const FrameStats emptyStats{};
    const FrameStats& stats = context && context->GetEngine() ? context->GetEngine()->GetFrameStats() : emptyStats;
    const RendererFrameStats& renderer = stats.renderer;
    nlohmann::json data = {{"frameMs", stats.smoothedFrameMs},
                           {"fps", stats.fps},
                           {"updateMs", stats.updateMs},
                           {"renderMs", stats.renderMs},
                           {"drawCalls", renderer.drawCalls},
                           {"shadowDrawCalls", renderer.shadowDrawCalls},
                           {"mainDrawCalls", renderer.mainDrawCalls},
                           {"fullscreenDrawCalls", renderer.fullscreenDrawCalls},
                           {"bindGroupCreates", renderer.bindGroupCreates},
                           {"textureUploads", renderer.textureUploads},
                           {"textureUploadBytes", renderer.textureUploadBytes},
                           {"textureUploadMs", renderer.textureUploadMs}};
    if (Scene* scene = context ? context->GetSimulationScene() : nullptr) {
        const WorldSchedulerStats& world = scene->GetFrameScheduler().GetStats();
        data["worldFixedTicks"] = world.fixedTicks;
        data["worldDroppedFixedTicks"] = world.droppedFixedTicks;
        nlohmann::json phases = nlohmann::json::object();
        for (size_t i = 0; i < kWorldPhaseCount; ++i)
            phases[WorldPhaseName(static_cast<WorldPhase>(i))] = world.phaseMilliseconds[i];
        data["worldPhasesMs"] = std::move(phases);
    }
    return data.dump();
}

void Profiler_Clear() {
    EditorContext* context = Context();
    if (EditorProfiler* profiler = context ? context->GetProfiler() : nullptr) {
        profiler->Clear();
    }
}

bool Profiler_GetEnabled() {
    EditorContext* context = Context();
    EditorProfiler* profiler = context ? context->GetProfiler() : nullptr;
    return profiler && profiler->IsEnabled();
}

void Profiler_SetEnabled(bool enabled) {
    EditorContext* context = Context();
    if (EditorProfiler* profiler = context ? context->GetProfiler() : nullptr) {
        profiler->SetEnabled(enabled);
    }
}

} // namespace

void SetActiveEditorContext(EditorContext* context) {
    g_ActiveContext = context;
}

EditorContext* GetActiveEditorContext() {
    return g_ActiveContext;
}

void SetActiveEditorPanelID(const std::string& panelID) {
    g_ActivePanelID = panelID;
}

void ClearActiveEditorPanelID() {
    g_ActivePanelID.clear();
}

bool DrawViewportImage(EditorContext& context, const std::string& which) {
#if defined(MYENGINE_ENABLE_IMGUI)
    const ImVec2 imageMin = ImGui::GetCursorScreenPos();
    const ImVec2 imageSize = ImGui::GetContentRegionAvail();
    if (imageSize.x <= 1.0f || imageSize.y <= 1.0f) {
        ImGui::Dummy({std::max(1.0f, imageSize.x), std::max(1.0f, imageSize.y)});
        return false;
    }

    GpuTextureView* view = nullptr;
    if (which == "game") {
        if (GameViewport* viewport = context.GetGameViewport()) {
            const ImVec2 local = [](const ImVec2& screenPos) {
                if (const ImGuiViewport* viewport = ImGui::GetWindowViewport()) {
                    return ImVec2{screenPos.x - viewport->Pos.x, screenPos.y - viewport->Pos.y};
                }
                return screenPos;
            }(imageMin);
            if (EditorOperators* operators = context.GetOperators()) {
                operators->Viewport().SetGameViewportRect(context, {local.x, local.y, imageSize.x, imageSize.y});
            }
            view = viewport->GetOutputView();
        }
    } else {
        if (SceneViewport* viewport = context.GetSceneViewport()) {
            if (EditorOperators* operators = context.GetOperators()) {
                operators->Viewport().SetSceneViewportRect(
                    context, {imageMin.x, imageMin.y, imageSize.x, imageSize.y},
                    ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows));
            }
            view = viewport->GetOutputView();
        }
    }

    void* texture = nullptr;
    if (view && context.GetImGuiBackend()) {
        texture = context.GetImGuiBackend()->GetTextureId(view);
    }
    if (texture) {
        ImGui::Image(reinterpret_cast<ImTextureID>(texture), imageSize);
    } else {
        ImGui::Dummy(imageSize);
    }
    return texture != nullptr;
#else
    (void)context;
    (void)which;
    return false;
#endif
}

void RegisterEditorUIFacade(void* scriptEngine) {
    auto* engine = static_cast<asIScriptEngine*>(scriptEngine);
    if (!engine)
        return;
    engine->SetDefaultNamespace("UI");
    Check(engine->RegisterGlobalFunction("void Text(const string &in)", asFUNCTION(UI_Text), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("bool Button(const string &in)", asFUNCTION(UI_Button), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("bool MenuItem(const string &in, bool enabled = true)",
                                         asFUNCTION(UI_MenuItem), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction(
        "bool BeginChild(const string &in, float width = 0, float height = 0, bool border = false)",
        asFUNCTION(UI_BeginChild), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("void EndChild()", asFUNCTION(UI_EndChild), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("void BeginGroup()", asFUNCTION(UI_BeginGroup), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("void EndGroup()", asFUNCTION(UI_EndGroup), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction(
        "bool IconButton(const string &in, const string &in, const string &in, bool enabled = true)",
        asFUNCTION(UI_IconButton), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("bool ToolbarActionButton(const string &in)",
                                         asFUNCTION(UI_ToolbarActionButton), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction(
        "bool ToolbarActionButtonEx(const string &in, const string &in, int variant = 0, bool sameLineAfter = true)",
        asFUNCTION(UI_ToolbarActionButtonEx), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("void Separator()", asFUNCTION(UI_Separator), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("void SameLine()", asFUNCTION(UI_SameLine), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("bool BeginTable(const string &in, int)", asFUNCTION(UI_BeginTable),
                                         asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("void EndTable()", asFUNCTION(UI_EndTable), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("void TableNextRow()", asFUNCTION(UI_TableNextRow), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("void TableSetColumnIndex(int)", asFUNCTION(UI_TableSetColumnIndex),
                                         asCALL_CDECL));
    Check(
        engine->RegisterGlobalFunction("void TableHeader(const string &in)", asFUNCTION(UI_TableHeader), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("bool TreeNode(const string &in, bool selected = false, bool leaf = false)",
                                         asFUNCTION(UI_TreeNode), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("void TreePop()", asFUNCTION(UI_TreePop), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("bool Selectable(const string &in, bool selected = false)",
                                         asFUNCTION(UI_Selectable), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("void Tooltip(const string &in)", asFUNCTION(UI_Tooltip), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("bool SectionHeader(const string &in, bool defaultOpen = true)",
                                         asFUNCTION(UI_SectionHeader), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("void BeginPropertyGrid(const string &in)", asFUNCTION(UI_BeginPropertyGrid),
                                         asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("void EndPropertyGrid()", asFUNCTION(UI_EndPropertyGrid), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("bool BeginPropertyRow(const string &in)", asFUNCTION(UI_BeginPropertyRow),
                                         asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("void EndPropertyRow()", asFUNCTION(UI_EndPropertyRow), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("bool InputText(const string &in, string &inout)", asFUNCTION(UI_InputText),
                                         asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("bool Checkbox(const string &in, bool &inout)", asFUNCTION(UI_Checkbox),
                                         asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("bool DragFloat(const string &in, float &inout, float speed = 0.1f)",
                                         asFUNCTION(UI_DragFloat), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction(
        "bool DragVec3(const string &in, float &inout, float &inout, float &inout, float speed = 0.1f)",
        asFUNCTION(UI_DragVec3), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("bool Combo(const string &in, string &inout, const string &in)",
                                         asFUNCTION(UI_Combo), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("void BeginDisabled(bool disabled = true)", asFUNCTION(UI_BeginDisabled),
                                         asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("void EndDisabled()", asFUNCTION(UI_EndDisabled), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("void PushStyleColor(int, float, float, float, float)",
                                         asFUNCTION(UI_PushStyleColor), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("void PopStyleColor(int count = 1)", asFUNCTION(UI_PopStyleColor),
                                         asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("bool ImageViewport(const string &in)", asFUNCTION(UI_ImageViewport),
                                         asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("void InlineMessage(int, const string &in)", asFUNCTION(UI_InlineMessage),
                                         asCALL_CDECL));
    engine->SetDefaultNamespace("");
}

void RegisterEditorContextBindings(void* scriptEngine) {
    auto* engine = static_cast<asIScriptEngine*>(scriptEngine);
    if (!engine)
        return;

    engine->SetDefaultNamespace("Editor");
    Check(engine->RegisterGlobalFunction("bool IsEditing()", asFUNCTION(Editor_IsEditing), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("bool IsPlaying()", asFUNCTION(Editor_IsPlaying), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("bool ExecuteAction(const string &in)", asFUNCTION(Editor_ExecuteAction),
                                         asCALL_CDECL));
    engine->SetDefaultNamespace("Selection");
    Check(engine->RegisterGlobalFunction("uint64 GetActorId()", asFUNCTION(Selection_GetActorId), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("void SelectActor(uint64)", asFUNCTION(Selection_SelectActor), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("void SelectActor(uint64, int)", asFUNCTION(Selection_SelectActorMode),
                                         asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("void SelectAsset(const string &in)", asFUNCTION(Selection_SelectAsset),
                                         asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("void Clear()", asFUNCTION(Selection_Clear), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("string GetAssetPath()", asFUNCTION(Selection_GetAssetPath), asCALL_CDECL));
    engine->SetDefaultNamespace("Scene");
    Check(engine->RegisterGlobalFunction("string GetRootActors()", asFUNCTION(Scene_GetRootActors), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("string GetChildren(uint64)", asFUNCTION(Scene_GetChildren), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("string GetActorName(uint64)", asFUNCTION(Scene_GetActorName), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("uint64 CreateActor(const string &in)", asFUNCTION(Scene_CreateActor),
                                         asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("bool DeleteActor(uint64)", asFUNCTION(Scene_DeleteActor), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("bool SetActorName(uint64, const string &in)", asFUNCTION(Scene_SetActorName),
                                         asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("string FindActorsWithComponent(const string &in)",
                                         asFUNCTION(Scene_FindActorsWithComponent), asCALL_CDECL));
    engine->SetDefaultNamespace("Commands");
    Check(engine->RegisterGlobalFunction("bool ExecuteAction(const string &in)", asFUNCTION(Commands_ExecuteAction),
                                         asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("uint64 CreateActor(const string &in)", asFUNCTION(Commands_CreateActor),
                                         asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("bool DeleteActor(uint64)", asFUNCTION(Commands_DeleteActor), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("bool RenameActor(uint64, const string &in)", asFUNCTION(Commands_RenameActor),
                                         asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("bool SetActorActive(uint64, bool)", asFUNCTION(Commands_SetActorActive),
                                         asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("bool SetActorTag(uint64, const string &in)", asFUNCTION(Commands_SetActorTag),
                                         asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("bool SetActorLayer(uint64, uint)", asFUNCTION(Commands_SetActorLayer),
                                         asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("bool MoveActor(uint64, uint64, uint64)", asFUNCTION(Commands_MoveActor),
                                         asCALL_CDECL));
    engine->SetDefaultNamespace("Components");
    Check(
        engine->RegisterGlobalFunction("bool Has(uint64, const string &in)", asFUNCTION(Components_Has), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("string GetJson(uint64, const string &in)", asFUNCTION(Components_GetJson),
                                         asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("bool SetJson(uint64, const string &in, const string &in)",
                                         asFUNCTION(Components_SetJson), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction(
        "bool SetFieldJson(uint64, const string &in, const string &in, const string &in)",
        asFUNCTION(Components_SetFieldJson), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("string GetMetadata(const string &in)", asFUNCTION(Components_GetMetadata),
                                         asCALL_CDECL));
    engine->SetDefaultNamespace("Assets");
    Check(engine->RegisterGlobalFunction("string List(const string &in, bool recursive = true)",
                                         asFUNCTION(Assets_List), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("string ListByType(const string &in)", asFUNCTION(Assets_ListByType),
                                         asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("void Refresh()", asFUNCTION(Assets_Refresh), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("bool Delete(const string &in)", asFUNCTION(Assets_Delete), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("bool Rename(const string &in, const string &in)", asFUNCTION(Assets_Rename),
                                         asCALL_CDECL));
    Check(
        engine->RegisterGlobalFunction("bool Duplicate(const string &in)", asFUNCTION(Assets_Duplicate), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("bool Reimport(const string &in)", asFUNCTION(Assets_Reimport), asCALL_CDECL));
    engine->SetDefaultNamespace("DragDrop");
    Check(engine->RegisterGlobalFunction("bool BeginSource(const string &in, const string &in, const string &in)",
                                         asFUNCTION(DragDrop_BeginSource), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("string Accept(const string &in)", asFUNCTION(DragDrop_Accept), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("bool ApplyActorDrop(uint64, uint64, uint64)",
                                         asFUNCTION(DragDrop_ApplyActorDrop), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("bool ApplyAssetDrop(const string &in, const string &in)",
                                         asFUNCTION(DragDrop_ApplyAssetDrop), asCALL_CDECL));
    engine->SetDefaultNamespace("Transaction");
    Check(engine->RegisterGlobalFunction("bool BeginSnapshot(const string &in)", asFUNCTION(Transaction_BeginSnapshot),
                                         asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("bool CommitIfChanged()", asFUNCTION(Transaction_CommitIfChanged),
                                         asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("void Cancel()", asFUNCTION(Transaction_Cancel), asCALL_CDECL));
    engine->SetDefaultNamespace("Viewport");
    Check(engine->RegisterGlobalFunction("bool SetSceneViewportRect(float, float, float, float, bool)",
                                         asFUNCTION(Viewport_SetSceneViewportRect), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("bool SetGameViewportRect(float, float, float, float)",
                                         asFUNCTION(Viewport_SetGameViewportRect), asCALL_CDECL));
    engine->SetDefaultNamespace("Project");
    Check(engine->RegisterGlobalFunction("string GetContentRoot()", asFUNCTION(Project_GetContentRoot), asCALL_CDECL));
    engine->SetDefaultNamespace("Validation");
    Check(engine->RegisterGlobalFunction("void ReportInfo(const string &in)", asFUNCTION(Validation_ReportInfo),
                                         asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("void ReportWarning(const string &in)", asFUNCTION(Validation_ReportWarning),
                                         asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("void ReportError(const string &in)", asFUNCTION(Validation_ReportError),
                                         asCALL_CDECL));
    engine->SetDefaultNamespace("PanelState");
    Check(engine->RegisterGlobalFunction("string GetString(const string &in, const string &in)",
                                         asFUNCTION(PanelState_GetString), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("void SetString(const string &in, const string &in)",
                                         asFUNCTION(PanelState_SetString), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("bool GetBool(const string &in, bool)", asFUNCTION(PanelState_GetBool),
                                         asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("void SetBool(const string &in, bool)", asFUNCTION(PanelState_SetBool),
                                         asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("float GetFloat(const string &in, float)", asFUNCTION(PanelState_GetFloat),
                                         asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("void SetFloat(const string &in, float)", asFUNCTION(PanelState_SetFloat),
                                         asCALL_CDECL));
    engine->SetDefaultNamespace("Hierarchy");
    Check(engine->RegisterGlobalFunction("string GetRows(const string &in)", asFUNCTION(Hierarchy_GetRows),
                                         asCALL_CDECL));
    engine->SetDefaultNamespace("Inspector");
    Check(engine->RegisterGlobalFunction("string GetSelection()", asFUNCTION(Inspector_GetSelection), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("string GetComponentTypes(uint64)", asFUNCTION(Inspector_GetComponentTypes),
                                         asCALL_CDECL));
    engine->SetDefaultNamespace("AssetBrowser");
    Check(engine->RegisterGlobalFunction("string GetFolders()", asFUNCTION(AssetBrowser_GetFolders), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("string GetAssets(const string &in, bool, const string &in)",
                                         asFUNCTION(AssetBrowser_GetAssets), asCALL_CDECL));
    engine->SetDefaultNamespace("Log");
    Check(engine->RegisterGlobalFunction("string GetRows()", asFUNCTION(Log_GetRows), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("void Clear()", asFUNCTION(Log_Clear), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("bool GetAutoScroll()", asFUNCTION(Log_GetAutoScroll), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("void SetAutoScroll(bool)", asFUNCTION(Log_SetAutoScroll), asCALL_CDECL));
    engine->SetDefaultNamespace("Profiler");
    Check(engine->RegisterGlobalFunction("string GetRows()", asFUNCTION(Profiler_GetRows), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("string GetFrameStats()", asFUNCTION(Profiler_GetFrameStats), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("void Clear()", asFUNCTION(Profiler_Clear), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("bool GetEnabled()", asFUNCTION(Profiler_GetEnabled), asCALL_CDECL));
    Check(engine->RegisterGlobalFunction("void SetEnabled(bool)", asFUNCTION(Profiler_SetEnabled), asCALL_CDECL));
    engine->SetDefaultNamespace("");
}

} // namespace Editor::Scripting
