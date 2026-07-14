#include "Editor/EditorPanels.h"

#include "Editor/EditorContext.h"
#include "Editor/EditorInspectorSection.h"
#include "Editor/EditorOperators.h"
#include "Editor/EditorUI/EditorAngelScriptDomain.h"
#include "Editor/UI/EditorWidgets.h"
#include "Editor/UI/EditorViewportPolicy.h"
#include "Editor/InspectorSections.h"
#include "Core/Logger.h"
#include "Scene/Actor.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"
#include "Scene/PrefabSystem.h"

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <set>
#include <sstream>
#include <vector>

namespace {
bool MatchesScriptInspectorTarget(const EditorScriptInspectorSpec& spec, const EditorSelectObject& selected,
                                  Actor* actor) {
    if (spec.targetType == "*")
        return true;
    if (spec.targetType == "Actor")
        return selected.IsActor();
    if (spec.targetType == "Asset")
        return selected.IsAsset();
    return actor && actor->HasComponentType(spec.targetType);
}

std::string LowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool ContainsCaseInsensitive(const std::string& value, const char* filter) {
    if (!filter || filter[0] == '\0')
        return true;
    return LowerCopy(value).find(LowerCopy(filter)) != std::string::npos;
}

std::string ComponentDisplayName(std::string type) {
    const std::string suffix = "Component";
    if (type.size() > suffix.size() && type.compare(type.size() - suffix.size(), suffix.size(), suffix) == 0) {
        type.resize(type.size() - suffix.size());
    }
    std::string result;
    for (size_t index = 0; index < type.size(); ++index) {
        const char ch = type[index];
        if (index > 0 && ch >= 'A' && ch <= 'Z' &&
            ((type[index - 1] >= 'a' && type[index - 1] <= 'z') ||
             (index + 1 < type.size() && type[index + 1] >= 'a' && type[index + 1] <= 'z'))) {
            result.push_back(' ');
        }
        result.push_back(ch);
    }
    return result.empty() ? type : result;
}

const char* ComponentCategory(const std::string& type) {
    if (type == "MeshRenderer" || type == "SkinnedMeshRenderer" || type == "Camera" || type == "Light" ||
        type == "PostProcess") {
        return "Rendering";
    }
    if (type == "AudioSource")
        return "Audio";
    if (type == "RigidBody" || type == "BoxCollider" || type == "SphereCollider" || type == "CapsuleCollider" ||
        type == "CharacterController") {
        return "Physics";
    }
    if (type == "Script")
        return "Scripting";
    if (type.rfind("UI", 0) == 0)
        return "UI";
    return "Gameplay";
}

bool ComponentMatchesFilter(const std::string& type, const char* filter) {
    return ContainsCaseInsensitive(type, filter) || ContainsCaseInsensitive(ComponentDisplayName(type), filter) ||
           ContainsCaseInsensitive(ComponentCategory(type), filter);
}

void CopyActorNameToBuffer(const Actor& actor, char* buffer, size_t bufferSize) {
    if (!buffer || bufferSize == 0)
        return;
    std::fill(buffer, buffer + bufferSize, '\0');
    std::strncpy(buffer, actor.GetName().c_str(), bufferSize - 1);
}

void CopyActorTagToBuffer(const Actor& actor, char* buffer, size_t bufferSize) {
    if (!buffer || bufferSize == 0)
        return;
    std::fill(buffer, buffer + bufferSize, '\0');
    std::strncpy(buffer, actor.GetTag().c_str(), bufferSize - 1);
}

bool CommitInspectorActorName(EditorContext& context, Actor& actor, const char* name) {
    const std::string afterName = name ? name : "";
    if (afterName == actor.GetName())
        return false;
    if (auto* operators = context.GetOperators()) {
        return operators->Commands().RenameActor(context, actor.GetID(), afterName);
    }
    EditorCommandOperator commandOperator;
    return commandOperator.RenameActor(context, actor.GetID(), afterName);
}

bool CommitInspectorActorActive(EditorContext& context, Actor& actor, bool active) {
    if (active == actor.IsActiveSelf())
        return false;
    if (auto* operators = context.GetOperators()) {
        return operators->Commands().SetActorActive(context, actor.GetID(), active);
    }
    EditorCommandOperator commandOperator;
    return commandOperator.SetActorActive(context, actor.GetID(), active);
}

bool CommitInspectorActorTag(EditorContext& context, Actor& actor, const char* tag) {
    const std::string afterTag = tag ? tag : "";
    if (afterTag == actor.GetTag())
        return false;
    if (auto* operators = context.GetOperators()) {
        return operators->Commands().SetActorTag(context, actor.GetID(), afterTag);
    }
    EditorCommandOperator commandOperator;
    return commandOperator.SetActorTag(context, actor.GetID(), afterTag);
}

bool CommitInspectorActorLayer(EditorContext& context, Actor& actor, uint32_t layer) {
    if (layer == actor.GetLayer())
        return false;
    if (auto* operators = context.GetOperators()) {
        return operators->Commands().SetActorLayer(context, actor.GetID(), layer);
    }
    EditorCommandOperator commandOperator;
    return commandOperator.SetActorLayer(context, actor.GetID(), layer);
}

bool CommitInspectorActorStatic(EditorContext& context, Actor& actor, bool isStatic) {
    if (isStatic == actor.IsStatic())
        return false;
    if (auto* operators = context.GetOperators()) {
        return operators->Commands().SetActorStatic(context, actor.GetID(), isStatic);
    }
    EditorCommandOperator commandOperator;
    return commandOperator.SetActorStatic(context, actor.GetID(), isStatic);
}

std::vector<Actor*> ResolveSelectedActors(EditorContext& context, Scene& scene) {
    std::vector<Actor*> actors;
    for (uint64_t actorID : context.GetSelection().GetActorIDs()) {
        if (Actor* actor = scene.FindByID(actorID))
            actors.push_back(actor);
    }
    return actors;
}

bool CommonActiveState(const std::vector<Actor*>& actors, bool& active) {
    if (actors.empty())
        return false;
    active = actors.front()->IsActiveSelf();
    for (const Actor* actor : actors) {
        if (!actor || actor->IsActiveSelf() != active)
            return false;
    }
    return true;
}

bool CommonTagValue(const std::vector<Actor*>& actors, std::string& tag) {
    if (actors.empty())
        return false;
    tag = actors.front()->GetTag();
    for (const Actor* actor : actors) {
        if (!actor || actor->GetTag() != tag)
            return false;
    }
    return true;
}

bool CommonLayerValue(const std::vector<Actor*>& actors, uint32_t& layer) {
    if (actors.empty())
        return false;
    layer = actors.front()->GetLayer();
    for (const Actor* actor : actors) {
        if (!actor || actor->GetLayer() != layer)
            return false;
    }
    return true;
}

bool CommonStaticState(const std::vector<Actor*>& actors, bool& isStatic) {
    if (actors.empty())
        return false;
    isStatic = actors.front()->IsStatic();
    for (const Actor* actor : actors) {
        if (!actor || actor->IsStatic() != isStatic)
            return false;
    }
    return true;
}

bool CommonPositionValue(const std::vector<Actor*>& actors, Vec3& position) {
    if (actors.empty())
        return false;
    position = actors.front()->GetTransform().position;
    for (const Actor* actor : actors) {
        if (!actor)
            return false;
        const Vec3 value = actor->GetTransform().position;
        if (value.x != position.x || value.y != position.y || value.z != position.z) {
            return false;
        }
    }
    return true;
}

bool CommonRotationValue(const std::vector<Actor*>& actors, Vec3& rotation) {
    if (actors.empty())
        return false;
    rotation = actors.front()->GetTransform().rotation;
    for (const Actor* actor : actors) {
        if (!actor)
            return false;
        const Vec3 value = actor->GetTransform().rotation;
        if (value.x != rotation.x || value.y != rotation.y || value.z != rotation.z) {
            return false;
        }
    }
    return true;
}

bool CommonScaleValue(const std::vector<Actor*>& actors, Vec3& scale) {
    if (actors.empty())
        return false;
    scale = actors.front()->GetTransform().scale;
    for (const Actor* actor : actors) {
        if (!actor)
            return false;
        const Vec3 value = actor->GetTransform().scale;
        if (value.x != scale.x || value.y != scale.y || value.z != scale.z) {
            return false;
        }
    }
    return true;
}

std::vector<uint64_t> ActorIDsFor(const std::vector<Actor*>& actors) {
    std::vector<uint64_t> ids;
    ids.reserve(actors.size());
    for (const Actor* actor : actors) {
        if (actor)
            ids.push_back(actor->GetID());
    }
    return ids;
}

std::vector<std::string> CommonComponentTypes(const std::vector<Actor*>& actors) {
    if (actors.empty() || !actors.front())
        return {};

    std::set<std::string> common;
    actors.front()->ForEachComponent([&](Component& component) { common.insert(component.GetTypeName()); });

    for (size_t index = 1; index < actors.size() && !common.empty(); ++index) {
        Actor* actor = actors[index];
        if (!actor)
            return {};
        for (auto it = common.begin(); it != common.end();) {
            if (!actor->HasComponentType(*it))
                it = common.erase(it);
            else
                ++it;
        }
    }
    return {common.begin(), common.end()};
}

bool IsEditableVec3Json(const nlohmann::json& value) {
    return value.is_array() && value.size() == 3 && value[0].is_number() && value[1].is_number() &&
           value[2].is_number();
}

bool IsEditableCommonProperty(const nlohmann::json& value) {
    return value.is_boolean() || value.is_number() || IsEditableVec3Json(value);
}

bool CompatiblePropertyType(const nlohmann::json& a, const nlohmann::json& b) {
    if (a.is_number() && b.is_number())
        return true;
    if (IsEditableVec3Json(a) && IsEditableVec3Json(b))
        return true;
    return a.type() == b.type();
}

struct CommonComponentProperty {
    std::string name;
    nlohmann::json value;
    bool hasCommonValue = true;
};

std::vector<CommonComponentProperty> CommonComponentProperties(const std::vector<Actor*>& actors,
                                                               const std::string& typeName) {
    if (actors.empty() || typeName.empty())
        return {};
    const Component* firstComponent = actors.front() ? actors.front()->GetComponentByTypeName(typeName) : nullptr;
    if (!firstComponent)
        return {};

    nlohmann::json firstData = nlohmann::json::object();
    firstComponent->Serialize(firstData);
    if (!firstData.is_object())
        return {};

    std::vector<CommonComponentProperty> properties;
    for (const auto& item : firstData.items()) {
        if (!IsEditableCommonProperty(item.value()))
            continue;

        CommonComponentProperty property;
        property.name = item.key();
        property.value = item.value();

        bool presentOnAll = true;
        for (size_t index = 1; index < actors.size(); ++index) {
            const Component* component = actors[index] ? actors[index]->GetComponentByTypeName(typeName) : nullptr;
            if (!component) {
                presentOnAll = false;
                break;
            }

            nlohmann::json data = nlohmann::json::object();
            component->Serialize(data);
            auto found = data.find(property.name);
            if (!data.is_object() || found == data.end() || !CompatiblePropertyType(property.value, *found) ||
                !IsEditableCommonProperty(*found)) {
                presentOnAll = false;
                break;
            }
            if (*found != property.value)
                property.hasCommonValue = false;
        }

        if (presentOnAll)
            properties.push_back(std::move(property));
    }
    return properties;
}

#if defined(MYENGINE_ENABLE_IMGUI)
bool ShouldCaptureInspectorEditSnapshot(bool hasActorSelection, bool canEditSelection, bool transactionActive) {
    if (!hasActorSelection || !canEditSelection || transactionActive)
        return false;
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)) {
        return false;
    }

    const ImGuiIO& io = ImGui::GetIO();
    return ImGui::IsAnyItemActive() || ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
           ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle) ||
           io.InputQueueCharacters.Size > 0 || ImGui::IsKeyPressed(ImGuiKey_Backspace) ||
           ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Enter) ||
           ImGui::IsKeyPressed(ImGuiKey_Space) || ImGui::IsKeyPressed(ImGuiKey_Tab) ||
           ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_RightArrow) ||
           ImGui::IsKeyPressed(ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_DownArrow);
}

