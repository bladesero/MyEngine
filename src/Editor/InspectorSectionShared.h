#pragma once
#include "Editor/InspectorSections.h"

#include "Animation/SkinnedMeshRendererComponent.h"
#include "Animation/AnimatorComponent.h"
#include "Audio/AudioClipAsset.h"
#include "Audio/AudioSourceComponent.h"
#include "Audio/AudioListenerComponent.h"
#include "Assets/Asset.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetManager.h"
#include "Assets/MaterialAsset.h"
#include "Assets/ModelAsset.h"
#include "Assets/PrefabAsset.h"
#include "Assets/ScriptAsset.h"
#include "Assets/TextureAsset.h"
#include "Camera/CameraComponent.h"
#include "Camera/ThirdPersonCameraComponent.h"
#include "Gameplay/GameplayComponents.h"
#include "Gameplay/EnemyAIComponent.h"
#include "Game/SceneRenderLayer.h"
#include "Navigation/NavAgentComponent.h"
#include "Renderer/ParticleSystemComponent.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorAssetRegistry.h"
#include "Editor/EditorInspectorSection.h"
#include "Editor/EditorImportService.h"
#include "Editor/EditorNavigationBakeService.h"
#include "Editor/EditorOperators.h"
#include "Editor/EditorPanelHelpers.h"
#include "Editor/EditorResourceOperator.h"
#include "Editor/EditorWorkspace.h"
#include "Editor/UI/EditorIcons.h"
#include "Editor/UI/EditorWidgets.h"
#include "Physics/BoxColliderComponent.h"
#include "Physics/CapsuleColliderComponent.h"
#include "Physics/CharacterControllerComponent.h"
#include "Physics/ColliderComponent.h"
#include "Physics/PhysicsWorld.h"
#include "Physics/RigidBodyComponent.h"
#include "Physics/SphereColliderComponent.h"
#include "Renderer/LightComponent.h"
#include "Renderer/PostProcessComponent.h"
#include "Scene/Actor.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/PrefabSystem.h"
#include "Scene/MeshRendererComponent.h"
#include "Scene/Scene.h"
#include "Scripting/ScriptComponent.h"
#include "UI/Core/UICanvasComponent.h"
#include "UI/Core/UIComponents.h"

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#endif

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
using namespace EditorPanelHelpers;
namespace EditorIcons = Editor::UI::EditorIcons;
namespace EditorWidgets = Editor::UI::EditorWidgets;

constexpr const char kTexturePayload[] = "MYENGINE_TEXTURE_PATH";

const char* ScriptFieldTypeLabel(ScriptFieldType type) {
    switch (type) {
    case ScriptFieldType::Bool:
        return "bool";
    case ScriptFieldType::Int:
        return "int";
    case ScriptFieldType::UInt:
        return "uint";
    case ScriptFieldType::Float:
        return "float";
    case ScriptFieldType::Double:
        return "double";
    case ScriptFieldType::String:
        return "string";
    case ScriptFieldType::Vec2:
        return "Vec2";
    case ScriptFieldType::Vec3:
        return "Vec3";
    default:
        return "unsupported";
    }
}

Actor* SelectedActor(EditorContext& context) {
    Scene* scene = context.GetInspectorScene();
    return scene ? context.GetSelection().ResolveActor(*scene) : nullptr;
}

bool SectionHeaderWithIcon(EditorContext& context, const char* icon, const char* label, bool defaultOpen = true) {
    EditorWidgets::SvgIcon(context, icon, 14.0f);
    ImGui::SameLine();
    return EditorWidgets::SectionHeader(label, defaultOpen);
}

bool IsJsonAsset(const std::string& path) {
    std::string extension = std::filesystem::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension == ".json";
}

const char* AssetImportStateLabel(AssetImportState state) {
    switch (state) {
    case AssetImportState::Ready:
        return "Ready";
    case AssetImportState::Importing:
        return "Importing";
    case AssetImportState::Failed:
        return "Failed";
    case AssetImportState::Stale:
        return "Stale";
    case AssetImportState::MissingSource:
        return "Missing Source";
    default:
        return "Unknown";
    }
}

std::string LowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

bool ContainsCaseInsensitive(const std::string& value, const char* filter) {
    if (!filter || !filter[0])
        return true;
    return LowerCopy(value).find(LowerCopy(filter)) != std::string::npos;
}

std::string ComponentDisplayName(std::string type) {
    const std::string suffix = "Component";
    if (type.size() > suffix.size() && type.compare(type.size() - suffix.size(), suffix.size(), suffix) == 0) {
        type.resize(type.size() - suffix.size());
    }
    std::string result;
    for (size_t i = 0; i < type.size(); ++i) {
        const char ch = type[i];
        if (i > 0 && ch >= 'A' && ch <= 'Z' &&
            ((type[i - 1] >= 'a' && type[i - 1] <= 'z') ||
             (i + 1 < type.size() && type[i + 1] >= 'a' && type[i + 1] <= 'z'))) {
            result.push_back(' ');
        }
        result.push_back(ch);
    }
    return result.empty() ? type : result;
}