bool ShouldCommitInspectorEditSnapshot(bool hasActorSelection, bool transactionActive) {
    return hasActorSelection && transactionActive && !ImGui::IsAnyItemActive();
}
#endif
} // namespace

InspectorPanel::InspectorPanel(std::shared_ptr<EditorGizmoState> state)
    : EditorPanel("inspector", "Inspector"), m_State(std::move(state)), m_SectionRegistry() {
    auto sections = CreateDefaultInspectorSections();
    for (auto& section : sections)
        m_SectionRegistry.Register(std::move(section));
}

InspectorPanel::~InspectorPanel() = default;

void InspectorPanel::OnAttach(EditorContext& context) {
    EditorPanel::OnAttach(context);
    m_SelectedObject = context.GetSelection().GetPrimaryObject();
    m_SelectionListenerID = context.GetSelection().SubscribeSelectionChanged(
        [this](const EditorSelectionChangedEvent& event) { OnSelectionChanged(event); });
}

void InspectorPanel::OnDetach() {
    if (EditorContext* context = GetContext()) {
        context->GetSelection().UnsubscribeSelectionChanged(m_SelectionListenerID);
    }
    m_SelectionListenerID = 0;
    m_SelectedObject = {};
    m_Transaction.Cancel();
    EditorPanel::OnDetach();
}