const char* ComponentCategory(const std::string& type) {
    if (type == "MeshRenderer" || type == "SkinnedMeshRenderer" || type == "Animator" || type == "Camera" ||
        type == "ThirdPersonCamera" || type == "Light" || type == "PostProcess") {
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

const char* EditorAssetTypeLabel(EditorAssetType type) {
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

std::filesystem::path AssetInspectorProjectRoot(const EditorContext& context) {
    if (!context.GetProjectRoot().empty())
        return context.GetProjectRoot();
    const std::filesystem::path& contentRoot = context.GetContentRoot();
    return contentRoot.empty() ? std::filesystem::path{} : contentRoot.parent_path();
}

bool ReadImportSettingsJson(EditorContext& context, const std::string& uuid, std::string& outSettings) {
    outSettings = "{}";
    if (uuid.empty())
        return false;
    const std::filesystem::path projectRoot = AssetInspectorProjectRoot(context);
    if (projectRoot.empty())
        return false;
    AssetDatabase database;
    if (!database.Open(projectRoot / ".myengine" / "AssetDatabase.json"))
        return false;
    const AssetRecord* record = database.FindByUuid(uuid);
    if (!record)
        return false;
    outSettings = record->settingsJson.empty() ? std::string("{}") : record->settingsJson;
    return true;
}

bool ParseImportSettingsJson(const std::string& text, nlohmann::json& out, std::string* error) {
    try {
        out = nlohmann::json::parse(text.empty() ? "{}" : text);
        if (!out.is_object()) {
            if (error)
                *error = "settings must be a JSON object";
            return false;
        }
        return true;
    } catch (const std::exception& exception) {
        if (error)
            *error = exception.what();
        return false;
    }
}

std::string PrettyImportSettingsJson(const std::string& text) {
    nlohmann::json value;
    return ParseImportSettingsJson(text, value, nullptr) ? value.dump(2) : text;
}

std::string ImportSettingsWithTextureSampler(EditorContext& context, const std::string& uuid, TextureFilter filter,
                                             TextureWrap wrapU, TextureWrap wrapV) {
    std::string current;
    nlohmann::json settings = nlohmann::json::object();
    if (ReadImportSettingsJson(context, uuid, current)) {
        std::string error;
        if (!ParseImportSettingsJson(current, settings, &error)) {
            settings = nlohmann::json::object();
        }
    }

    const auto filterText = filter == TextureFilter::Nearest ? std::string("nearest") : std::string("linear");
    const auto wrapText = [](TextureWrap wrap) {
        return wrap == TextureWrap::Clamp ? std::string("clamp") : std::string("repeat");
    };
    settings["textureSampler"] = {{"filter", filterText}, {"wrapU", wrapText(wrapU)}, {"wrapV", wrapText(wrapV)}};
    return settings.dump();
}

std::unordered_map<std::string, std::string> g_ImportSettingsEditBuffers;

void DrawImportSettingsEditor(EditorContext& context, const EditorAssetInfo& info) {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!info.imported || info.uuid.empty())
        return;

    std::string currentSettings;
    if (!ReadImportSettingsJson(context, info.uuid, currentSettings)) {
        ImGui::TextDisabled("Import settings unavailable");
        return;
    }

    auto bufferIt = g_ImportSettingsEditBuffers.find(info.uuid);
    if (bufferIt == g_ImportSettingsEditBuffers.end()) {
        bufferIt = g_ImportSettingsEditBuffers.emplace(info.uuid, PrettyImportSettingsJson(currentSettings)).first;
    }
    std::string& editBuffer = bufferIt->second;

    if (!ImGui::TreeNodeEx("Import Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    constexpr size_t kSettingsBufferSize = 8192;
    std::array<char, kSettingsBufferSize> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%s", editBuffer.c_str());
    if (ImGui::InputTextMultiline("Settings JSON", buffer.data(), buffer.size(),
                                  ImVec2(0.0f, ImGui::GetTextLineHeight() * 8.0f), ImGuiInputTextFlags_AllowTabInput)) {
        editBuffer = buffer.data();
    }

    nlohmann::json parsed;
    std::string parseError;
    const bool valid = ParseImportSettingsJson(editBuffer, parsed, &parseError);
    if (!valid) {
        ImGui::TextColored({1.0f, 0.35f, 0.25f, 1.0f}, "Invalid settings JSON: %s", parseError.c_str());
    }

    ImGui::BeginDisabled(!valid || parsed.dump() == currentSettings);
    if (ImGui::Button("Apply Settings")) {
        if (auto* operators = context.GetOperators()) {
            if (operators->Assets().ReimportWithSettings(context, info.uuid, parsed.dump())) {
                editBuffer = parsed.dump(2);
            }
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Reset Settings")) {
        if (auto* operators = context.GetOperators()) {
            if (operators->Assets().ReimportWithSettings(context, info.uuid, "{}")) {
                editBuffer = "{}";
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload Settings")) {
        editBuffer = PrettyImportSettingsJson(currentSettings);
    }

    ImGui::TreePop();
#else
    (void)context;
    (void)info;
#endif
}

const EditorAssetInfo* SelectedAssetInfo(EditorContext& context, const std::string& path) {
    const EditorAssetRegistry* registry = context.GetAssetRegistry();
    return registry ? registry->GetAssetInfo(path) : nullptr;
}

std::string AssetRecordDisplayPath(const AssetRecord& record) {
    if (!record.sourcePath.empty())
        return record.sourcePath;
    if (!record.artifactPath.empty())
        return record.artifactPath;
    return record.uuid;
}

const char* AssetValidationIssueLabel(AssetDatabaseValidationIssueCode code) {
    switch (code) {
    case AssetDatabaseValidationIssueCode::DuplicateUuid:
        return "Duplicate UUID";
    case AssetDatabaseValidationIssueCode::DuplicateSourcePath:
        return "Duplicate Source";
    case AssetDatabaseValidationIssueCode::MissingSource:
        return "Missing Source";
    case AssetDatabaseValidationIssueCode::MissingArtifact:
        return "Missing Artifact";
    case AssetDatabaseValidationIssueCode::ArtifactHashMismatch:
        return "Artifact Hash";
    case AssetDatabaseValidationIssueCode::UnknownDependency:
        return "Unknown Dependency";
    case AssetDatabaseValidationIssueCode::DependencyCycle:
        return "Dependency Cycle";
    case AssetDatabaseValidationIssueCode::StateNotReady:
        return "Import State";
    case AssetDatabaseValidationIssueCode::IncompleteRecord:
        return "Incomplete Record";
    default:
        return "Validation";
    }
}

bool AssetPathMatchesIssue(const AssetRecord& record, const AssetDatabaseValidationIssue& issue) {
    if (!issue.uuid.empty() && issue.uuid == record.uuid)
        return true;
    if (issue.path.empty())
        return false;
    const std::string issuePath = std::filesystem::path(issue.path).lexically_normal().generic_string();
    const auto matches = [&](const std::string& path) {
        return !path.empty() && std::filesystem::path(path).lexically_normal().generic_string() == issuePath;
    };
    return matches(record.sourcePath) || matches(record.artifactPath);
}

void SelectAssetRecord(EditorContext& context, const AssetRecord& record) {
    const std::string path = AssetRecordDisplayPath(record);
    if (path.empty())
        return;
    if (auto* operators = context.GetOperators()) {
        operators->Selection().SelectAsset(context, path);
    } else {
        context.GetSelection().SelectAssetPath(path);
    }
}

void DrawAssetRecordRow(EditorContext& context, const AssetRecord& record, const char* suffix) {
#if defined(MYENGINE_ENABLE_IMGUI)
    const std::string path = AssetRecordDisplayPath(record);
    const std::string name = path.empty() ? record.uuid : std::filesystem::path(path).filename().string();
    ImGui::PushID((std::string(suffix) + record.uuid + path).c_str());
    if (ImGui::SmallButton("Select")) {
        SelectAssetRecord(context, record);
    }
    ImGui::SameLine();
    ImGui::TextWrapped("%s", name.empty() ? "(unnamed asset)" : name.c_str());
    if (!record.type.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("[%s]", record.type.c_str());
    }
    if (!path.empty() && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", path.c_str());
    }
    if (!record.uuid.empty()) {
        ImGui::TextDisabled("UUID: %s", record.uuid.c_str());
    }
    ImGui::PopID();
#else
    (void)context;
    (void)record;
    (void)suffix;
#endif
}

void DrawAssetRecordList(EditorContext& context, const char* label, const std::vector<const AssetRecord*>& records,
                         const char* suffix) {
#if defined(MYENGINE_ENABLE_IMGUI)
    const std::string header = std::string(label) + " (" + std::to_string(records.size()) + ")";
    if (!ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    if (records.empty()) {
        ImGui::TextDisabled("None");
    } else {
        for (const AssetRecord* record : records) {
            if (record)
                DrawAssetRecordRow(context, *record, suffix);
        }
    }
    ImGui::TreePop();
#else
    (void)context;
    (void)label;
    (void)records;
    (void)suffix;
#endif
}

void DrawUnresolvedDependencyList(const AssetDatabase& database, const AssetRecord& record) {
#if defined(MYENGINE_ENABLE_IMGUI)
    std::vector<std::string> unresolved;
    for (const std::string& uuid : record.dependencies) {
        if (!uuid.empty() && !database.FindByUuid(uuid))
            unresolved.push_back(uuid);
    }
    if (unresolved.empty())
        return;
    const std::string header = "Unresolved Dependencies (" + std::to_string(unresolved.size()) + ")";
    if (!ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    for (const std::string& uuid : unresolved) {
        ImGui::TextWrapped("Missing dependency UUID: %s", uuid.c_str());
    }
    ImGui::TreePop();
#else
    (void)database;
    (void)record;
#endif
}

void DrawAssetValidationIssueList(const std::vector<AssetDatabaseValidationIssue>& issues) {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (issues.empty())
        return;
    const std::string header = "Validation Issues (" + std::to_string(issues.size()) + ")";
    if (!ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    for (const AssetDatabaseValidationIssue& issue : issues) {
        ImGui::TextWrapped("%s: %s", AssetValidationIssueLabel(issue.code), issue.message.c_str());
        if (!issue.path.empty()) {
            ImGui::TextDisabled("%s", issue.path.c_str());
        }
    }
    ImGui::TreePop();
#else
    (void)issues;
#endif
}

void DrawAssetSceneReferenceRows(EditorContext& context,
                                 const std::vector<EditorAssetOperator::SceneReferenceInfo>& references,
                                 bool allowSelectCurrentSceneActor) {
#if defined(MYENGINE_ENABLE_IMGUI)
    auto* operators = context.GetOperators();
    for (const auto& reference : references) {
        ImGui::PushID(reference.scenePath.c_str());
        ImGui::PushID(static_cast<int>(reference.actorID));
        ImGui::PushID(reference.componentType.c_str());
        ImGui::PushID(reference.jsonPath.c_str());
        if (allowSelectCurrentSceneActor && operators) {
            if (ImGui::SmallButton("Select")) {
                operators->Selection().SelectActor(context, reference.actorID);
                context.RequestPanelFocus("sceneHierarchy");
            }
            ImGui::SameLine();
        }
        ImGui::TextWrapped("%s", reference.actorName.empty() ? "(unnamed actor)" : reference.actorName.c_str());
        if (!reference.scenePath.empty()) {
            ImGui::TextDisabled("%s", reference.scenePath.c_str());
        }
        ImGui::TextDisabled("%s %s", reference.componentType.c_str(), reference.jsonPath.c_str());
        if (!reference.valuePreview.empty()) {
            ImGui::TextWrapped("%s", reference.valuePreview.c_str());
        }
        ImGui::PopID();
        ImGui::PopID();
        ImGui::PopID();
        ImGui::PopID();
    }
#else
    (void)context;
    (void)references;
    (void)allowSelectCurrentSceneActor;
#endif
}

void DrawAssetSceneReferenceList(EditorContext& context, const std::string& path) {
#if defined(MYENGINE_ENABLE_IMGUI)
    auto* operators = context.GetOperators();
    if (!operators)
        return;
    const auto references = operators->Assets().FindSceneReferences(context, path);
    const std::string header = "Scene References (" + std::to_string(references.size()) + ")";
    if (!ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    if (references.empty()) {
        ImGui::TextDisabled("No current scene references");
        ImGui::TreePop();
        return;
    }
    DrawAssetSceneReferenceRows(context, references, true);
    ImGui::TreePop();
#else
    (void)context;
    (void)path;
#endif
}

void DrawAssetProjectSceneReferenceList(EditorContext& context, const std::string& path) {
#if defined(MYENGINE_ENABLE_IMGUI)
    auto* operators = context.GetOperators();
    if (!operators)
        return;
    const auto references = operators->Assets().FindProjectSceneReferences(context, path);
    const std::string header = "Project Scene References (" + std::to_string(references.size()) + ")";
    if (!ImGui::TreeNodeEx(header.c_str())) {
        return;
    }
    if (references.empty()) {
        ImGui::TextDisabled("No project scene references");
        ImGui::TreePop();
        return;
    }
    DrawAssetSceneReferenceRows(context, references, false);
    ImGui::TreePop();
#else
    (void)context;
    (void)path;
#endif
}

void DrawAssetDependencySection(EditorContext& context, const EditorAssetInfo& info) {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (info.uuid.empty())
        return;
    const std::filesystem::path projectRoot = AssetInspectorProjectRoot(context);
    if (projectRoot.empty())
        return;

    AssetDatabase database;
    std::string error;
    if (!database.Open(projectRoot / ".myengine" / "AssetDatabase.json", &error)) {
        ImGui::TextDisabled("Asset dependencies unavailable: %s", error.c_str());
        return;
    }

    const AssetRecord* record = database.FindByUuid(info.uuid);
    if (!record) {
        ImGui::TextDisabled("Asset dependency record unavailable");
        return;
    }

    const auto dependencies = database.GetDependencies(info.uuid);
    const auto referencers = database.GetReferencers(info.uuid);
    AssetDatabaseValidationReport validationReport;
    database.ValidateAgainstProject(projectRoot, validationReport);
    std::vector<AssetDatabaseValidationIssue> currentIssues;
    for (const AssetDatabaseValidationIssue& issue : validationReport.issues) {
        if (AssetPathMatchesIssue(*record, issue))
            currentIssues.push_back(issue);
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Dependencies");
    DrawAssetRecordList(context, "Dependencies", dependencies, "dependency");
    DrawUnresolvedDependencyList(database, *record);
    DrawAssetRecordList(context, "Referencers", referencers, "referencer");
    DrawAssetSceneReferenceList(context, info.absolutePath.string());
    DrawAssetProjectSceneReferenceList(context, info.absolutePath.string());
    DrawAssetValidationIssueList(currentIssues);
#else
    (void)context;
    (void)info;
#endif
}

void DrawAssetMetadataHeader(EditorContext& context, const std::string& path, const char* fallbackTypeLabel) {
#if defined(MYENGINE_ENABLE_IMGUI)
    const EditorAssetInfo* info = SelectedAssetInfo(context, path);
    ImGui::Separator();
    ImGui::Text("Asset: %s", std::filesystem::path(path).filename().string().c_str());
    ImGui::TextWrapped("Path: %s", path.c_str());
    ImGui::Text("Type: %s", info ? EditorAssetTypeLabel(info->type) : fallbackTypeLabel);
    if (!info) {
        ImGui::TextDisabled("Registry metadata unavailable");
        return;
    }

    if (!info->uuid.empty())
        ImGui::Text("UUID: %s", info->uuid.c_str());
    ImGui::Text("Import State: %s", AssetImportStateLabel(info->importState));
    if (!info->artifactPath.empty()) {
        ImGui::TextWrapped("Artifact: %s", info->artifactPath.string().c_str());
    }
    if (info->imported && !info->uuid.empty()) {
        if (ImGui::Button("Reimport")) {
            if (auto* operators = context.GetOperators()) {
                operators->Assets().Reimport(context, info->uuid);
            }
        }
    }
    DrawImportSettingsEditor(context, *info);
    DrawAssetDependencySection(context, *info);
    if (!info->diagnostics.empty()) {
        ImGui::Separator();
        ImGui::TextUnformatted("Diagnostics");
        for (const AssetDiagnostic& diagnostic : info->diagnostics) {
            ImGui::TextWrapped("%s: %s", diagnostic.severity.c_str(), diagnostic.message.c_str());
        }
    }
#else
    (void)context;
    (void)path;
    (void)fallbackTypeLabel;
#endif
}

template <typename T, typename Fn>
void CommitComponentEdit(EditorContext& context, Actor& actor, T& component, const char* propertyName, Fn&& edit) {
    nlohmann::json before = nlohmann::json::object();
    component.Serialize(before);
    edit();
    nlohmann::json after = nlohmann::json::object();
    component.Serialize(after);
    if (before == after)
        return;
    component.Deserialize(before);
    if (auto* operators = context.GetOperators()) {
        operators->Components().SetProperty(context, actor, component.GetTypeName(), propertyName, before, after);
    } else {
        EditorComponentOperator componentOperator;
        componentOperator.SetProperty(context, actor, component.GetTypeName(), propertyName, before, after);
    }
}

bool CommitSceneNameEdit(EditorContext& context, const std::string& beforeName, const std::string& afterName) {
    if (beforeName == afterName)
        return false;
    if (auto* operators = context.GetOperators()) {
        return operators->Commands().SetSceneName(context, afterName);
    }
    EditorCommandOperator commandOperator;
    return commandOperator.SetSceneName(context, afterName);
}

bool CommitSceneGravityEdit(EditorContext& context, const Vec3& beforeGravity, const Vec3& afterGravity) {
    if (beforeGravity.x == afterGravity.x && beforeGravity.y == afterGravity.y && beforeGravity.z == afterGravity.z) {
        return false;
    }
    if (auto* operators = context.GetOperators()) {
        return operators->Commands().SetSceneGravity(context, afterGravity);
    }
    EditorCommandOperator commandOperator;
    return commandOperator.SetSceneGravity(context, afterGravity);
}

bool CommitSceneMainCameraHintEdit(EditorContext& context, uint64_t beforeActorID, uint64_t afterActorID) {
    if (beforeActorID == afterActorID)
        return false;
    if (auto* operators = context.GetOperators()) {
        return operators->Commands().SetSceneMainCameraHint(context, afterActorID);
    }
    EditorCommandOperator commandOperator;
    return commandOperator.SetSceneMainCameraHint(context, afterActorID);
}

bool CommitSceneAmbientIntensityEdit(EditorContext& context, float beforeIntensity, float afterIntensity) {
    if (afterIntensity < 0.0f)
        afterIntensity = 0.0f;
    if (beforeIntensity == afterIntensity)
        return false;
    if (auto* operators = context.GetOperators()) {
        return operators->Commands().SetSceneAmbientIntensity(context, afterIntensity);
    }
    EditorCommandOperator commandOperator;
    return commandOperator.SetSceneAmbientIntensity(context, afterIntensity);
}

template <typename Fn>
bool ModifyMaterialAssetField(EditorContext& context, const std::string& path, const char* label, Fn&& edit) {
    MaterialModifier modifier(path, label, [fn = std::forward<Fn>(edit)](MaterialAsset& target) mutable {
        fn(target);
        return true;
    });
    return modifier.Modify(context);
}

bool RemoveComponentByType(EditorContext& context, Actor& actor, const char* typeName) {
    if (!typeName || !actor.HasComponentType(typeName))
        return false;
    if (auto* operators = context.GetOperators()) {
        return operators->Components().RemoveComponent(context, actor.GetID(), typeName);
    }
    EditorComponentOperator componentOperator;
    return componentOperator.RemoveComponent(context, actor.GetID(), typeName);
}

bool AddComponentByType(EditorContext& context, Actor& actor, const std::string& typeName,
                        const nlohmann::json& initialData) {
    if (typeName.empty())
        return false;
    if (auto* operators = context.GetOperators()) {
        return operators->Components().AddComponent(context, actor.GetID(), typeName, initialData);
    }
    EditorComponentOperator componentOperator;
    return componentOperator.AddComponent(context, actor.GetID(), typeName, initialData);
}

bool DrawStringField(const char* label, std::string& value) {
    std::array<char, 260> buffer{};
    std::strncpy(buffer.data(), value.c_str(), buffer.size() - 1);
    if (!ImGui::InputText(label, buffer.data(), buffer.size()))
        return false;
    value = buffer.data();
    return true;
}

bool DrawVec2Field(const char* label, Vec2& value) {
    float data[2] = {value.x, value.y};
    if (!ImGui::DragFloat2(label, data, 1.0f))
        return false;
    value = {data[0], data[1]};
    return true;
}

bool DrawColorField(const char* label, Color& value) {
    float data[4] = {value.r, value.g, value.b, value.a};
    if (!ImGui::ColorEdit4(label, data))
        return false;
    value = {data[0], data[1], data[2], data[3]};
    return true;
}

void DrawJsonAssetPreview(const std::string& path) {
#if defined(MYENGINE_ENABLE_IMGUI)
    try {
        std::ifstream input(path);
        if (!input) {
            ImGui::TextDisabled("Unable to open JSON file");
            return;
        }

        nlohmann::json document;
        input >> document;
        std::string preview = document.dump(2);
        constexpr size_t maxPreviewBytes = 64 * 1024;
        if (preview.size() > maxPreviewBytes) {
            preview.resize(maxPreviewBytes);
            preview += "\n... preview truncated ...";
        }

        ImGui::Separator();
        ImGui::TextUnformatted("JSON Preview");
        ImGui::BeginChild("##JsonAssetPreview", ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(preview.c_str());
        ImGui::EndChild();
    } catch (const std::exception& exception) {
        ImGui::TextDisabled("Invalid JSON: %s", exception.what());
    }
#else
    (void)path;
#endif
}

bool DrawScriptFieldValue(const ScriptFieldInfo& field, const nlohmann::json& current, nlohmann::json& outValue) {
#if defined(MYENGINE_ENABLE_IMGUI)
    switch (field.type) {
    case ScriptFieldType::Bool: {
        bool value = current.is_boolean() ? current.get<bool>() : false;
        if (ImGui::Checkbox(field.name.c_str(), &value)) {
            outValue = value;
            return true;
        }
        return false;
    }
    case ScriptFieldType::Int: {
        int value = current.is_number_integer() ? current.get<int>() : 0;
        if (ImGui::DragInt(field.name.c_str(), &value, 1.0f)) {
            outValue = value;
            return true;
        }
        return false;
    }
    case ScriptFieldType::UInt: {
        int value = current.is_number_integer() ? static_cast<int>(current.get<unsigned int>()) : 0;
        if (ImGui::DragInt(field.name.c_str(), &value, 1.0f, 0)) {
            outValue = static_cast<unsigned int>(std::max(0, value));
            return true;
        }
        return false;
    }
    case ScriptFieldType::Float: {
        float value = current.is_number() ? current.get<float>() : 0.0f;
        if (ImGui::DragFloat(field.name.c_str(), &value, 0.05f)) {
            outValue = value;
            return true;
        }
        return false;
    }
    case ScriptFieldType::Double: {
        float value = current.is_number() ? static_cast<float>(current.get<double>()) : 0.0f;
        if (ImGui::DragFloat(field.name.c_str(), &value, 0.05f)) {
            outValue = static_cast<double>(value);
            return true;
        }
        return false;
    }
    case ScriptFieldType::String: {
        std::array<char, 256> buffer{};
        if (current.is_string()) {
            std::strncpy(buffer.data(), current.get<std::string>().c_str(), buffer.size() - 1);
        }
        if (ImGui::InputText(field.name.c_str(), buffer.data(), buffer.size())) {
            outValue = std::string(buffer.data());
            return true;
        }
        return false;
    }
    case ScriptFieldType::Vec2: {
        float value[2] = {};
        if (current.is_array() && current.size() >= 2) {
            value[0] = current[0].get<float>();
            value[1] = current[1].get<float>();
        }
        if (ImGui::DragFloat2(field.name.c_str(), value, 0.05f)) {
            outValue = nlohmann::json::array({value[0], value[1]});
            return true;
        }
        return false;
    }
    case ScriptFieldType::Vec3: {
        float value[3] = {};
        if (current.is_array() && current.size() >= 3) {
            value[0] = current[0].get<float>();
            value[1] = current[1].get<float>();
            value[2] = current[2].get<float>();
        }
        if (ImGui::DragFloat3(field.name.c_str(), value, 0.05f)) {
            outValue = nlohmann::json::array({value[0], value[1], value[2]});
            return true;
        }
        return false;
    }
    default:
        ImGui::TextDisabled("%s (%s)", field.name.c_str(), ScriptFieldTypeLabel(field.type));
        return false;
    }
#else
    (void)field;
    (void)current;
    (void)outValue;
    return false;
#endif
}

class ActorInspectorSection : public EditorInspectorSection {
public:
    bool CanDraw(const EditorSelectObject& object, const EditorContext&) const override { return object.IsActor(); }
};

// ---------------------------------------------------------------------------
// Scene Settings Inspector (P1-A.2)
// ---------------------------------------------------------------------------
} // namespace