void InspectorPanel::OnSelectionChanged(const EditorSelectionChangedEvent& event) {
    m_SelectedObject = event.current;
    m_Transaction.Cancel();
    m_ActorNameEditID = 0;
    m_ActorNameDirty = false;
    std::fill(std::begin(m_ActorNameBuffer), std::end(m_ActorNameBuffer), '\0');
    m_ActorTagEditID = 0;
    m_ActorTagDirty = false;
    std::fill(std::begin(m_ActorTagBuffer), std::end(m_ActorTagBuffer), '\0');
    m_MultiActorTagDirty = false;
    std::fill(std::begin(m_MultiActorTagBuffer), std::end(m_MultiActorTagBuffer), '\0');
    std::fill(std::begin(m_MultiAddComponentSearch), std::end(m_MultiAddComponentSearch), '\0');
    m_PrefabOperationMessage.clear();
    m_PrefabOperationMessageIsError = false;
}

void InspectorPanel::DrawContent() {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (TryDrawScriptedBody("inspector"))
        return;

    EditorContext* context = GetContext();
    Scene* scene = context ? context->GetInspectorScene() : nullptr;
    if (!scene)
        return;

    Actor* actor = m_SelectedObject.IsActor() ? context->GetSelection().ResolveActor(*scene) : nullptr;
    const uint64_t selection = actor ? actor->GetID() : 0;
    bool directCommand = false;

    const bool canEditSelection = context->CanEditSelection();
    const bool captureInspectorSnapshot =
        ShouldCaptureInspectorEditSnapshot(actor != nullptr, canEditSelection, m_Transaction.IsActive());
    const std::string before = captureInspectorSnapshot ? SceneSerializer::SaveToString(*scene) : std::string{};

    if (m_SelectedObject.IsActor() && m_SelectedObject.GetWorldKind() == EditorSelectionWorldKind::Play) {
        ImGui::TextColored({1.0f, 0.75f, 0.25f, 1.0f}, "PlayWorld runtime object - read-only");
        ImGui::Separator();
    }

    ImGui::BeginDisabled(!canEditSelection);
    const bool multiActorSelection = m_SelectedObject.IsActor() && context->GetSelection().GetActorIDs().size() > 1;
    if (multiActorSelection) {
        std::vector<Actor*> actors = ResolveSelectedActors(*context, *scene);
        const std::vector<uint64_t> actorIDs = ActorIDsFor(actors);
        ImGui::Text("%zu Actors Selected", actors.size());
        ImGui::Separator();

        bool commonActive = false;
        const bool hasCommonActive = CommonActiveState(actors, commonActive);
        ImGui::Text("Active: %s", hasCommonActive ? (commonActive ? "On" : "Off") : "Mixed");
        if (ImGui::Button("Set Active")) {
            if (auto* operators = context->GetOperators()) {
                operators->Commands().SetActorsActive(*context, actorIDs, true);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Set Inactive")) {
            if (auto* operators = context->GetOperators()) {
                operators->Commands().SetActorsActive(*context, actorIDs, false);
            }
        }

        std::string commonTag;
        const bool hasCommonTag = CommonTagValue(actors, commonTag);
        if (!m_MultiActorTagDirty) {
            std::fill(std::begin(m_MultiActorTagBuffer), std::end(m_MultiActorTagBuffer), '\0');
            if (hasCommonTag) {
                const size_t count = std::min(commonTag.size(), sizeof(m_MultiActorTagBuffer) - 1);
                std::memcpy(m_MultiActorTagBuffer, commonTag.data(), count);
            }
        }
        if (!hasCommonTag)
            ImGui::TextDisabled("Tag: Mixed");
        if (ImGui::InputText("Tag", m_MultiActorTagBuffer, sizeof(m_MultiActorTagBuffer))) {
            m_MultiActorTagDirty = true;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            if (auto* operators = context->GetOperators()) {
                operators->Commands().SetActorsTag(*context, actorIDs, m_MultiActorTagBuffer);
            }
            m_MultiActorTagDirty = false;
        }

        uint32_t layer = 0;
        const bool hasCommonLayer = CommonLayerValue(actors, layer);
        if (!hasCommonLayer)
            ImGui::TextDisabled("Layer: Mixed");
        if (ImGui::InputScalar("Layer", ImGuiDataType_U32, &layer)) {
            if (auto* operators = context->GetOperators()) {
                operators->Commands().SetActorsLayer(*context, actorIDs, layer);
            }
        }

        bool commonStatic = false;
        const bool hasCommonStatic = CommonStaticState(actors, commonStatic);
        ImGui::Text("Static: %s", hasCommonStatic ? (commonStatic ? "On" : "Off") : "Mixed");
        if (ImGui::Button("Set Static")) {
            if (auto* operators = context->GetOperators()) {
                operators->Commands().SetActorsStatic(*context, actorIDs, true);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Static")) {
            if (auto* operators = context->GetOperators()) {
                operators->Commands().SetActorsStatic(*context, actorIDs, false);
            }
        }

        ImGui::Separator();
        Vec3 position = Vec3::Zero();
        const bool hasCommonPosition = CommonPositionValue(actors, position);
        if (!hasCommonPosition)
            ImGui::TextDisabled("Position: Mixed");
        float positionValues[3] = {position.x, position.y, position.z};
        if (ImGui::DragFloat3("Position", positionValues, 0.05f)) {
            if (auto* operators = context->GetOperators()) {
                operators->Commands().SetActorsPosition(*context, actorIDs,
                                                        {positionValues[0], positionValues[1], positionValues[2]});
            }
        }
        Vec3 rotation = Vec3::Zero();
        const bool hasCommonRotation = CommonRotationValue(actors, rotation);
        if (!hasCommonRotation)
            ImGui::TextDisabled("Rotation: Mixed");
        float rotationValues[3] = {rotation.x, rotation.y, rotation.z};
        if (ImGui::DragFloat3("Rotation", rotationValues, 0.5f)) {
            if (auto* operators = context->GetOperators()) {
                operators->Commands().SetActorsRotation(*context, actorIDs,
                                                        {rotationValues[0], rotationValues[1], rotationValues[2]});
            }
        }
        Vec3 scale = Vec3::One();
        const bool hasCommonScale = CommonScaleValue(actors, scale);
        if (!hasCommonScale)
            ImGui::TextDisabled("Scale: Mixed");
        float scaleValues[3] = {scale.x, scale.y, scale.z};
        if (ImGui::DragFloat3("Scale", scaleValues, 0.01f)) {
            if (auto* operators = context->GetOperators()) {
                operators->Commands().SetActorsScale(*context, actorIDs,
                                                     {scaleValues[0], scaleValues[1], scaleValues[2]});
            }
        }

        ImGui::Separator();
        const std::vector<std::string> commonComponents = CommonComponentTypes(actors);
        if (ImGui::TreeNodeEx("Common Components", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (commonComponents.empty()) {
                ImGui::TextDisabled("No common components");
            } else {
                for (const std::string& typeName : commonComponents) {
                    ImGui::PushID(typeName.c_str());
                    const bool open = ImGui::TreeNodeEx(typeName.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Remove")) {
                        if (auto* operators = context->GetOperators()) {
                            operators->Components().RemoveComponents(*context, actorIDs, typeName);
                        }
                    }
                    if (open) {
                        const std::vector<CommonComponentProperty> properties =
                            CommonComponentProperties(actors, typeName);
                        if (properties.empty()) {
                            ImGui::TextDisabled("No common editable fields");
                        } else {
                            for (const CommonComponentProperty& property : properties) {
                                ImGui::PushID(property.name.c_str());
                                if (!property.hasCommonValue) {
                                    ImGui::TextDisabled("%s: Mixed", property.name.c_str());
                                }
                                if (property.value.is_boolean()) {
                                    bool value = property.value.get<bool>();
                                    if (ImGui::Checkbox(property.name.c_str(), &value)) {
                                        if (auto* operators = context->GetOperators()) {
                                            operators->Components().SetComponentPropertyForActors(
                                                *context, actorIDs, typeName, property.name, value);
                                        }
                                    }
                                } else if (property.value.is_number_integer() || property.value.is_number_unsigned()) {
                                    int value = property.value.get<int>();
                                    if (ImGui::InputInt(property.name.c_str(), &value)) {
                                        if (auto* operators = context->GetOperators()) {
                                            operators->Components().SetComponentPropertyForActors(
                                                *context, actorIDs, typeName, property.name, value);
                                        }
                                    }
                                } else if (property.value.is_number_float()) {
                                    float value = property.value.get<float>();
                                    if (ImGui::DragFloat(property.name.c_str(), &value, 0.01f)) {
                                        if (auto* operators = context->GetOperators()) {
                                            operators->Components().SetComponentPropertyForActors(
                                                *context, actorIDs, typeName, property.name, value);
                                        }
                                    }
                                } else if (IsEditableVec3Json(property.value)) {
                                    float values[3] = {property.value[0].get<float>(), property.value[1].get<float>(),
                                                       property.value[2].get<float>()};
                                    if (ImGui::DragFloat3(property.name.c_str(), values, 0.01f)) {
                                        if (auto* operators = context->GetOperators()) {
                                            operators->Components().SetComponentPropertyForActors(
                                                *context, actorIDs, typeName, property.name,
                                                nlohmann::json::array({values[0], values[1], values[2]}));
                                        }
                                    }
                                }
                                ImGui::PopID();
                            }
                        }
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
            }
            ImGui::TreePop();
        }

        ImGui::Separator();
        Editor::UI::EditorViewportPolicy::BindNextPopupToCurrentViewport();
        if (ImGui::BeginCombo("##MultiAddComponent", "Add Component to Selection...")) {
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##MultiAddComponentSearch", "Search components...", m_MultiAddComponentSearch,
                                     sizeof(m_MultiAddComponentSearch));
            ImGui::Separator();

            std::vector<std::string> types = ComponentRegistry::Get().GetRegisteredTypes();
            std::sort(types.begin(), types.end(), [](const std::string& left, const std::string& right) {
                const std::string leftCategory = ComponentCategory(left);
                const std::string rightCategory = ComponentCategory(right);
                if (leftCategory != rightCategory) {
                    return leftCategory < rightCategory;
                }
                return ComponentDisplayName(left) < ComponentDisplayName(right);
            });

            constexpr std::array<const char*, 6> kCategories = {"Rendering", "Physics", "Audio",
                                                                "Scripting", "UI",      "Gameplay"};
            for (const char* category : kCategories) {
                bool hasVisible = false;
                for (const std::string& type : types) {
                    if (std::strcmp(ComponentCategory(type), category) == 0 &&
                        ComponentMatchesFilter(type, m_MultiAddComponentSearch)) {
                        hasVisible = true;
                        break;
                    }
                }
                if (!hasVisible || !ImGui::BeginMenu(category))
                    continue;
                for (const std::string& type : types) {
                    if (std::strcmp(ComponentCategory(type), category) != 0 ||
                        !ComponentMatchesFilter(type, m_MultiAddComponentSearch)) {
                        continue;
                    }
                    const bool existsOnAny = std::any_of(actors.begin(), actors.end(), [&](const Actor* selectedActor) {
                        return selectedActor && selectedActor->HasComponentType(type);
                    });
                    if (existsOnAny)
                        ImGui::BeginDisabled();
                    const std::string label = ComponentDisplayName(type);
                    if (ImGui::Selectable(label.c_str()) && !existsOnAny) {
                        if (auto* operators = context->GetOperators()) {
                            operators->Components().AddComponents(*context, actorIDs, type, nlohmann::json::object());
                        }
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", type.c_str());
                    if (existsOnAny)
                        ImGui::EndDisabled();
                }
                ImGui::EndMenu();
            }
            ImGui::EndCombo();
        }

        ImGui::EndDisabled();
        return;
    }

    if (actor && actor->IsPrefabRoot()) {
        ImGui::Text("Prefab: %s", actor->GetPrefabAssetPath().c_str());
        bool refreshed = false;
        auto* operators = context->GetOperators();
        std::vector<EditorPrefabOperator::OverrideInfo> prefabOverrides;
        auto refreshPrefabOverrides = [&]() {
            prefabOverrides = operators ? operators->Prefabs().GetOverrides(*context, actor->GetID())
                                        : std::vector<EditorPrefabOperator::OverrideInfo>{};
        };
        refreshPrefabOverrides();
        const bool hasPrefabOverrides = !prefabOverrides.empty();
        auto setPrefabOperationMessage = [&](std::string message, bool error) {
            m_PrefabOperationMessage = std::move(message);
            m_PrefabOperationMessageIsError = error;
        };
        ImGui::BeginDisabled(!operators || !hasPrefabOverrides);
        if (ImGui::Button("Apply All")) {
            refreshed = operators->Prefabs().ApplyAll(*context, actor->GetID());
            setPrefabOperationMessage(refreshed ? "Applied prefab overrides" : "Failed to apply prefab overrides",
                                      !refreshed);
            directCommand = directCommand || refreshed;
        }
        ImGui::SameLine();
        if (ImGui::Button("Revert All")) {
            refreshed = operators->Prefabs().RevertAll(*context, actor->GetID());
            setPrefabOperationMessage(refreshed ? "Reverted prefab overrides" : "Failed to revert prefab overrides",
                                      !refreshed);
            directCommand = directCommand || refreshed;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!operators);
        if (ImGui::Button("Unpack")) {
            refreshed = operators->Prefabs().Unpack(*context, actor->GetID());
            setPrefabOperationMessage(refreshed ? "Unpacked prefab instance" : "Failed to unpack prefab instance",
                                      !refreshed);
            directCommand = directCommand || refreshed;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!operators);
        if (ImGui::Button("Select Source")) {
            const std::string sourcePath = PrefabSystem::ResolvePrefabPath(actor->GetPrefabAssetPath()).string();
            const bool selectedSource = operators->Selection().SelectAsset(*context, sourcePath);
            if (selectedSource) {
                context->RequestPanelFocus("inspector");
            }
            setPrefabOperationMessage(selectedSource ? "Selected prefab source: " +
                                                           std::filesystem::path(sourcePath).filename().string()
                                                     : "Failed to select prefab source: " + sourcePath,
                                      !selectedSource);
        }
        ImGui::EndDisabled();
        if (!m_PrefabOperationMessage.empty()) {
            Editor::UI::EditorWidgets::InlineMessage(m_PrefabOperationMessageIsError
                                                         ? Editor::UI::EditorNotificationType::Error
                                                         : Editor::UI::EditorNotificationType::Success,
                                                     m_PrefabOperationMessage.c_str());
        }
        if (refreshed) {
            actor = context->GetSelection().ResolveActor(*scene);
            if (!actor) {
                ImGui::EndDisabled();
                return;
            }
            refreshPrefabOverrides();
        }
        if (actor && operators) {
            const auto& overrides = prefabOverrides;
            const std::string header = "Overrides (" + std::to_string(overrides.size()) + ")";
            if (ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                if (overrides.empty()) {
                    ImGui::TextDisabled("No overrides");
                } else {
                    ImGui::InputTextWithHint("##PrefabOverrideFilter", "Filter overrides...", m_PrefabOverrideFilter,
                                             sizeof(m_PrefabOverrideFilter));
                    ImGui::SameLine();
                    ImGui::Checkbox("Diagnostics only", &m_PrefabOverrideDiagnosticsOnly);
                    auto matchesOverrideFilter = [&](const auto& item) {
                        if (m_PrefabOverrideDiagnosticsOnly && item.diagnostic.empty()) {
                            return false;
                        }
                        if (m_PrefabOverrideFilter[0] == '\0')
                            return true;
                        return ContainsCaseInsensitive(item.category, m_PrefabOverrideFilter) ||
                               ContainsCaseInsensitive(item.target, m_PrefabOverrideFilter) ||
                               ContainsCaseInsensitive(item.property, m_PrefabOverrideFilter) ||
                               ContainsCaseInsensitive(item.valuePreview, m_PrefabOverrideFilter) ||
                               ContainsCaseInsensitive(item.diagnostic, m_PrefabOverrideFilter);
                    };
                    int visibleOverrideCount = 0;
                    int visibleReadyCount = 0;
                    int visibleBlockedCount = 0;
                    int totalBlockedCount = 0;
                    std::unordered_map<std::string, int> categoryCounts;
                    for (const auto& item : overrides) {
                        if (!item.diagnostic.empty()) {
                            ++totalBlockedCount;
                        }
                        if (matchesOverrideFilter(item)) {
                            ++visibleOverrideCount;
                            if (item.diagnostic.empty()) {
                                ++visibleReadyCount;
                            } else {
                                ++visibleBlockedCount;
                            }
                            ++categoryCounts[item.category];
                        }
                    }
                    ImGui::TextDisabled("%d shown / %d total - %d ready, %d blocked", visibleOverrideCount,
                                        static_cast<int>(overrides.size()), visibleReadyCount, visibleBlockedCount);
                    if (m_PrefabOverrideDiagnosticsOnly && totalBlockedCount == 0) {
                        ImGui::TextDisabled("No blocked overrides have diagnostics");
                    }
                    if (visibleOverrideCount == 0) {
                        ImGui::TextDisabled("No overrides match the current filter");
                    } else if (ImGui::BeginTable("##PrefabOverridesFiltered", 6,
                                                 ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
                                                     ImGuiTableFlags_Resizable)) {
                        ImGui::TableSetupColumn("Category");
                        ImGui::TableSetupColumn("Target");
                        ImGui::TableSetupColumn("Property");
                        ImGui::TableSetupColumn("Preview");
                        ImGui::TableSetupColumn("Status");
                        ImGui::TableSetupColumn("Actions");
                        ImGui::TableHeadersRow();
                        std::string currentCategory;
                        bool currentCategoryOpen = true;
                        for (const auto& item : overrides) {
                            if (!matchesOverrideFilter(item))
                                continue;
                            if (item.category != currentCategory) {
                                currentCategory = item.category;
                                auto categoryState =
                                    m_PrefabOverrideCategoryOpen.try_emplace(currentCategory, true).first;
                                currentCategoryOpen = categoryState->second;
                                ImGui::TableNextRow();
                                ImGui::TableSetColumnIndex(0);
                                const std::string groupLabel = std::string(currentCategoryOpen ? "v " : "> ") +
                                                               currentCategory + " (" +
                                                               std::to_string(categoryCounts[currentCategory]) + ")";
                                ImGui::PushID(("PrefabOverrideGroup" + currentCategory).c_str());
                                if (ImGui::Selectable(groupLabel.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                                    categoryState->second = !categoryState->second;
                                    currentCategoryOpen = categoryState->second;
                                }
                                ImGui::PopID();
                            }
                            if (!currentCategoryOpen)
                                continue;
                            ImGui::PushID(static_cast<int>(item.index));
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextUnformatted(item.category.c_str());
                            ImGui::TableSetColumnIndex(1);
                            ImGui::TextUnformatted(item.target.c_str());
                            ImGui::TableSetColumnIndex(2);
                            ImGui::TextUnformatted(item.property.c_str());
                            ImGui::TableSetColumnIndex(3);
                            ImGui::TextUnformatted(item.valuePreview.c_str());
                            ImGui::TableSetColumnIndex(4);
                            if (item.diagnostic.empty()) {
                                ImGui::TextDisabled("Ready");
                            } else {
                                ImGui::TextDisabled("%s", item.diagnostic.c_str());
                            }
                            if (!item.diagnostic.empty() && ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("%s", item.diagnostic.c_str());
                            }
                            ImGui::TableSetColumnIndex(5);
                            ImGui::BeginDisabled(!item.canApply);
                            if (ImGui::SmallButton("Apply")) {
                                refreshed = context->GetOperators()->Prefabs().ApplyOverride(*context, actor->GetID(),
                                                                                             item.index);
                                setPrefabOperationMessage(refreshed ? "Applied prefab override: " + item.label
                                                                    : "Failed to apply prefab override: " + item.label,
                                                          !refreshed);
                                directCommand = directCommand || refreshed;
                            }
                            ImGui::EndDisabled();
                            if (!item.canApply && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                                ImGui::SetTooltip("%s", item.diagnostic.empty()
                                                            ? "This override kind cannot be applied individually."
                                                            : item.diagnostic.c_str());
                            }
                            ImGui::SameLine();
                            ImGui::BeginDisabled(!item.canRevert);
                            if (ImGui::SmallButton("Revert")) {
                                refreshed = context->GetOperators()->Prefabs().RevertOverride(*context, actor->GetID(),
                                                                                              item.index);
                                setPrefabOperationMessage(refreshed ? "Reverted prefab override: " + item.label
                                                                    : "Failed to revert prefab override: " + item.label,
                                                          !refreshed);
                                directCommand = directCommand || refreshed;
                            }
                            ImGui::EndDisabled();
                            if (!item.canRevert && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                                ImGui::SetTooltip("%s", item.diagnostic.empty()
                                                            ? "This override kind cannot be reverted individually."
                                                            : item.diagnostic.c_str());
                            }
                            ImGui::PopID();
                            if (refreshed)
                                break;
                        }
                        ImGui::EndTable();
                        if (refreshed) {
                            actor = context->GetSelection().ResolveActor(*scene);
                            if (!actor) {
                                ImGui::TreePop();
                                ImGui::EndDisabled();
                                return;
                            }
                        }
                    }
                }
                ImGui::TreePop();
            }
        }
        ImGui::Separator();
    }
    if (canEditSelection && actor) {
        bool active = actor->IsActiveSelf();
        if (ImGui::Checkbox("Active", &active)) {
            const bool changed = CommitInspectorActorActive(*context, *actor, active);
            directCommand = directCommand || changed;
            actor = context->GetSelection().ResolveActor(*scene);
        }
        if (!actor) {
            ImGui::EndDisabled();
            return;
        }

        if (m_ActorNameEditID != actor->GetID()) {
            m_ActorNameEditID = actor->GetID();
            m_ActorNameDirty = false;
            CopyActorNameToBuffer(*actor, m_ActorNameBuffer, sizeof(m_ActorNameBuffer));
        } else if (!m_ActorNameDirty && std::string(m_ActorNameBuffer) != actor->GetName()) {
            CopyActorNameToBuffer(*actor, m_ActorNameBuffer, sizeof(m_ActorNameBuffer));
        }
        if (ImGui::InputText("Name", m_ActorNameBuffer, sizeof(m_ActorNameBuffer))) {
            m_ActorNameDirty = true;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            const bool renamed = CommitInspectorActorName(*context, *actor, m_ActorNameBuffer);
            directCommand = directCommand || renamed;
            actor = context->GetSelection().ResolveActor(*scene);
            if (actor) {
                m_ActorNameEditID = actor->GetID();
                CopyActorNameToBuffer(*actor, m_ActorNameBuffer, sizeof(m_ActorNameBuffer));
            }
            m_ActorNameDirty = false;
        }
        if (actor) {
            ImGui::Text("ID: %llu", static_cast<unsigned long long>(actor->GetID()));
        }
        if (actor) {
            if (m_ActorTagEditID != actor->GetID()) {
                m_ActorTagEditID = actor->GetID();
                m_ActorTagDirty = false;
                CopyActorTagToBuffer(*actor, m_ActorTagBuffer, sizeof(m_ActorTagBuffer));
            } else if (!m_ActorTagDirty && std::string(m_ActorTagBuffer) != actor->GetTag()) {
                CopyActorTagToBuffer(*actor, m_ActorTagBuffer, sizeof(m_ActorTagBuffer));
            }
            if (ImGui::InputText("Tag", m_ActorTagBuffer, sizeof(m_ActorTagBuffer))) {
                m_ActorTagDirty = true;
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                const bool changed = CommitInspectorActorTag(*context, *actor, m_ActorTagBuffer);
                directCommand = directCommand || changed;
                actor = context->GetSelection().ResolveActor(*scene);
                if (actor) {
                    m_ActorTagEditID = actor->GetID();
                    CopyActorTagToBuffer(*actor, m_ActorTagBuffer, sizeof(m_ActorTagBuffer));
                }
                m_ActorTagDirty = false;
            }
        }
        if (actor) {
            uint32_t layer = actor->GetLayer();
            if (ImGui::InputScalar("Layer", ImGuiDataType_U32, &layer)) {
                const bool changed = CommitInspectorActorLayer(*context, *actor, layer);
                directCommand = directCommand || changed;
                actor = context->GetSelection().ResolveActor(*scene);
            }
        }
        if (actor) {
            bool isStatic = actor->IsStatic();
            if (ImGui::Checkbox("Static", &isStatic)) {
                const bool changed = CommitInspectorActorStatic(*context, *actor, isStatic);
                directCommand = directCommand || changed;
                actor = context->GetSelection().ResolveActor(*scene);
            }
        }
    }

    for (const auto& section : m_SectionRegistry.GetSections()) {
        if (section->CanDraw(m_SelectedObject, *context))
            section->Draw(*context);
    }
    if (EditorAngelScriptDomain* domain = context->GetEditorScriptDomain();
        domain && domain->IsLoaded() && domain->GetConfig().enableInspectorExtensions) {
        for (const auto& section : domain->GetRegistry().GetInspectors()) {
            if (!MatchesScriptInspectorTarget(section, m_SelectedObject, actor))
                continue;
            std::string error;
            const std::string stateKey = "inspector:" + section.targetType;
            if (!domain->ExecuteExtension(section.callback, stateKey, *context, &error) && !error.empty()) {
                Logger::Warn("[EditorScript] Inspector section failed for ", section.targetType, ": ", error);
            }
        }
    }
    ImGui::EndDisabled();

    if (actor && captureInspectorSnapshot) {
        const std::string after = SceneSerializer::SaveToString(*scene);
        if (!directCommand && before != after && !m_Transaction.IsActive()) {
            m_Transaction.Begin("Inspector Edit", before, selection);
        }
    }
    if (ShouldCommitInspectorEditSnapshot(actor != nullptr, m_Transaction.IsActive())) {
        m_Transaction.Commit(*context);
    }
#endif
}
