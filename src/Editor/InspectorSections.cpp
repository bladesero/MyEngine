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

const char* ScriptFieldTypeLabel(ScriptFieldType type)
{
    switch (type) {
        case ScriptFieldType::Bool: return "bool";
        case ScriptFieldType::Int: return "int";
        case ScriptFieldType::UInt: return "uint";
        case ScriptFieldType::Float: return "float";
        case ScriptFieldType::Double: return "double";
        case ScriptFieldType::String: return "string";
        case ScriptFieldType::Vec2: return "Vec2";
        case ScriptFieldType::Vec3: return "Vec3";
        default: return "unsupported";
    }
}

Actor* SelectedActor(EditorContext& context)
{
    Scene* scene = context.GetInspectorScene();
    return scene ? context.GetSelection().ResolveActor(*scene) : nullptr;
}

bool SectionHeaderWithIcon(EditorContext& context, const char* icon,
                           const char* label, bool defaultOpen = true)
{
    EditorWidgets::SvgIcon(context, icon, 14.0f);
    ImGui::SameLine();
    return EditorWidgets::SectionHeader(label, defaultOpen);
}

bool IsJsonAsset(const std::string& path)
{
    std::string extension = std::filesystem::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension == ".json";
}

const char* AssetImportStateLabel(AssetImportState state)
{
    switch (state) {
        case AssetImportState::Ready: return "Ready";
        case AssetImportState::Importing: return "Importing";
        case AssetImportState::Failed: return "Failed";
        case AssetImportState::Stale: return "Stale";
        case AssetImportState::MissingSource: return "Missing Source";
        default: return "Unknown";
    }
}

std::string LowerCopy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

bool ContainsCaseInsensitive(const std::string& value, const char* filter)
{
    if (!filter || !filter[0]) return true;
    return LowerCopy(value).find(LowerCopy(filter)) != std::string::npos;
}

std::string ComponentDisplayName(std::string type)
{
    const std::string suffix = "Component";
    if (type.size() > suffix.size() &&
        type.compare(type.size() - suffix.size(), suffix.size(), suffix) == 0) {
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

const char* ComponentCategory(const std::string& type)
{
    if (type == "MeshRenderer" || type == "SkinnedMeshRenderer" || type == "Animator" ||
        type == "Camera" || type == "ThirdPersonCamera" || type == "Light" || type == "PostProcess") {
        return "Rendering";
    }
    if (type == "AudioSource") return "Audio";
    if (type == "RigidBody" || type == "BoxCollider" ||
        type == "SphereCollider" || type == "CapsuleCollider" ||
        type == "CharacterController") {
        return "Physics";
    }
    if (type == "Script") return "Scripting";
    if (type.rfind("UI", 0) == 0) return "UI";
    return "Gameplay";
}

bool ComponentMatchesFilter(const std::string& type, const char* filter)
{
    return ContainsCaseInsensitive(type, filter) ||
        ContainsCaseInsensitive(ComponentDisplayName(type), filter) ||
        ContainsCaseInsensitive(ComponentCategory(type), filter);
}

const char* EditorAssetTypeLabel(EditorAssetType type)
{
    switch (type) {
        case EditorAssetType::Model: return "Model";
        case EditorAssetType::Texture: return "Texture";
        case EditorAssetType::Material: return "Material";
        case EditorAssetType::Scene: return "Scene";
        case EditorAssetType::Prefab: return "Prefab";
        case EditorAssetType::Script: return "Script";
        case EditorAssetType::Shader: return "Shader";
        case EditorAssetType::Audio: return "Audio";
        case EditorAssetType::UI: return "UI";
        case EditorAssetType::Particle: return "Particle";
        case EditorAssetType::Navigation: return "Navigation";
        default: return "Unknown";
    }
}

std::filesystem::path AssetInspectorProjectRoot(const EditorContext& context)
{
    if (!context.GetProjectRoot().empty()) return context.GetProjectRoot();
    const std::filesystem::path& contentRoot = context.GetContentRoot();
    return contentRoot.empty() ? std::filesystem::path{} : contentRoot.parent_path();
}

bool ReadImportSettingsJson(EditorContext& context, const std::string& uuid,
                            std::string& outSettings)
{
    outSettings = "{}";
    if (uuid.empty()) return false;
    const std::filesystem::path projectRoot = AssetInspectorProjectRoot(context);
    if (projectRoot.empty()) return false;
    AssetDatabase database;
    if (!database.Open(projectRoot / ".myengine" / "AssetDatabase.json")) return false;
    const AssetRecord* record = database.FindByUuid(uuid);
    if (!record) return false;
    outSettings = record->settingsJson.empty() ? std::string("{}") : record->settingsJson;
    return true;
}

bool ParseImportSettingsJson(const std::string& text, nlohmann::json& out,
                             std::string* error)
{
    try {
        out = nlohmann::json::parse(text.empty() ? "{}" : text);
        if (!out.is_object()) {
            if (error) *error = "settings must be a JSON object";
            return false;
        }
        return true;
    } catch (const std::exception& exception) {
        if (error) *error = exception.what();
        return false;
    }
}

std::string PrettyImportSettingsJson(const std::string& text)
{
    nlohmann::json value;
    return ParseImportSettingsJson(text, value, nullptr) ? value.dump(2) : text;
}

std::string ImportSettingsWithTextureSampler(
    EditorContext& context, const std::string& uuid,
    TextureFilter filter, TextureWrap wrapU, TextureWrap wrapV)
{
    std::string current;
    nlohmann::json settings = nlohmann::json::object();
    if (ReadImportSettingsJson(context, uuid, current)) {
        std::string error;
        if (!ParseImportSettingsJson(current, settings, &error)) {
            settings = nlohmann::json::object();
        }
    }

    const auto filterText = filter == TextureFilter::Nearest
        ? std::string("nearest") : std::string("linear");
    const auto wrapText = [](TextureWrap wrap) {
        return wrap == TextureWrap::Clamp ? std::string("clamp") : std::string("repeat");
    };
    settings["textureSampler"] = {
        {"filter", filterText},
        {"wrapU", wrapText(wrapU)},
        {"wrapV", wrapText(wrapV)}
    };
    return settings.dump();
}

std::unordered_map<std::string, std::string> g_ImportSettingsEditBuffers;

void DrawImportSettingsEditor(EditorContext& context, const EditorAssetInfo& info)
{
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!info.imported || info.uuid.empty()) return;

    std::string currentSettings;
    if (!ReadImportSettingsJson(context, info.uuid, currentSettings)) {
        ImGui::TextDisabled("Import settings unavailable");
        return;
    }

    auto bufferIt = g_ImportSettingsEditBuffers.find(info.uuid);
    if (bufferIt == g_ImportSettingsEditBuffers.end()) {
        bufferIt = g_ImportSettingsEditBuffers
            .emplace(info.uuid, PrettyImportSettingsJson(currentSettings))
            .first;
    }
    std::string& editBuffer = bufferIt->second;

    if (!ImGui::TreeNodeEx("Import Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    constexpr size_t kSettingsBufferSize = 8192;
    std::array<char, kSettingsBufferSize> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%s", editBuffer.c_str());
    if (ImGui::InputTextMultiline(
            "Settings JSON", buffer.data(), buffer.size(),
            ImVec2(0.0f, ImGui::GetTextLineHeight() * 8.0f),
            ImGuiInputTextFlags_AllowTabInput)) {
        editBuffer = buffer.data();
    }

    nlohmann::json parsed;
    std::string parseError;
    const bool valid = ParseImportSettingsJson(editBuffer, parsed, &parseError);
    if (!valid) {
        ImGui::TextColored({1.0f, 0.35f, 0.25f, 1.0f},
                           "Invalid settings JSON: %s", parseError.c_str());
    }

    ImGui::BeginDisabled(!valid || parsed.dump() == currentSettings);
    if (ImGui::Button("Apply Settings")) {
        if (auto* operators = context.GetOperators()) {
            if (operators->Assets().ReimportWithSettings(
                    context, info.uuid, parsed.dump())) {
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

const EditorAssetInfo* SelectedAssetInfo(EditorContext& context,
                                         const std::string& path)
{
    const EditorAssetRegistry* registry = context.GetAssetRegistry();
    return registry ? registry->GetAssetInfo(path) : nullptr;
}

std::string AssetRecordDisplayPath(const AssetRecord& record)
{
    if (!record.sourcePath.empty()) return record.sourcePath;
    if (!record.artifactPath.empty()) return record.artifactPath;
    return record.uuid;
}

const char* AssetValidationIssueLabel(AssetDatabaseValidationIssueCode code)
{
    switch (code) {
        case AssetDatabaseValidationIssueCode::DuplicateUuid: return "Duplicate UUID";
        case AssetDatabaseValidationIssueCode::DuplicateSourcePath: return "Duplicate Source";
        case AssetDatabaseValidationIssueCode::MissingSource: return "Missing Source";
        case AssetDatabaseValidationIssueCode::MissingArtifact: return "Missing Artifact";
        case AssetDatabaseValidationIssueCode::ArtifactHashMismatch: return "Artifact Hash";
        case AssetDatabaseValidationIssueCode::UnknownDependency: return "Unknown Dependency";
        case AssetDatabaseValidationIssueCode::DependencyCycle: return "Dependency Cycle";
        case AssetDatabaseValidationIssueCode::StateNotReady: return "Import State";
        case AssetDatabaseValidationIssueCode::IncompleteRecord: return "Incomplete Record";
        default: return "Validation";
    }
}

bool AssetPathMatchesIssue(const AssetRecord& record,
                           const AssetDatabaseValidationIssue& issue)
{
    if (!issue.uuid.empty() && issue.uuid == record.uuid) return true;
    if (issue.path.empty()) return false;
    const std::string issuePath =
        std::filesystem::path(issue.path).lexically_normal().generic_string();
    const auto matches = [&](const std::string& path) {
        return !path.empty() &&
            std::filesystem::path(path).lexically_normal().generic_string() == issuePath;
    };
    return matches(record.sourcePath) || matches(record.artifactPath);
}

void SelectAssetRecord(EditorContext& context, const AssetRecord& record)
{
    const std::string path = AssetRecordDisplayPath(record);
    if (path.empty()) return;
    if (auto* operators = context.GetOperators()) {
        operators->Selection().SelectAsset(context, path);
    } else {
        context.GetSelection().SelectAssetPath(path);
    }
}

void DrawAssetRecordRow(EditorContext& context, const AssetRecord& record,
                        const char* suffix)
{
#if defined(MYENGINE_ENABLE_IMGUI)
    const std::string path = AssetRecordDisplayPath(record);
    const std::string name = path.empty()
        ? record.uuid
        : std::filesystem::path(path).filename().string();
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

void DrawAssetRecordList(EditorContext& context, const char* label,
                         const std::vector<const AssetRecord*>& records,
                         const char* suffix)
{
#if defined(MYENGINE_ENABLE_IMGUI)
    const std::string header = std::string(label) + " (" +
        std::to_string(records.size()) + ")";
    if (!ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    if (records.empty()) {
        ImGui::TextDisabled("None");
    } else {
        for (const AssetRecord* record : records) {
            if (record) DrawAssetRecordRow(context, *record, suffix);
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

void DrawUnresolvedDependencyList(const AssetDatabase& database,
                                  const AssetRecord& record)
{
#if defined(MYENGINE_ENABLE_IMGUI)
    std::vector<std::string> unresolved;
    for (const std::string& uuid : record.dependencies) {
        if (!uuid.empty() && !database.FindByUuid(uuid)) unresolved.push_back(uuid);
    }
    if (unresolved.empty()) return;
    const std::string header = "Unresolved Dependencies (" +
        std::to_string(unresolved.size()) + ")";
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

void DrawAssetValidationIssueList(const std::vector<AssetDatabaseValidationIssue>& issues)
{
#if defined(MYENGINE_ENABLE_IMGUI)
    if (issues.empty()) return;
    const std::string header = "Validation Issues (" +
        std::to_string(issues.size()) + ")";
    if (!ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    for (const AssetDatabaseValidationIssue& issue : issues) {
        ImGui::TextWrapped("%s: %s",
            AssetValidationIssueLabel(issue.code), issue.message.c_str());
        if (!issue.path.empty()) {
            ImGui::TextDisabled("%s", issue.path.c_str());
        }
    }
    ImGui::TreePop();
#else
    (void)issues;
#endif
}

void DrawAssetSceneReferenceRows(
    EditorContext& context,
    const std::vector<EditorAssetOperator::SceneReferenceInfo>& references,
    bool allowSelectCurrentSceneActor)
{
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
        ImGui::TextWrapped("%s", reference.actorName.empty()
            ? "(unnamed actor)" : reference.actorName.c_str());
        if (!reference.scenePath.empty()) {
            ImGui::TextDisabled("%s", reference.scenePath.c_str());
        }
        ImGui::TextDisabled("%s %s", reference.componentType.c_str(),
                            reference.jsonPath.c_str());
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

void DrawAssetSceneReferenceList(EditorContext& context, const std::string& path)
{
#if defined(MYENGINE_ENABLE_IMGUI)
    auto* operators = context.GetOperators();
    if (!operators) return;
    const auto references = operators->Assets().FindSceneReferences(context, path);
    const std::string header = "Scene References (" +
        std::to_string(references.size()) + ")";
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

void DrawAssetProjectSceneReferenceList(EditorContext& context, const std::string& path)
{
#if defined(MYENGINE_ENABLE_IMGUI)
    auto* operators = context.GetOperators();
    if (!operators) return;
    const auto references =
        operators->Assets().FindProjectSceneReferences(context, path);
    const std::string header = "Project Scene References (" +
        std::to_string(references.size()) + ")";
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

void DrawAssetDependencySection(EditorContext& context, const EditorAssetInfo& info)
{
#if defined(MYENGINE_ENABLE_IMGUI)
    if (info.uuid.empty()) return;
    const std::filesystem::path projectRoot = AssetInspectorProjectRoot(context);
    if (projectRoot.empty()) return;

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
        if (AssetPathMatchesIssue(*record, issue)) currentIssues.push_back(issue);
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

void DrawAssetMetadataHeader(EditorContext& context, const std::string& path,
                             const char* fallbackTypeLabel)
{
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

    if (!info->uuid.empty()) ImGui::Text("UUID: %s", info->uuid.c_str());
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
            ImGui::TextWrapped("%s: %s",
                diagnostic.severity.c_str(), diagnostic.message.c_str());
        }
    }
#else
    (void)context;
    (void)path;
    (void)fallbackTypeLabel;
#endif
}

template <typename T, typename Fn>
void CommitComponentEdit(EditorContext& context, Actor& actor, T& component,
                         const char* propertyName, Fn&& edit)
{
    nlohmann::json before = nlohmann::json::object();
    component.Serialize(before);
    edit();
    nlohmann::json after = nlohmann::json::object();
    component.Serialize(after);
    if (before == after) return;
    component.Deserialize(before);
    if (auto* operators = context.GetOperators()) {
        operators->Components().SetProperty(
            context, actor, component.GetTypeName(), propertyName, before, after);
    } else {
        EditorComponentOperator componentOperator;
        componentOperator.SetProperty(
            context, actor, component.GetTypeName(), propertyName, before, after);
    }
}

bool CommitSceneNameEdit(EditorContext& context, const std::string& beforeName,
                         const std::string& afterName)
{
    if (beforeName == afterName) return false;
    if (auto* operators = context.GetOperators()) {
        return operators->Commands().SetSceneName(context, afterName);
    }
    EditorCommandOperator commandOperator;
    return commandOperator.SetSceneName(context, afterName);
}

bool CommitSceneGravityEdit(EditorContext& context, const Vec3& beforeGravity,
                            const Vec3& afterGravity)
{
    if (beforeGravity.x == afterGravity.x &&
        beforeGravity.y == afterGravity.y &&
        beforeGravity.z == afterGravity.z) {
        return false;
    }
    if (auto* operators = context.GetOperators()) {
        return operators->Commands().SetSceneGravity(context, afterGravity);
    }
    EditorCommandOperator commandOperator;
    return commandOperator.SetSceneGravity(context, afterGravity);
}

bool CommitSceneMainCameraHintEdit(EditorContext& context, uint64_t beforeActorID,
                                   uint64_t afterActorID)
{
    if (beforeActorID == afterActorID) return false;
    if (auto* operators = context.GetOperators()) {
        return operators->Commands().SetSceneMainCameraHint(context, afterActorID);
    }
    EditorCommandOperator commandOperator;
    return commandOperator.SetSceneMainCameraHint(context, afterActorID);
}

bool CommitSceneAmbientIntensityEdit(EditorContext& context, float beforeIntensity,
                                     float afterIntensity)
{
    if (afterIntensity < 0.0f) afterIntensity = 0.0f;
    if (beforeIntensity == afterIntensity) return false;
    if (auto* operators = context.GetOperators()) {
        return operators->Commands().SetSceneAmbientIntensity(context, afterIntensity);
    }
    EditorCommandOperator commandOperator;
    return commandOperator.SetSceneAmbientIntensity(context, afterIntensity);
}

template <typename Fn>
bool ModifyMaterialAssetField(EditorContext& context, const std::string& path,
                              const char* label, Fn&& edit)
{
    MaterialModifier modifier(
        path, label,
        [fn = std::forward<Fn>(edit)](MaterialAsset& target) mutable {
            fn(target);
            return true;
        });
    return modifier.Modify(context);
}

bool RemoveComponentByType(EditorContext& context, Actor& actor, const char* typeName)
{
    if (!typeName || !actor.HasComponentType(typeName)) return false;
    if (auto* operators = context.GetOperators()) {
        return operators->Components().RemoveComponent(context, actor.GetID(), typeName);
    }
    EditorComponentOperator componentOperator;
    return componentOperator.RemoveComponent(context, actor.GetID(), typeName);
}

bool AddComponentByType(EditorContext& context, Actor& actor, const std::string& typeName,
                        const nlohmann::json& initialData)
{
    if (typeName.empty()) return false;
    if (auto* operators = context.GetOperators()) {
        return operators->Components().AddComponent(
            context, actor.GetID(), typeName, initialData);
    }
    EditorComponentOperator componentOperator;
    return componentOperator.AddComponent(context, actor.GetID(), typeName, initialData);
}

bool DrawStringField(const char* label, std::string& value)
{
    std::array<char, 260> buffer{};
    std::strncpy(buffer.data(), value.c_str(), buffer.size() - 1);
    if (!ImGui::InputText(label, buffer.data(), buffer.size())) return false;
    value = buffer.data();
    return true;
}

bool DrawVec2Field(const char* label, Vec2& value)
{
    float data[2] = {value.x, value.y};
    if (!ImGui::DragFloat2(label, data, 1.0f)) return false;
    value = {data[0], data[1]};
    return true;
}

bool DrawColorField(const char* label, Color& value)
{
    float data[4] = {value.r, value.g, value.b, value.a};
    if (!ImGui::ColorEdit4(label, data)) return false;
    value = {data[0], data[1], data[2], data[3]};
    return true;
}

void DrawJsonAssetPreview(const std::string& path)
{
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
        ImGui::BeginChild("##JsonAssetPreview", ImVec2(0.0f, 0.0f), true,
                          ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(preview.c_str());
        ImGui::EndChild();
    } catch (const std::exception& exception) {
        ImGui::TextDisabled("Invalid JSON: %s", exception.what());
    }
#else
    (void)path;
#endif
}

bool DrawScriptFieldValue(const ScriptFieldInfo& field, const nlohmann::json& current,
                          nlohmann::json& outValue)
{
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
                outValue = nlohmann::json::array({ value[0], value[1] });
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
                outValue = nlohmann::json::array({ value[0], value[1], value[2] });
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
    bool CanDraw(const EditorSelectObject& object,
                 const EditorContext&) const override
    {
        return object.IsActor();
    }
};

// ---------------------------------------------------------------------------
// Scene Settings Inspector (P1-A.2)
// ---------------------------------------------------------------------------
class SceneSettingsInspectorSection final : public EditorInspectorSection {
public:
    const char* GetID() const override { return "sceneSettings"; }
    int GetOrder() const override { return -10; }

    bool CanDraw(const EditorSelectObject& object,
                 const EditorContext&) const override {
        return object.IsNone();
    }

    void Draw(EditorContext& context) override {
        Scene* scene = context.GetScene();
        if (!scene) return;
        ImGui::Separator();
        ImGui::TextUnformatted("Scene Settings");

        std::array<char, 128> nameBuf{};
        std::strncpy(nameBuf.data(), scene->GetName().c_str(), nameBuf.size() - 1);
        if (ImGui::InputText("Name", nameBuf.data(), nameBuf.size())) {
            CommitSceneNameEdit(context, scene->GetName(), nameBuf.data());
        }

        ImGui::Text("Actors: %zu", scene->ActorCount());

        std::vector<Actor*> cameraActors;
        scene->ForEach([&](Actor& actor) {
            if (actor.GetComponent<CameraComponent>()) cameraActors.push_back(&actor);
        });
        const uint64_t currentMainCameraHint = scene->GetMainCameraHintActorID();
        std::string mainCameraLabel = "Auto";
        if (currentMainCameraHint != 0) {
            Actor* hintedActor = scene->FindByID(currentMainCameraHint);
            mainCameraLabel = hintedActor
                ? hintedActor->GetName() + " (" + std::to_string(currentMainCameraHint) + ")"
                : "Missing actor " + std::to_string(currentMainCameraHint);
        }
        if (ImGui::BeginCombo("Main Camera Hint", mainCameraLabel.c_str())) {
            if (ImGui::Selectable("Auto", currentMainCameraHint == 0)) {
                CommitSceneMainCameraHintEdit(context, currentMainCameraHint, 0);
            }
            for (Actor* cameraActor : cameraActors) {
                if (!cameraActor) continue;
                const uint64_t actorID = cameraActor->GetID();
                const std::string label = cameraActor->GetName() + " (" +
                    std::to_string(actorID) + ")";
                if (ImGui::Selectable(label.c_str(), currentMainCameraHint == actorID)) {
                    CommitSceneMainCameraHintEdit(context, currentMainCameraHint, actorID);
                }
            }
            ImGui::EndCombo();
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Rendering Defaults");
        float ambientIntensity = scene->GetAmbientIntensity();
        if (ImGui::DragFloat("Ambient Intensity", &ambientIntensity, 0.05f,
                             0.0f, 20.0f, "%.2f")) {
            CommitSceneAmbientIntensityEdit(
                context, scene->GetAmbientIntensity(), ambientIntensity);
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Physics");
        Vec3 grav = scene->GetPhysicsWorld().GetGravity();
        if (DrawVec3("Gravity", grav, 0.1f)) {
            CommitSceneGravityEdit(context, scene->GetPhysicsWorld().GetGravity(), grav);
        }
        ImGui::Separator();
        ImGui::TextUnformatted("Navigation");
        const NavigationWorld& navigation = scene->GetNavigationWorld();
        ImGui::Text("Status: %s", navigation.IsBaked() ? "Baked" : "Not baked");
        if (navigation.IsBaked()) {
            ImGui::Text("Grid: %u x %u", navigation.GetWidth(), navigation.GetHeight());
        }
        if (ImGui::Button("Bake Navigation")) {
            EditorNavigationBakeService{}.Bake(context, *scene);
        }
    }
};

// ---------------------------------------------------------------------------
// Asset Inspector base (P1-B.2)
// ---------------------------------------------------------------------------
class AssetInspectorSection : public EditorInspectorSection {
public:
    explicit AssetInspectorSection(EditorAssetType type) : m_Type(type) {}

    bool CanDraw(const EditorSelectObject& object,
                 const EditorContext& context) const override {
        if (!object.IsAsset()) return false;
        const EditorAssetRegistry* registry = context.GetAssetRegistry();
        const EditorAssetInfo* info = registry
            ? registry->GetAssetInfo(object.GetAssetPath()) : nullptr;
        if (m_Type == EditorAssetType::Unknown) {
            return !info || (info->type != EditorAssetType::Material
                && info->type != EditorAssetType::Texture
                && info->type != EditorAssetType::Model
                && info->type != EditorAssetType::Prefab
                && info->type != EditorAssetType::Audio);
        }
        return info && info->type == m_Type;
    }

private:
    EditorAssetType m_Type;
};

class ModelAssetInspectorSection final : public AssetInspectorSection {
public:
    ModelAssetInspectorSection()
        : AssetInspectorSection(EditorAssetType::Model) {}
    const char* GetID() const override { return "modelAsset"; }
    int GetOrder() const override { return -7; }

    void Draw(EditorContext& context) override {
        const std::string& path = context.GetSelection().GetAssetPath();
        if (path.empty()) return;

        ImGui::Separator();
        ImGui::PushID("ModelAsset");
        DrawAssetMetadataHeader(context, path, "Model");

        auto handle = AssetManager::Get().GetByPath<ModelAsset>(path);
        if (!handle.IsValid()) {
            handle = AssetManager::Get().Load<ModelAsset>(path);
        }
        if (!handle.IsValid()) {
            ImGui::TextDisabled("Model not loaded: %s", path.c_str());
            ImGui::PopID();
            return;
        }

        const ModelAsset* model = handle.Get();
        ImGui::Separator();
        ImGui::Text("Model: %s", model->GetName().c_str());
        ImGui::Text("Materials: %d", model->MaterialCount());
        ImGui::Text("Nodes: %zu", model->GetNodes().size());
        ImGui::Text("Bones: %zu", model->GetBones().size());
        ImGui::Text("Animations: %zu", model->GetAnimations().size());

        if (const MeshAsset* mesh = model->GetMeshPtr()) {
            ImGui::Separator();
            ImGui::TextUnformatted("Mesh");
            ImGui::Text("Vertices: %u", mesh->VertexCount());
            ImGui::Text("Indices: %u", mesh->IndexCount());
            ImGui::Text("Submeshes: %zu", mesh->GetSubMeshes().size());
            ImGui::Text("LODs: %zu", mesh->GetLods().size());
            ImGui::Text("GPU Uploaded: %s", mesh->IsUploaded() ? "Yes" : "No");
        } else {
            ImGui::TextDisabled("Mesh unavailable");
        }

        if (!model->GetMaterials().empty() &&
            ImGui::TreeNodeEx("Material Slots", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (size_t index = 0; index < model->GetMaterials().size(); ++index) {
                const MaterialHandle& material = model->GetMaterials()[index];
                ImGui::Text("%zu: %s", index,
                    material.IsValid() ? material->GetPath().c_str() : "(missing)");
            }
            ImGui::TreePop();
        }

        if (!model->GetAnimations().empty() && ImGui::TreeNodeEx("Animations")) {
            for (const AnimationClip& clip : model->GetAnimations()) {
                ImGui::Text("%s  %.2fs  tracks=%zu",
                    clip.name.empty() ? "(unnamed)" : clip.name.c_str(),
                    clip.duration, clip.tracks.size());
            }
            ImGui::TreePop();
        }

        ImGui::PopID();
    }
};

class PrefabAssetInspectorSection final : public AssetInspectorSection {
public:
    PrefabAssetInspectorSection()
        : AssetInspectorSection(EditorAssetType::Prefab) {}
    const char* GetID() const override { return "prefabAsset"; }
    int GetOrder() const override { return -6; }

    void Draw(EditorContext& context) override {
        const std::string& path = context.GetSelection().GetAssetPath();
        if (path.empty()) return;

        ImGui::Separator();
        ImGui::PushID("PrefabAsset");
        DrawAssetMetadataHeader(context, path, "Prefab");

        PrefabAsset prefab;
        std::string error;
        if (!PrefabAsset::Load(path, prefab, &error)) {
            ImGui::TextColored(ImVec4(0.9f, 0.35f, 0.25f, 1.0f),
                               "Prefab failed to load: %s", error.c_str());
            ImGui::PopID();
            return;
        }

        const PrefabNode* rootNode = nullptr;
        size_t componentCount = 0;
        size_t rootCount = 0;
        size_t sceneInstanceCount = 0;
        const std::filesystem::path resolvedPrefabPath =
            PrefabSystem::ResolvePrefabPath(path).lexically_normal();
        for (const PrefabNode& node : prefab.nodes) {
            componentCount += node.components.size();
            if (node.parentLocalId.empty()) ++rootCount;
            if (node.localId == prefab.rootLocalId) rootNode = &node;
        }
        if (Scene* scene = context.GetScene()) {
            scene->ForEach([&](Actor& actor) {
                if (!actor.IsPrefabRoot() || actor.GetPrefabAssetPath().empty()) return;
                const std::filesystem::path actorPrefabPath =
                    PrefabSystem::ResolvePrefabPath(actor.GetPrefabAssetPath())
                        .lexically_normal();
                if (actorPrefabPath == resolvedPrefabPath) ++sceneInstanceCount;
            });
        }

        ImGui::Separator();
        ImGui::Text("Prefab UUID: %s", prefab.uuid.empty() ? "(none)" : prefab.uuid.c_str());
        ImGui::Text("Root Local ID: %s",
                    prefab.rootLocalId.empty() ? "(none)" : prefab.rootLocalId.c_str());
        ImGui::Text("Root Actor: %s",
                    rootNode ? rootNode->name.c_str() : "(missing)");
        ImGui::Text("Nodes: %zu", prefab.nodes.size());
        ImGui::Text("Root Nodes: %zu", rootCount);
        ImGui::Text("Components: %zu", componentCount);
        ImGui::Text("Scene Instances: %zu", sceneInstanceCount);

        ImGui::BeginDisabled(sceneInstanceCount == 0 || !context.GetOperators());
        if (ImGui::Button("Select Scene Instances")) {
            context.GetOperators()->Prefabs().SelectInstances(context, path);
        }
        ImGui::EndDisabled();

        if (!prefab.Validate(&error)) {
            ImGui::TextColored(ImVec4(0.9f, 0.65f, 0.15f, 1.0f),
                               "Validation: %s", error.c_str());
        }

        if (!prefab.nodes.empty() &&
            ImGui::TreeNodeEx("Prefab Nodes", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (const PrefabNode& node : prefab.nodes) {
                ImGui::PushID(node.localId.c_str());
                const std::string label = node.name.empty()
                    ? "(unnamed)###node"
                    : node.name + "###node";
                const bool open = ImGui::TreeNodeEx(
                    label.c_str(),
                    ImGuiTreeNodeFlags_SpanAvailWidth |
                    (node.components.empty() ? ImGuiTreeNodeFlags_Leaf : 0));
                ImGui::SameLine();
                ImGui::TextDisabled("%s", node.localId.c_str());
                if (open) {
                    ImGui::Text("Parent: %s",
                                node.parentLocalId.empty()
                                    ? "(root)"
                                    : node.parentLocalId.c_str());
                    ImGui::Text("Active: %s", node.activeSelf ? "Yes" : "No");
                    if (!node.components.empty() &&
                        ImGui::TreeNodeEx("Components", ImGuiTreeNodeFlags_DefaultOpen)) {
                        for (const ComponentCreateDesc& component : node.components) {
                            ImGui::Text("%s%s",
                                        component.type.empty()
                                            ? "(unknown component)"
                                            : component.type.c_str(),
                                        component.enabled ? "" : " (disabled)");
                        }
                        ImGui::TreePop();
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }

        ImGui::PopID();
    }
};

class MaterialAssetInspectorSection final : public AssetInspectorSection {
public:
    MaterialAssetInspectorSection()
        : AssetInspectorSection(EditorAssetType::Material) {}
    const char* GetID() const override { return "materialAsset"; }
    int GetOrder() const override { return -5; }

    void Draw(EditorContext& context) override {
        const std::string& path = context.GetSelection().GetAssetPath();
        if (path.empty()) return;

        ImGui::Separator();
        ImGui::PushID("MaterialAsset");
        DrawAssetMetadataHeader(context, path, "Material");

        auto handle = AssetManager::Get().GetByPath<MaterialAsset>(path);
        if (!handle.IsValid()) {
            handle = AssetManager::Get().Load<MaterialAsset>(path);
        }
        if (!handle.IsValid()) {
            ImGui::TextDisabled("Material not loaded: %s", path.c_str());
            ImGui::PopID();
            return;
        }

        auto* mat = handle.Get();
        ImGui::Separator();
        ImGui::Text("Material: %s", mat->GetName().c_str());

        // Blend mode
        int blendMode = static_cast<int>(mat->GetBlendMode());
        const char* blendModes[] = {"Opaque", "AlphaTest", "Transparent"};
        if (ImGui::Combo("Blend Mode", &blendMode, blendModes, 3)) {
            ModifyMaterialAssetField(
                context, path, "Set Material Blend Mode",
                [blendMode](MaterialAsset& target) {
                    target.SetBlendMode(static_cast<BlendMode>(blendMode));
                });
        }

        // Alpha threshold
        if (mat->GetBlendMode() == BlendMode::AlphaTest) {
            float threshold = mat->GetAlphaThreshold();
            if (ImGui::DragFloat("Alpha Threshold", &threshold, 0.01f, 0.0f, 1.0f)) {
                ModifyMaterialAssetField(
                    context, path, "Set Material Alpha Threshold",
                    [threshold](MaterialAsset& target) {
                        target.SetAlphaThreshold(threshold);
                    });
            }
        }

        // Two-sided
        bool twoSided = mat->IsTwoSided();
        if (ImGui::Checkbox("Two Sided", &twoSided)) {
            ModifyMaterialAssetField(
                context, path, "Set Material Two Sided",
                [twoSided](MaterialAsset& target) {
                    target.SetTwoSided(twoSided);
                });
        }

        // Wireframe
        bool wireframe = mat->IsWireframe();
        if (ImGui::Checkbox("Wireframe", &wireframe)) {
            ModifyMaterialAssetField(
                context, path, "Set Material Wireframe",
                [wireframe](MaterialAsset& target) {
                    target.SetWireframe(wireframe);
                });
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Parameters");

        // Material parameters
        std::vector<std::pair<std::string, MaterialParam>> params;
        params.reserve(mat->GetParams().size());
        for (const auto& [name, param] : mat->GetParams()) {
            params.emplace_back(name, param);
        }
        for (const auto& [name, param] : params) {
            ImGui::PushID(name.c_str());
            switch (param.type) {
                case MaterialParam::Type::Float: {
                    float v = param.data[0];
                    if (ImGui::DragFloat(name.c_str(), &v, 0.01f)) {
                        ModifyMaterialAssetField(
                            context, path, "Set Material Parameter",
                            [name, v](MaterialAsset& target) {
                                target.SetParam(name, MaterialParam::FromFloat(v));
                            });
                    }
                    break;
                }
                case MaterialParam::Type::Vec3: {
                    Vec3 v(param.data[0], param.data[1], param.data[2]);
                    if (DrawVec3(name.c_str(), v, 0.01f)) {
                        ModifyMaterialAssetField(
                            context, path, "Set Material Parameter",
                            [name, v](MaterialAsset& target) {
                                target.SetParam(name,
                                    MaterialParam::FromVec3(v.x, v.y, v.z));
                            });
                    }
                    break;
                }
                case MaterialParam::Type::Vec4: {
                    float data[4] = {param.data[0], param.data[1], param.data[2], param.data[3]};
                    if (ImGui::ColorEdit4(name.c_str(), data)) {
                        const float x = data[0];
                        const float y = data[1];
                        const float z = data[2];
                        const float w = data[3];
                        ModifyMaterialAssetField(
                            context, path, "Set Material Parameter",
                            [name, x, y, z, w](MaterialAsset& target) {
                                target.SetParam(name,
                                    MaterialParam::FromVec4(x, y, z, w));
                            });
                    }
                    break;
                }
                default: ImGui::Text("%s: (unsupported type)", name.c_str()); break;
            }
            ImGui::PopID();
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Textures");

        // Textures
        const auto& textures = mat->GetTextures();
        for (const auto& [slot, tex] : textures) {
            ImGui::Text("%s: %s", slot.c_str(), tex.IsValid() ? tex->GetPath().c_str() : "(none)");
        }

        // Save button
        ImGui::Separator();
        if (ImGui::Button("Save Material")) {
            MaterialModifier modifier(
                path, "Modify Material",
                [mat](MaterialAsset& target) {
                    target.ReloadFrom(*mat);
                    return true;
                });
            if (modifier.Modify(context)) {
                Logger::Info("[Editor] Material saved: ", path);
            } else {
                Logger::Warn("[Editor] Failed to save material: ", path);
            }
        }

        ImGui::PopID();
    }
};

class TextureAssetInspectorSection final : public AssetInspectorSection {
public:
    TextureAssetInspectorSection()
        : AssetInspectorSection(EditorAssetType::Texture) {}
    const char* GetID() const override { return "textureAsset"; }
    int GetOrder() const override { return -4; }

    static const char* FilterName(TextureFilter filter) {
        switch (filter) {
            case TextureFilter::Nearest: return "Nearest";
            default: return "Linear";
        }
    }

    static const char* WrapName(TextureWrap wrap) {
        switch (wrap) {
            case TextureWrap::Clamp: return "Clamp";
            default: return "Repeat";
        }
    }

    void Draw(EditorContext& context) override {
        const std::string& path = context.GetSelection().GetAssetPath();
        if (path.empty()) return;

        ImGui::Separator();
        ImGui::PushID("TextureAsset");
        DrawAssetMetadataHeader(context, path, "Texture");

        auto handle = AssetManager::Get().GetByPath<TextureAsset>(path);
        if (!handle.IsValid()) {
            handle = AssetManager::Get().Load<TextureAsset>(path);
        }
        if (!handle.IsValid()) {
            ImGui::TextDisabled("Texture not loaded: %s", path.c_str());
            ImGui::PopID();
            return;
        }

        auto* tex = handle.Get();
        ImGui::Separator();
        ImGui::Text("Texture: %s", tex->GetName().c_str());

        int w = tex->GetWidth();
        int h = tex->GetHeight();
        ImGui::Text("Size: %d x %d", w, h);
        ImGui::Text("Mips: %d", tex->GetMipLevels());
        ImGui::Text("Format: SRGB=%s", tex->GetDesc().sRGB ? "Yes" : "No");

        TextureFilter filter = tex->GetFilter();
        TextureWrap wrapU = tex->GetWrapU();
        TextureWrap wrapV = tex->GetWrapV();
        bool changed = false;
        ImGui::Separator();
        ImGui::TextUnformatted("Sampler");
        if (ImGui::BeginCombo("Filter", FilterName(filter))) {
            if (ImGui::Selectable("Nearest", filter == TextureFilter::Nearest)) {
                filter = TextureFilter::Nearest;
                changed = true;
            }
            if (ImGui::Selectable("Linear", filter != TextureFilter::Nearest)) {
                filter = TextureFilter::Linear;
                changed = true;
            }
            ImGui::EndCombo();
        }
        if (ImGui::BeginCombo("Wrap U", WrapName(wrapU))) {
            if (ImGui::Selectable("Repeat", wrapU != TextureWrap::Clamp)) {
                wrapU = TextureWrap::Repeat;
                changed = true;
            }
            if (ImGui::Selectable("Clamp", wrapU == TextureWrap::Clamp)) {
                wrapU = TextureWrap::Clamp;
                changed = true;
            }
            ImGui::EndCombo();
        }
        if (ImGui::BeginCombo("Wrap V", WrapName(wrapV))) {
            if (ImGui::Selectable("Repeat", wrapV != TextureWrap::Clamp)) {
                wrapV = TextureWrap::Repeat;
                changed = true;
            }
            if (ImGui::Selectable("Clamp", wrapV == TextureWrap::Clamp)) {
                wrapV = TextureWrap::Clamp;
                changed = true;
            }
            ImGui::EndCombo();
        }
        if (changed) {
            const EditorAssetRegistry* registry = context.GetAssetRegistry();
            const EditorAssetInfo* info = registry ? registry->GetAssetInfo(path) : nullptr;
            if (info && info->imported && !info->uuid.empty()) {
                const std::string settingsJson = ImportSettingsWithTextureSampler(
                    context, info->uuid, filter, wrapU, wrapV);
                if (auto* operators = context.GetOperators()) {
                    operators->Assets().ReimportWithSettings(context, info->uuid, settingsJson);
                } else {
                    EditorAssetOperator assetOperator;
                    assetOperator.ReimportWithSettings(context, info->uuid, settingsJson);
                }
            }
        }

        // Texture thumbnail (requires ImGui GPU backend integration for preview)\n        if (tex->GetGpuHandle()) {\n            ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.3f, 1.0f), "(GPU resident)");\n        } else {\n            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(preview not available)");\n        }

        ImGui::PopID();
    }
};

class AudioAssetInspectorSection final : public AssetInspectorSection {
public:
    AudioAssetInspectorSection()
        : AssetInspectorSection(EditorAssetType::Audio) {}
    const char* GetID() const override { return "audioAsset"; }
    int GetOrder() const override { return -3; }

    void Draw(EditorContext& context) override {
        const std::string& path = context.GetSelection().GetAssetPath();
        if (path.empty()) return;

        ImGui::Separator();
        ImGui::PushID("AudioAsset");
        DrawAssetMetadataHeader(context, path, "Audio");

        auto handle = AssetManager::Get().GetByPath<AudioClipAsset>(path);
        if (!handle.IsValid()) {
            handle = AssetManager::Get().Load<AudioClipAsset>(path);
        }
        if (!handle.IsValid()) {
            ImGui::TextDisabled("Audio clip not loaded: %s", path.c_str());
            ImGui::PopID();
            return;
        }

        const AudioClipAsset* clip = handle.Get();
        ImGui::Separator();
        ImGui::Text("Clip: %s", clip->GetName().c_str());
        ImGui::Text("Channels: %u", clip->GetChannels());
        ImGui::Text("Sample Rate: %u Hz", clip->GetSampleRate());
        ImGui::Text("Frames: %llu",
                    static_cast<unsigned long long>(clip->GetFrameCount()));
        ImGui::Text("Duration: %.3f seconds", clip->GetDurationSeconds());
        ImGui::PopID();
    }
};

class GenericAssetInspectorSection final : public AssetInspectorSection {
public:
    GenericAssetInspectorSection()
        : AssetInspectorSection(EditorAssetType::Unknown) {}
    const char* GetID() const override { return "genericAsset"; }
    int GetOrder() const override { return 0; }

    void Draw(EditorContext& context) override {
        const std::string& path = context.GetSelection().GetAssetPath();
        if (path.empty()) return;

        DrawAssetMetadataHeader(context, path, "Asset");

        if (IsJsonAsset(path)) {
            ImGui::TextUnformatted("Type: JSON");
            DrawJsonAssetPreview(path);
            return;
        }

        auto handle = AssetManager::Get().GetByPath<Asset>(path);
        if (!handle.IsValid()) {
            handle = AssetManager::Get().Load<Asset>(path);
        }

        ImGui::Text("Asset: %s",
            handle.IsValid() ? handle->GetName().c_str() : "(not loaded)");

        if (handle.IsValid()) {
            ImGui::Text("Type: %s", AssetTypeToString(handle->GetType()));
            ImGui::Text("Loaded: %s", handle->IsReady() ? "Yes" : "No");
        }
    }
};

// ... (rest of existing inspector sections unchanged)
std::shared_ptr<MaterialAsset> CloneMaterial(const MaterialAsset& source)
{
    auto result = std::make_shared<MaterialAsset>(source.GetPath());
    result->SetName(source.GetName());
    result->SetBlendMode(source.GetBlendMode());
    result->SetTwoSided(source.IsTwoSided());
    result->SetWireframe(source.IsWireframe());
    result->SetAlphaThreshold(source.GetAlphaThreshold());
    for (const auto& [name, value] : source.GetParams()) result->SetParam(name, value);
    for (const auto& [slot, texture] : source.GetTextures()) result->SetTexture(slot, texture);
    result->MarkReady();
    return result;
}

class TransformInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "transform"; }
    int GetOrder() const override { return 0; }

    void Draw(EditorContext& context) override
    {
        Actor* actor = SelectedActor(context);
        if (!actor) return;

        ImGui::Separator();
        if (!SectionHeaderWithIcon(context, EditorIcons::Actor, "Transform")) return;
        Transform& transform = actor->GetTransform();
        EditorWidgets::BeginPropertyGrid("TransformProperties");
        if (EditorWidgets::BeginPropertyRow("Position")) {
            float value[3] = {transform.position.x, transform.position.y, transform.position.z};
            if (ImGui::DragFloat3("##Value", value, 0.05f)) {
                transform.position = {value[0], value[1], value[2]};
            }
            EditorWidgets::EndPropertyRow();
        }
        if (EditorWidgets::BeginPropertyRow("Rotation")) {
            float value[3] = {transform.rotation.x, transform.rotation.y, transform.rotation.z};
            if (ImGui::DragFloat3("##Value", value, 0.2f)) {
                transform.rotation = {value[0], value[1], value[2]};
            }
            EditorWidgets::EndPropertyRow();
        }
        if (EditorWidgets::BeginPropertyRow("Scale")) {
            float value[3] = {transform.scale.x, transform.scale.y, transform.scale.z};
            if (ImGui::DragFloat3("##Value", value, 0.05f)) {
                transform.scale = {value[0], value[1], value[2]};
            }
            EditorWidgets::EndPropertyRow();
        }
        EditorWidgets::EndPropertyGrid();
    }
};

class MeshRendererInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "meshRenderer"; }
    int GetOrder() const override { return 100; }

    void Draw(EditorContext& context) override
    {
        Actor* actor = SelectedActor(context);
        auto* renderer = actor ? actor->GetComponent<MeshRendererComponent>() : nullptr;
        if (!renderer) return;

        ImGui::Separator();
        ImGui::PushID("MeshRenderer");
        if (!SectionHeaderWithIcon(context, EditorIcons::Mesh, "Mesh Renderer")) {
            ImGui::PopID();
            return;
        }
        DrawEnabled(*renderer);

        std::vector<std::string> meshes {
            "__builtin__/Triangle", "__builtin__/Quad", "__builtin__/Cube"
        };
        auto extra = AssetManager::Get().GetCachedPathsByType(AssetType::Mesh);
        meshes.insert(meshes.end(), extra.begin(), extra.end());
        const std::string meshPath = renderer->GetMesh() ? renderer->GetMesh()->GetPath() : "";
        if (ImGui::BeginCombo("Mesh", meshPath.empty() ? "(none)" : meshPath.c_str())) {
            for (const std::string& path : meshes) {
                if (ImGui::Selectable(path.c_str(), path == meshPath)) {
                    const MeshHandle mesh = ResolveMesh(path);
                    if (mesh.IsValid()) renderer->SetMesh(mesh);
                }
            }
            ImGui::EndCombo();
        }

        std::vector<std::string> materials {"__builtin__/Default"};
        extra = AssetManager::Get().GetCachedPathsByType(AssetType::Material);
        materials.insert(materials.end(), extra.begin(), extra.end());
        const std::string materialPath = renderer->GetMaterialForSlot(0)
            ? renderer->GetMaterialForSlot(0)->GetPath() : "";
        if (ImGui::BeginCombo("Default Slot / Slot 0",
                              materialPath.empty() ? "(none)" : materialPath.c_str())) {
            for (const std::string& path : materials) {
                if (ImGui::Selectable(path.c_str(), path == materialPath)) {
                    const MaterialHandle material = ResolveMaterial(path);
                    if (material.IsValid()) renderer->SetMaterialSlot(0, material);
                }
            }
            ImGui::EndCombo();
        }

        size_t slotCount = renderer->GetMaterials().size();
        if (MeshAsset* mesh = renderer->GetMesh().Get()) {
            for (const SubMesh& subMesh : mesh->GetSubMeshes()) {
                if (subMesh.materialSlot >= 0) {
                    slotCount = (std::max)(slotCount,
                        static_cast<size_t>(subMesh.materialSlot) + 1);
                }
            }
        }
        if (slotCount > 1) {
            ImGui::TextDisabled("Material Slots");
            for (size_t slot = 0; slot < slotCount; ++slot) {
                ImGui::PushID(static_cast<int>(slot));
                const MaterialHandle current = renderer->GetMaterialForSlot(
                    static_cast<int>(slot));
                const std::string currentPath = current ? current->GetPath() : "";
                const std::string label = "Slot " + std::to_string(slot);
                if (ImGui::BeginCombo(label.c_str(),
                                      currentPath.empty() ? "(none)" : currentPath.c_str())) {
                    for (const std::string& path : materials) {
                        if (ImGui::Selectable(path.c_str(), path == currentPath)) {
                            const MaterialHandle material = ResolveMaterial(path);
                            if (material.IsValid()) {
                                renderer->SetMaterialSlot(slot, material);
                            }
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopID();
            }
        }

        if (EditorWidgets::IconButton("RemoveMeshRenderer", "X", "Remove Mesh Renderer")) {
            RemoveComponentByType(context, *actor, "MeshRenderer");
        }
        ImGui::PopID();
    }
};

class SkinnedMeshInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "skinnedMesh"; }
    int GetOrder() const override { return 110; }

    void Draw(EditorContext& context) override
    {
        Actor* actor = SelectedActor(context);
        auto* skinned = actor ? actor->GetComponent<SkinnedMeshRendererComponent>() : nullptr;
        if (!skinned) return;

        ImGui::Separator();
        ImGui::PushID("SkinnedMesh");
        if (!SectionHeaderWithIcon(context, EditorIcons::Mesh, "Skinned Mesh Renderer")) {
            ImGui::PopID();
            return;
        }
        DrawEnabled(*skinned);

        if (EditorWidgets::IconButton("RemoveSkinnedMesh", "X", "Remove Skinned Mesh")) {
            RemoveComponentByType(context, *actor, "SkinnedMeshRenderer");
        }
        ImGui::PopID();
    }
};

class AnimatorInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "animator"; }
    int GetOrder() const override { return 120; }
    void Draw(EditorContext& context) override
    {
        Actor* actor = SelectedActor(context);
        auto* animator = actor ? actor->GetComponent<AnimatorComponent>() : nullptr;
        if (!animator) return;
        ImGui::Separator();
        ImGui::PushID("Animator");
        if (SectionHeaderWithIcon(context, EditorIcons::Mesh, "Animator")) {
            DrawEnabled(*animator);
            ImGui::Text("State: %s", animator->GetCurrentState().empty() ? "(none)" : animator->GetCurrentState().c_str());
            ImGui::Text("Normalized Time: %.3f", animator->GetNormalizedTime());
            bool rootMotion = animator->AppliesRootMotion();
            if (ImGui::Checkbox("Apply Root Motion", &rootMotion)) {
                CommitComponentEdit(context, *actor, *animator, "applyRootMotion", [&] {
                    animator->SetApplyRootMotion(rootMotion);
                });
            }
            ImGui::TextDisabled("Controller states are serialized with this component in P0.");
            if (EditorWidgets::IconButton("RemoveAnimator", "X", "Remove Animator"))
                RemoveComponentByType(context, *actor, "Animator");
        }
        ImGui::PopID();
    }
};

class ThirdPersonCameraInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "thirdPersonCamera"; }
    int GetOrder() const override { return 121; }
    void Draw(EditorContext& context) override
    {
        Actor* actor = SelectedActor(context);
        auto* camera = actor ? actor->GetComponent<ThirdPersonCameraComponent>() : nullptr;
        if (!camera) return;
        ImGui::Separator();
        ImGui::PushID("ThirdPersonCamera");
        if (SectionHeaderWithIcon(context, EditorIcons::Camera, "Third Person Camera")) {
            DrawEnabled(*camera);
            float distance = camera->GetDistance();
            float sensitivity = camera->GetSensitivity();
            float radius = camera->GetCollisionRadius();
            float sharpness = camera->GetFollowSharpness();
            Vec3 offset = camera->GetTargetOffset();
            if (ImGui::DragFloat("Distance", &distance, 0.05f, 0.1f, 100.0f))
                CommitComponentEdit(context, *actor, *camera, "distance", [&] { camera->SetDistance(distance); });
            if (DrawVec3("Target Offset", offset, 0.02f))
                CommitComponentEdit(context, *actor, *camera, "targetOffset", [&] { camera->SetTargetOffset(offset); });
            if (ImGui::DragFloat("Sensitivity", &sensitivity, 0.01f, 0.0f, 10.0f))
                CommitComponentEdit(context, *actor, *camera, "sensitivity", [&] { camera->SetSensitivity(sensitivity); });
            if (ImGui::DragFloat("Collision Radius", &radius, 0.01f, 0.0f, 5.0f))
                CommitComponentEdit(context, *actor, *camera, "collisionRadius", [&] { camera->SetCollisionRadius(radius); });
            if (ImGui::DragFloat("Follow Sharpness", &sharpness, 0.1f, 0.0f, 100.0f))
                CommitComponentEdit(context, *actor, *camera, "followSharpness", [&] { camera->SetFollowSharpness(sharpness); });
            if (EditorWidgets::IconButton("RemoveThirdPersonCamera", "X", "Remove Third Person Camera"))
                RemoveComponentByType(context, *actor, "ThirdPersonCamera");
        }
        ImGui::PopID();
    }
};

class GameplayInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "gameplay"; }
    int GetOrder() const override { return 240; }

    void Draw(EditorContext& context) override
    {
        Actor* actor = SelectedActor(context);
        if (!actor) return;

        auto* health = actor->GetComponent<HealthComponent>();
        auto* hitbox = actor->GetComponent<HitboxComponent>();
        auto* hurtbox = actor->GetComponent<HurtboxComponent>();
        auto* interaction = actor->GetComponent<InteractionComponent>();
        auto* particles = actor->GetComponent<ParticleSystemComponent>();
        auto* listener = actor->GetComponent<AudioListenerComponent>();
        auto* agent = actor->GetComponent<NavAgentComponent>();
        auto* enemy = actor->GetComponent<EnemyAIComponent>();
        auto* feedback = actor->GetComponent<GameplayFeedbackComponent>();
        if (!health && !hitbox && !hurtbox && !interaction && !particles &&
            !listener && !agent && !enemy && !feedback) {
            return;
        }

        ImGui::Separator();
        ImGui::PushID("GameplayComponents");
        if (!SectionHeaderWithIcon(context, EditorIcons::Physics, "Gameplay")) {
            ImGui::PopID();
            return;
        }

        if (health) {
            ImGui::TextUnformatted("Health");
            float maxHealth = health->GetMaxHealth();
            float value = health->GetHealth();
            if (ImGui::DragFloat("Max Health", &maxHealth, 1.0f, 0.01f, 100000.0f)) {
                CommitComponentEdit(context, *actor, *health, "maxHealth",
                    [&] { health->SetMaxHealth(maxHealth, true); });
            }
            if (ImGui::DragFloat("Current Health", &value, 1.0f, 0.0f, 100000.0f)) {
                CommitComponentEdit(context, *actor, *health, "health",
                    [&] { health->SetHealth(value); });
            }
            if (EditorWidgets::IconButton("RemoveHealth", "X", "Remove Health")) {
                RemoveComponentByType(context, *actor, "Health");
            }
        }

        if (hitbox) {
            ImGui::TextUnformatted("Hitbox");
            float damage = hitbox->GetDamage();
            float radius = hitbox->GetRadius();
            uint32_t team = hitbox->GetTeam();
            if (ImGui::DragFloat("Damage", &damage, 0.5f, 0.0f, 10000.0f)) {
                CommitComponentEdit(context, *actor, *hitbox, "damage",
                    [&] { hitbox->SetDamage(damage); });
            }
            if (ImGui::DragFloat("Hit Radius", &radius, 0.02f, 0.01f, 100.0f)) {
                CommitComponentEdit(context, *actor, *hitbox, "radius",
                    [&] { hitbox->SetRadius(radius); });
            }
            if (ImGui::InputScalar("Hit Team", ImGuiDataType_U32, &team)) {
                CommitComponentEdit(context, *actor, *hitbox, "team",
                    [&] { hitbox->SetTeam(team); });
            }
        }

        if (hurtbox) {
            ImGui::TextUnformatted("Hurtbox");
            uint32_t team = hurtbox->GetTeam();
            float multiplier = hurtbox->GetDamageMultiplier();
            if (ImGui::InputScalar("Hurt Team", ImGuiDataType_U32, &team)) {
                CommitComponentEdit(context, *actor, *hurtbox, "team",
                    [&] { hurtbox->SetTeam(team); });
            }
            if (ImGui::DragFloat("Damage Multiplier", &multiplier, 0.05f, 0.0f, 10.0f)) {
                CommitComponentEdit(context, *actor, *hurtbox, "damageMultiplier",
                    [&] { hurtbox->SetDamageMultiplier(multiplier); });
            }
        }

        if (interaction) {
            ImGui::TextUnformatted("Interaction");
            float range = interaction->GetRange();
            bool single = interaction->IsSingleUse();
            bool destroyOnUse = interaction->GetDestroyOnUse();
            if (ImGui::DragFloat("Interaction Range", &range, 0.05f, 0.0f, 100.0f)) {
                CommitComponentEdit(context, *actor, *interaction, "range",
                    [&] { interaction->SetRange(range); });
            }
            if (ImGui::Checkbox("Single Use", &single)) {
                CommitComponentEdit(context, *actor, *interaction, "singleUse",
                    [&] { interaction->SetSingleUse(single); });
            }
            if (ImGui::Checkbox("Destroy On Use", &destroyOnUse)) {
                CommitComponentEdit(context, *actor, *interaction, "destroyOnUse",
                    [&] { interaction->SetDestroyOnUse(destroyOnUse); });
            }
            ImGui::Text("Prompt: %s", interaction->GetPrompt().c_str());
        }

        if (particles) {
            ImGui::TextUnformatted("Particle System");
            auto settings = particles->GetSettings();
            bool changed = ImGui::InputScalar(
                "Max Particles", ImGuiDataType_U32, &settings.maxParticles);
            changed |= ImGui::DragFloat("Emission Rate", &settings.rate,
                                        0.5f, 0.0f, 10000.0f);
            changed |= ImGui::DragFloat("Lifetime", &settings.lifetime,
                                        0.05f, 0.01f, 100.0f);
            changed |= ImGui::DragFloat("Start Size", &settings.startSize,
                                        0.01f, 0.0f, 100.0f);
            if (changed) {
                CommitComponentEdit(context, *actor, *particles, "emitter",
                    [&] { particles->GetSettings() = settings; });
            }
            ImGui::Text("Alive: %zu", particles->GetAliveCount());
        }

        if (listener) {
            bool primary = listener->IsPrimary();
            if (ImGui::Checkbox("Primary Audio Listener", &primary)) {
                CommitComponentEdit(context, *actor, *listener, "primary",
                    [&] { listener->SetPrimary(primary); });
            }
        }

        if (agent) {
            float speed = agent->GetSpeed();
            float stop = agent->GetStoppingDistance();
            if (ImGui::DragFloat("Agent Speed", &speed, 0.1f, 0.0f, 100.0f)) {
                CommitComponentEdit(context, *actor, *agent, "speed",
                    [&] { agent->SetSpeed(speed); });
            }
            if (ImGui::DragFloat("Stopping Distance", &stop, 0.01f, 0.0f, 10.0f)) {
                CommitComponentEdit(context, *actor, *agent, "stoppingDistance",
                    [&] { agent->SetStoppingDistance(stop); });
            }
            ImGui::Text("Path: %s",
                        agent->HasPath() ? "Active" :
                        (agent->ReachedDestination() ? "Reached" : "None"));
        }

        if (enemy) {
            float detection = enemy->GetDetectionRange();
            float fieldOfView = enemy->GetFieldOfViewDegrees();
            float attackRange = enemy->GetAttackRange();
            float damage = enemy->GetAttackDamage();
            if (ImGui::DragFloat("Detection Range", &detection, 0.1f, 0.0f, 1000.0f)) {
                CommitComponentEdit(context, *actor, *enemy, "detectionRange",
                    [&] { enemy->SetDetectionRange(detection); });
            }
            if (ImGui::DragFloat("Field Of View", &fieldOfView, 1.0f, 1.0f, 360.0f)) {
                CommitComponentEdit(context, *actor, *enemy, "fieldOfView",
                    [&] { enemy->SetFieldOfViewDegrees(fieldOfView); });
            }
            if (ImGui::DragFloat("Attack Range", &attackRange, 0.05f, 0.0f, 100.0f)) {
                CommitComponentEdit(context, *actor, *enemy, "attackRange",
                    [&] { enemy->SetAttackRange(attackRange); });
            }
            if (ImGui::DragFloat("Attack Damage", &damage, 0.5f, 0.0f, 10000.0f)) {
                CommitComponentEdit(context, *actor, *enemy, "attackDamage",
                    [&] { enemy->SetAttackDamage(damage); });
            }
            ImGui::Text("State: %d", static_cast<int>(enemy->GetState()));
        }

        if (feedback) {
            ImGui::Text("Feedback: %s", feedback->IsActive() ? "Active" : "Idle");
        }
        ImGui::PopID();
    }
};

class MaterialInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "material"; }
    int GetOrder() const override { return 200; }

    void Draw(EditorContext& context) override
    {
        Actor* actor = SelectedActor(context);
        auto* renderer = actor ? actor->GetComponent<MeshRendererComponent>() : nullptr;
        if (!renderer || !renderer->GetMaterial().IsValid()) return;

        ImGui::Separator();
        ImGui::PushID("Material");
        if (!SectionHeaderWithIcon(context, EditorIcons::Material, "Material Instance")) {
            ImGui::PopID();
            return;
        }
        auto* mat = renderer->GetMaterial().Get();
        if (!mat) { ImGui::PopID(); return; }

        ImGui::Text("Source: %s", mat->GetPath().c_str());
        ImGui::PopID();
    }
};

class AudioSourceInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "audioSource"; }
    int GetOrder() const override { return 250; }

    void Draw(EditorContext& context) override
    {
        Actor* actor = SelectedActor(context);
        auto* source = actor ? actor->GetComponent<AudioSourceComponent>() : nullptr;
        if (!source) return;

        ImGui::Separator();
        ImGui::PushID("AudioSource");
        if (!SectionHeaderWithIcon(context, EditorIcons::Audio, "Audio Source")) {
            ImGui::PopID();
            return;
        }
        DrawEnabled(*source);

        std::vector<std::string> clips = AssetManager::Get().GetCachedPathsByType(AssetType::AudioClip);
        if (const EditorAssetRegistry* registry = context.GetAssetRegistry()) {
            for (const auto& asset : registry->GetAssets(EditorAssetType::Audio)) {
                clips.push_back(asset.absolutePath.string());
                if (!asset.relativePath.empty())
                    clips.push_back((std::filesystem::path("Content") / asset.relativePath).generic_string());
            }
        }
        std::sort(clips.begin(), clips.end());
        clips.erase(std::unique(clips.begin(), clips.end()), clips.end());

        const std::string current = source->GetClipPath().empty()
            ? std::string("(none)") : source->GetClipPath();
        if (ImGui::BeginCombo("Clip", current.c_str())) {
            if (ImGui::Selectable("(none)", source->GetClipPath().empty()))
                source->SetClipPath({});
            for (const std::string& path : clips)
                if (ImGui::Selectable(path.c_str(), path == source->GetClipPath()))
                    source->SetClipPath(path);
            ImGui::EndCombo();
        }

        bool playOnStart = source->GetPlayOnStart();
        if (ImGui::Checkbox("Play On Start", &playOnStart)) source->SetPlayOnStart(playOnStart);
        bool loop = source->GetLoop();
        if (ImGui::Checkbox("Loop", &loop)) source->SetLoop(loop);
        bool spatial = source->GetSpatial();
        if (ImGui::Checkbox("Spatial", &spatial)) source->SetSpatial(spatial);
        float volume = source->GetVolume();
        if (ImGui::SliderFloat("Volume", &volume, 0.0f, 2.0f)) source->SetVolume(volume);
        float pitch = source->GetPitch();
        if (ImGui::SliderFloat("Pitch", &pitch, 0.25f, 4.0f)) source->SetPitch(pitch);
        float minDistance = source->GetMinDistance();
        if (ImGui::DragFloat("Min Distance", &minDistance, 0.05f, 0.01f, 1000.0f))
            source->SetMinDistance(minDistance);
        float maxDistance = source->GetMaxDistance();
        if (ImGui::DragFloat("Max Distance", &maxDistance, 0.1f, 0.01f, 10000.0f))
            source->SetMaxDistance(maxDistance);

        if (EditorWidgets::IconButton("RemoveAudioSource", "X", "Remove Audio Source"))
            RemoveComponentByType(context, *actor, "AudioSource");
        ImGui::PopID();
    }
};

class PhysicsInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "physics"; }
    int GetOrder() const override { return 300; }

    void Draw(EditorContext& context) override
    {
        Actor* actor = SelectedActor(context);
        if (!actor) return;

        auto* rb = actor->GetComponent<RigidBodyComponent>();
        auto* box = actor->GetComponent<BoxColliderComponent>();
        auto* sphere = actor->GetComponent<SphereColliderComponent>();
        auto* capsule = actor->GetComponent<CapsuleColliderComponent>();
        auto* character = actor->GetComponent<CharacterControllerComponent>();
        if (!rb && !box && !sphere && !capsule && !character) return;

        ImGui::Separator();
        ImGui::PushID("Physics");
        if (!SectionHeaderWithIcon(context, EditorIcons::Physics, "Physics")) {
            ImGui::PopID();
            return;
        }
        if (rb) {
            DrawEnabled(*rb);
            int bodyType = static_cast<int>(rb->GetBodyType());
            if (ImGui::Combo("Body Type", &bodyType, "Static\0Dynamic\0Kinematic\0")) {
                CommitComponentEdit(context, *actor, *rb, "bodyType", [&] {
                    rb->SetBodyType(static_cast<BodyType>(bodyType));
                });
            }
            float mass = rb->GetMass();
            if (ImGui::DragFloat("Mass", &mass, 0.1f, 0.01f, 1000.0f)) {
                CommitComponentEdit(context, *actor, *rb, "mass", [&] { rb->SetMass(mass); });
            }
            float linearDamping = rb->GetLinearDamping(), angularDamping = rb->GetAngularDamping();
            if (ImGui::DragFloat("Linear Damping", &linearDamping, 0.01f, 0.0f, 10.0f)) {
                CommitComponentEdit(context, *actor, *rb, "linearDamping", [&] {
                    rb->SetLinearDamping(linearDamping);
                });
            }
            if (ImGui::DragFloat("Angular Damping", &angularDamping, 0.01f, 0.0f, 10.0f)) {
                CommitComponentEdit(context, *actor, *rb, "angularDamping", [&] {
                    rb->SetAngularDamping(angularDamping);
                });
            }
            float friction = rb->GetFriction(), restitution = rb->GetRestitution();
            if (ImGui::SliderFloat("Friction", &friction, 0.0f, 1.0f)) {
                CommitComponentEdit(context, *actor, *rb, "friction", [&] {
                    rb->SetFriction(friction);
                });
            }
            if (ImGui::SliderFloat("Restitution", &restitution, 0.0f, 1.0f)) {
                CommitComponentEdit(context, *actor, *rb, "restitution", [&] {
                    rb->SetRestitution(restitution);
                });
            }
            bool gravity = rb->UsesGravity();
            if (ImGui::Checkbox("Use Gravity", &gravity)) {
                CommitComponentEdit(context, *actor, *rb, "useGravity", [&] {
                    rb->SetUseGravity(gravity);
                });
            }
            bool continuous = rb->GetCollisionDetectionMode() == CollisionDetectionMode::Continuous;
            if (ImGui::Checkbox("Continuous Collision", &continuous)) {
                CommitComponentEdit(context, *actor, *rb, "collisionDetection", [&] {
                    rb->SetCollisionDetectionMode(continuous
                        ? CollisionDetectionMode::Continuous
                        : CollisionDetectionMode::Discrete);
                });
            }
            Vec3 velocity = rb->GetVelocity(), angularVelocity = rb->GetAngularVelocity();
            if (DrawVec3("Velocity", velocity, 0.05f)) {
                CommitComponentEdit(context, *actor, *rb, "velocity", [&] {
                    rb->SetVelocity(velocity);
                });
            }
            if (DrawVec3("Angular Velocity", angularVelocity, 0.05f)) {
                CommitComponentEdit(context, *actor, *rb, "angularVelocity", [&] {
                    rb->SetAngularVelocity(angularVelocity);
                });
            }
            Vec3 linearLocks = rb->GetLinearAxisLocks(), angularLocks = rb->GetAngularAxisLocks();
            if (DrawVec3("Linear Axis Locks", linearLocks, 1.0f)) {
                CommitComponentEdit(context, *actor, *rb, "linearAxisLocks", [&] {
                    rb->SetLinearAxisLocks(linearLocks);
                });
            }
            if (DrawVec3("Angular Axis Locks", angularLocks, 1.0f)) {
                CommitComponentEdit(context, *actor, *rb, "angularAxisLocks", [&] {
                    rb->SetAngularAxisLocks(angularLocks);
                });
            }
            if (EditorWidgets::IconButton("RemoveRigidBody", "X", "Remove RigidBody"))
                RemoveComponentByType(context, *actor, "RigidBody");
        }

        const auto drawCollider = [&](ColliderComponent& collider) {
            bool trigger = collider.IsTrigger();
            if (ImGui::Checkbox("Trigger", &trigger)) {
                CommitComponentEdit(context, *actor, collider, "isTrigger", [&] {
                    collider.SetTrigger(trigger);
                });
            }
            uint32_t layer = collider.GetLayer(), mask = collider.GetLayerMask();
            if (ImGui::InputScalar("Layer", ImGuiDataType_U32, &layer)) {
                CommitComponentEdit(context, *actor, collider, "layer", [&] {
                    collider.SetLayer(layer);
                });
            }
            if (ImGui::InputScalar("Layer Mask", ImGuiDataType_U32, &mask)) {
                CommitComponentEdit(context, *actor, collider, "layerMask", [&] {
                    collider.SetLayerMask(mask);
                });
            }
        };

        if (box) {
            ImGui::TextUnformatted("Box Collider");
            DrawEnabled(*box);
            Vec3 half = box->GetHalfExtents();
            if (DrawVec3("HalfExtents", half, 0.05f)) {
                CommitComponentEdit(context, *actor, *box, "halfExtents", [&] {
                    box->SetHalfExtents(half);
                });
            }
            drawCollider(*box);
            if (EditorWidgets::IconButton("RemoveBoxCollider", "X", "Remove Box Collider"))
                RemoveComponentByType(context, *actor, "BoxCollider");
        }

        if (sphere) {
            ImGui::TextUnformatted("Sphere Collider");
            DrawEnabled(*sphere);
            float radius = sphere->GetRadius();
            if (ImGui::DragFloat("Radius", &radius, 0.05f, 0.01f, 100.0f)) {
                CommitComponentEdit(context, *actor, *sphere, "radius", [&] {
                    sphere->SetRadius(radius);
                });
            }
            drawCollider(*sphere);
            if (EditorWidgets::IconButton("RemoveSphereCollider", "X", "Remove Sphere Collider"))
                RemoveComponentByType(context, *actor, "SphereCollider");
        }
        if (capsule) {
            ImGui::TextUnformatted("Capsule Collider"); DrawEnabled(*capsule);
            float radius = capsule->GetRadius(), halfHeight = capsule->GetHalfHeight();
            if (ImGui::DragFloat("Capsule Radius", &radius, 0.05f, 0.01f, 100.0f)) {
                CommitComponentEdit(context, *actor, *capsule, "radius", [&] {
                    capsule->SetRadius(radius);
                });
            }
            if (ImGui::DragFloat("Capsule Half Height", &halfHeight, 0.05f, 0.0f, 100.0f)) {
                CommitComponentEdit(context, *actor, *capsule, "halfHeight", [&] {
                    capsule->SetHalfHeight(halfHeight);
                });
            }
            drawCollider(*capsule);
            if (EditorWidgets::IconButton("RemoveCapsuleCollider", "X", "Remove Capsule Collider"))
                RemoveComponentByType(context, *actor, "CapsuleCollider");
        }
        if (character) {
            ImGui::TextUnformatted("Character Controller"); DrawEnabled(*character);
            bool gravity = character->UsesGravity(); float step = character->GetStepOffset(), slope = character->GetMaxSlopeAngle();
            float jumpSpeed = character->GetJumpSpeed(), airControl = character->GetAirControl();
            if (ImGui::Checkbox("Character Gravity", &gravity)) {
                CommitComponentEdit(context, *actor, *character, "useGravity", [&] {
                    character->SetUseGravity(gravity);
                });
            }
            if (ImGui::DragFloat("Step Offset", &step, 0.01f, 0.0f, 10.0f)) {
                CommitComponentEdit(context, *actor, *character, "stepOffset", [&] {
                    character->SetStepOffset(step);
                });
            }
            if (ImGui::SliderFloat("Max Slope Angle", &slope, 0.0f, 89.0f)) {
                CommitComponentEdit(context, *actor, *character, "maxSlopeAngle", [&] {
                    character->SetMaxSlopeAngle(slope);
                });
            }
            if (ImGui::DragFloat("Jump Speed", &jumpSpeed, 0.05f, 0.0f, 100.0f)) {
                CommitComponentEdit(context, *actor, *character, "jumpSpeed", [&] {
                    character->SetJumpSpeed(jumpSpeed);
                });
            }
            if (ImGui::SliderFloat("Air Control", &airControl, 0.0f, 1.0f)) {
                CommitComponentEdit(context, *actor, *character, "airControl", [&] {
                    character->SetAirControl(airControl);
                });
            }
            if (EditorWidgets::IconButton("RemoveCharacterController", "X", "Remove Character Controller"))
                RemoveComponentByType(context, *actor, "CharacterController");
        }
        ImGui::PopID();
    }
};

class LightInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "light"; }
    int GetOrder() const override { return 350; }

    void Draw(EditorContext& context) override
    {
        Actor* actor = SelectedActor(context);
        auto* light = actor ? actor->GetComponent<LightComponent>() : nullptr;
        if (!light) return;

        ImGui::Separator();
        ImGui::PushID("Light");
        if (!SectionHeaderWithIcon(context, EditorIcons::Light, "Light")) {
            ImGui::PopID();
            return;
        }
        DrawEnabled(*light);

        int type = static_cast<int>(light->GetLightType());
        if (ImGui::Combo("Type", &type, "Directional\0Point\0Spot\0")) {
            CommitComponentEdit(context, *actor, *light, "type", [&] {
                light->SetLightType(static_cast<LightType>(type));
            });
        }
        Vec3 color = light->GetColor();
        float values[3] = {color.x, color.y, color.z};
        if (ImGui::ColorEdit3("Color", values)) {
            CommitComponentEdit(context, *actor, *light, "color", [&] {
                light->SetColor({values[0], values[1], values[2]});
            });
        }
        float intensity = light->GetIntensity();
        if (ImGui::DragFloat("Intensity", &intensity, 0.05f, 0.0f, 1000.0f)) {
            CommitComponentEdit(context, *actor, *light, "intensity", [&] {
                light->SetIntensity(intensity);
            });
        }
        bool castShadows = light->CastsShadows();
        if (ImGui::Checkbox("Cast Shadows", &castShadows)) {
            CommitComponentEdit(context, *actor, *light, "castShadows", [&] {
                light->SetCastShadows(castShadows);
            });
        }
        float shadowIntensity = light->GetShadowIntensity();
        if (ImGui::SliderFloat("Shadow Intensity", &shadowIntensity, 0.0f, 1.0f)) {
            CommitComponentEdit(context, *actor, *light, "shadowIntensity", [&] {
                light->SetShadowIntensity(shadowIntensity);
            });
        }
        Vec3 direction = light->GetDirection();
        if (DrawVec3("Direction", direction, 0.02f)) {
            CommitComponentEdit(context, *actor, *light, "direction", [&] {
                light->SetDirection(direction);
            });
        }
        if (EditorWidgets::IconButton("RemoveLight", "X", "Remove Light"))
            RemoveComponentByType(context, *actor, "Light");
        ImGui::PopID();
    }
};

class CameraInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "camera"; }
    int GetOrder() const override { return 340; }

    void Draw(EditorContext& context) override
    {
        Actor* actor = SelectedActor(context);
        auto* camera = actor ? actor->GetComponent<CameraComponent>() : nullptr;
        if (!camera) return;

        ImGui::Separator();
        ImGui::PushID("Camera");
        if (!SectionHeaderWithIcon(context, EditorIcons::Camera, "Camera")) {
            ImGui::PopID();
            return;
        }
        DrawEnabled(*camera);

        bool isMain = camera->IsMainCamera();
        if (ImGui::Checkbox("Main Camera", &isMain)) {
            CommitComponentEdit(context, *actor, *camera, "isMainCamera", [&] {
                camera->SetMainCamera(isMain);
            });
        }
        float fov = camera->GetFovYDegrees();
        if (ImGui::DragFloat("FOV Y", &fov, 0.25f, 1.0f, 179.0f)) {
            CommitComponentEdit(context, *actor, *camera, "fovYDegrees", [&] {
                camera->SetFovYDegrees(fov);
            });
        }
        float nearClip = camera->GetNearClip();
        if (ImGui::DragFloat("Near", &nearClip, 0.01f, 0.001f, 1000.0f)) {
            CommitComponentEdit(context, *actor, *camera, "nearClip", [&] {
                camera->SetNearClip(nearClip);
            });
        }
        float farClip = camera->GetFarClip();
        if (ImGui::DragFloat("Far", &farClip, 1.0f, 0.002f, 100000.0f)) {
            CommitComponentEdit(context, *actor, *camera, "farClip", [&] {
                camera->SetFarClip(farClip);
            });
        }
        Vec3 clear = camera->GetClearColor();
        float color[3] = {clear.x, clear.y, clear.z};
        if (ImGui::ColorEdit3("Clear Color", color)) {
            CommitComponentEdit(context, *actor, *camera, "clearColor", [&] {
                camera->SetClearColor({color[0], color[1], color[2]});
            });
        }
        if (EditorWidgets::IconButton("RemoveCamera", "X", "Remove Camera"))
            RemoveComponentByType(context, *actor, "Camera");
        ImGui::PopID();
    }
};

class PostProcessInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "postProcess"; }
    int GetOrder() const override { return 400; }

    void Draw(EditorContext& context) override
    {
        Actor* actor = SelectedActor(context);
        auto* post = actor ? actor->GetComponent<PostProcessComponent>() : nullptr;
        if (!post) return;

        ImGui::Separator();
        ImGui::PushID("PostProcess");
        if (!SectionHeaderWithIcon(context, EditorIcons::Renderer, "Post Process")) {
            ImGui::PopID();
            return;
        }
        float exposure = post->GetExposure();
        float gamma = post->GetGamma();
        float ssao = post->GetSSAOIntensity();
        float bloom = post->GetBloomIntensity();
        if (ImGui::DragFloat("Exposure", &exposure, 0.02f, 0.0f, 16.0f)) {
            CommitComponentEdit(context, *actor, *post, "exposure", [&] {
                post->SetExposure(exposure);
            });
        }
        if (ImGui::DragFloat("Gamma", &gamma, 0.01f, 0.1f, 8.0f)) {
            CommitComponentEdit(context, *actor, *post, "gamma", [&] {
                post->SetGamma(gamma);
            });
        }
        if (ImGui::DragFloat("SSAO", &ssao, 0.02f, 0.0f, 4.0f)) {
            CommitComponentEdit(context, *actor, *post, "ssaoIntensity", [&] {
                post->SetSSAOIntensity(ssao);
            });
        }
        if (ImGui::DragFloat("Bloom", &bloom, 0.02f, 0.0f, 8.0f)) {
            CommitComponentEdit(context, *actor, *post, "bloomIntensity", [&] {
                post->SetBloomIntensity(bloom);
            });
        }
        if (EditorWidgets::IconButton("RemovePostProcess", "X", "Remove Post Process")) {
            RemoveComponentByType(context, *actor, "PostProcess");
        }
        ImGui::PopID();
    }
};

class ScriptInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "script"; }
    int GetOrder() const override { return 500; }

    void Draw(EditorContext& context) override
    {
        Actor* actor = SelectedActor(context);
        auto* script = actor ? actor->GetComponent<ScriptComponent>() : nullptr;
        if (!script) return;

        ImGui::Separator();
        ImGui::PushID("Script");
        ImGui::Text("Script: %s", script->IsCompiled() ? "Compiled" : "Error");
        ImGui::Text("Language: %s", script->GetLanguage().c_str());
        ImGui::Text("Class: %s", script->GetClassName().c_str());
        ImGui::TextWrapped("%s", script->GetScriptPath().empty()
            ? "(inline)" : script->GetScriptPath().c_str());
        if (!script->GetLastError().empty()) {
            ImGui::TextWrapped("Error: %s", script->GetLastError().c_str());
        }
        const auto& fields = script->GetFields();
        if (!fields.empty()) {
            ImGui::TextUnformatted("Parameters");
            const nlohmann::json properties = script->GetProperties();
            for (const auto& field : fields) {
                ImGui::PushID(field.name.c_str());
                const nlohmann::json current = properties.contains(field.name)
                    ? properties[field.name] : field.defaultValue;
                nlohmann::json next;
                if (DrawScriptFieldValue(field, current, next)) {
                    nlohmann::json before = nlohmann::json::object();
                    script->Serialize(before);
                    script->SetPropertyValue(field.name, next);
                    nlohmann::json after = nlohmann::json::object();
                    script->Serialize(after);
                    script->Deserialize(before);
                    if (auto* operators = context.GetOperators()) {
                        operators->Components().SetProperty(
                            context, *actor, "Script", field.name, before, after);
                    } else {
                        EditorComponentOperator componentOperator;
                        componentOperator.SetProperty(
                            context, *actor, "Script", field.name, before, after);
                    }
                }
                if (!field.declaration.empty() && ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", field.declaration.c_str());
                }
                ImGui::PopID();
            }
        }
        if (ImGui::Button("Remove Script")) RemoveComponentByType(context, *actor, "Script");
        ImGui::PopID();
    }
};

class UICanvasInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "uiCanvas"; }
    int GetOrder() const override { return 520; }

    void Draw(EditorContext& context) override
    {
        Actor* actor = SelectedActor(context);
        auto* canvas = actor ? actor->GetComponent<UICanvasComponent>() : nullptr;
        if (!canvas) return;

        ImGui::Separator();
        ImGui::PushID("UICanvas");
        if (!SectionHeaderWithIcon(context, EditorIcons::Input, "UI Canvas")) {
            ImGui::PopID();
            return;
        }

        bool visible = canvas->IsVisible();
        if (ImGui::Checkbox("Visible", &visible)) {
            CommitComponentEdit(context, *actor, *canvas, "visible", [&] {
                canvas->SetVisible(visible);
            });
        }
        bool interactive = canvas->IsInteractive();
        if (ImGui::Checkbox("Interactive", &interactive)) {
            CommitComponentEdit(context, *actor, *canvas, "interactive", [&] {
                canvas->SetInteractive(interactive);
            });
        }
        int sortOrder = canvas->GetSortOrder();
        if (ImGui::DragInt("Sort Order", &sortOrder, 1.0f)) {
            CommitComponentEdit(context, *actor, *canvas, "sortOrder", [&] {
                canvas->SetSortOrder(sortOrder);
            });
        }
        int inputMode = static_cast<int>(canvas->GetInputMode());
        if (ImGui::Combo("Input Mode", &inputMode, "None\0UI Only\0Game And UI\0")) {
            CommitComponentEdit(context, *actor, *canvas, "inputMode", [&] {
                canvas->SetInputMode(static_cast<UIInputMode>(inputMode));
            });
        }
        int sourceMode = static_cast<int>(canvas->GetSourceMode());
        if (ImGui::Combo("Source Mode", &sourceMode, "Asset Document\0Actor Tree\0")) {
            CommitComponentEdit(context, *actor, *canvas, "sourceMode", [&] {
                canvas->SetSourceMode(static_cast<UICanvasSourceMode>(sourceMode));
            });
        }

        if (canvas->GetSourceMode() == UICanvasSourceMode::AssetDocument) {
            std::array<char, 260> document{};
            std::strncpy(document.data(), canvas->GetDocumentPath().c_str(), document.size() - 1);
            if (ImGui::InputText("Document", document.data(), document.size())) {
                CommitComponentEdit(context, *actor, *canvas, "documentPath", [&] {
                    canvas->SetDocumentPath(document.data());
                });
            }
        } else {
            auto generatedStyles = canvas->GetGeneratedStylePaths();
            std::string firstStyle = generatedStyles.empty() ? std::string{} : generatedStyles.front();
            if (DrawStringField("Generated Style", firstStyle)) {
                CommitComponentEdit(context, *actor, *canvas, "generatedStylePaths", [&] {
                    canvas->SetGeneratedStylePaths(firstStyle.empty()
                        ? std::vector<std::string>{}
                        : std::vector<std::string>{firstStyle});
                });
            }
        }
        if (ImGui::Button("Reload Document")) {
            canvas->Reload();
        }
        ImGui::SameLine();
        if (EditorWidgets::IconButton("RemoveUICanvas", "X", "Remove UI Canvas"))
            RemoveComponentByType(context, *actor, "UICanvas");
        ImGui::PopID();
    }
};

class UIRectTransformInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "uiRectTransform"; }
    int GetOrder() const override { return 530; }

    void Draw(EditorContext& context) override
    {
        Actor* actor = SelectedActor(context);
        auto* rect = actor ? actor->GetComponent<UIRectTransformComponent>() : nullptr;
        if (!rect) return;

        ImGui::Separator();
        ImGui::PushID("UIRectTransform");
        if (!SectionHeaderWithIcon(context, EditorIcons::Actor, "UI Rect Transform")) {
            ImGui::PopID();
            return;
        }
        RectTransform next = rect->GetRect();
        bool changed = false;
        changed |= DrawVec2Field("Anchor Min", next.anchorMin);
        changed |= DrawVec2Field("Anchor Max", next.anchorMax);
        changed |= DrawVec2Field("Offset Min", next.offsetMin);
        changed |= DrawVec2Field("Offset Max", next.offsetMax);
        changed |= DrawVec2Field("Pivot", next.pivot);
        if (changed) {
            CommitComponentEdit(context, *actor, *rect, "Rect Transform", [&]() {
                rect->GetRect() = next;
            });
        }
        if (EditorWidgets::IconButton("RemoveUIRectTransform", "X", "Remove UI Rect Transform")) {
            RemoveComponentByType(context, *actor, "UIRectTransform");
        }
        ImGui::PopID();
    }
};

class UIWidgetInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "uiWidget"; }
    int GetOrder() const override { return 540; }

    void Draw(EditorContext& context) override
    {
        Actor* actor = SelectedActor(context);
        if (!actor) return;

        DrawText(context, *actor);
        DrawImage(context, *actor);
        DrawButton(context, *actor);
        DrawSlider(context, *actor);
        DrawProgress(context, *actor);
        DrawScrollView(context, *actor);
    }

private:
    template <typename T>
    bool DrawCommon(EditorContext& context, Actor& actor, T& component, const char* label)
    {
        ImGui::Separator();
        ImGui::PushID(label);
        if (!SectionHeaderWithIcon(context, EditorIcons::Input, label)) {
            ImGui::PopID();
            return false;
        }
        bool changed = false;
        std::string elementId = component.GetElementID();
        std::string className = component.GetClassName();
        changed |= DrawStringField("Element ID", elementId);
        changed |= DrawStringField("Class", className);
        if (changed) {
            CommitComponentEdit(context, actor, component, label, [&]() {
                component.SetElementID(elementId);
                component.SetClassName(className);
            });
        }
        return true;
    }

    void DrawText(EditorContext& context, Actor& actor)
    {
        auto* component = actor.GetComponent<UITextComponent>();
        if (!component) return;
        if (!DrawCommon(context, actor, *component, "UI Text")) return;
        std::string text = component->text;
        float fontSize = component->fontSize;
        Color color = component->color;
        bool changed = false;
        changed |= DrawStringField("Text", text);
        changed |= ImGui::DragFloat("Font Size", &fontSize, 1.0f, 1.0f, 256.0f);
        changed |= DrawColorField("Color", color);
        if (changed) {
            CommitComponentEdit(context, actor, *component, "Text", [&]() {
                component->text = text;
                component->fontSize = fontSize;
                component->color = color;
            });
        }
        ImGui::PopID();
    }

    void DrawImage(EditorContext& context, Actor& actor)
    {
        auto* component = actor.GetComponent<UIImageComponent>();
        if (!component) return;
        if (!DrawCommon(context, actor, *component, "UI Image")) return;
        std::string source = component->source;
        Color tint = component->tint;
        bool changed = DrawStringField("Source", source);
        changed |= DrawColorField("Tint", tint);
        if (changed) {
            CommitComponentEdit(context, actor, *component, "Image", [&]() {
                component->source = source;
                component->tint = tint;
            });
        }
        ImGui::PopID();
    }

    void DrawButton(EditorContext& context, Actor& actor)
    {
        auto* component = actor.GetComponent<UIButtonComponent>();
        if (!component) return;
        if (!DrawCommon(context, actor, *component, "UI Button")) return;
        std::string text = component->text;
        bool disabled = component->disabled;
        bool changed = DrawStringField("Text", text);
        changed |= ImGui::Checkbox("Disabled", &disabled);
        if (changed) {
            CommitComponentEdit(context, actor, *component, "Button", [&]() {
                component->text = text;
                component->disabled = disabled;
            });
        }
        ImGui::PopID();
    }

    void DrawSlider(EditorContext& context, Actor& actor)
    {
        auto* component = actor.GetComponent<UISliderComponent>();
        if (!component) return;
        if (!DrawCommon(context, actor, *component, "UI Slider")) return;
        float value = component->value;
        float min = component->min;
        float max = component->max;
        float step = component->step;
        std::string binding = component->dataBinding;
        bool changed = ImGui::DragFloat("Value", &value, 0.01f);
        changed |= ImGui::DragFloat("Min", &min, 0.01f);
        changed |= ImGui::DragFloat("Max", &max, 0.01f);
        changed |= ImGui::DragFloat("Step", &step, 0.01f, 0.001f, 100.0f);
        changed |= DrawStringField("Data Binding", binding);
        if (changed) {
            CommitComponentEdit(context, actor, *component, "Slider", [&]() {
                component->value = value;
                component->min = min;
                component->max = max;
                component->step = step;
                component->dataBinding = binding;
            });
        }
        ImGui::PopID();
    }

    void DrawProgress(EditorContext& context, Actor& actor)
    {
        auto* component = actor.GetComponent<UIProgressBarComponent>();
        if (!component) return;
        if (!DrawCommon(context, actor, *component, "UI Progress Bar")) return;
        float value = component->value;
        float max = component->max;
        bool changed = ImGui::DragFloat("Value", &value, 0.01f);
        changed |= ImGui::DragFloat("Max", &max, 0.01f, 0.001f, 100000.0f);
        if (changed) {
            CommitComponentEdit(context, actor, *component, "Progress Bar", [&]() {
                component->value = value;
                component->max = max;
            });
        }
        ImGui::PopID();
    }

    void DrawScrollView(EditorContext& context, Actor& actor)
    {
        auto* component = actor.GetComponent<UIScrollViewComponent>();
        if (!component) return;
        if (!DrawCommon(context, actor, *component, "UI Scroll View")) return;
        bool horizontal = component->horizontal;
        bool vertical = component->vertical;
        bool changed = ImGui::Checkbox("Horizontal", &horizontal);
        changed |= ImGui::Checkbox("Vertical", &vertical);
        if (changed) {
            CommitComponentEdit(context, actor, *component, "Scroll View", [&]() {
                component->horizontal = horizontal;
                component->vertical = vertical;
            });
        }
        ImGui::PopID();
    }
};

class UILayoutInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "uiLayout"; }
    int GetOrder() const override { return 550; }

    void Draw(EditorContext& context) override
    {
        Actor* actor = SelectedActor(context);
        if (!actor) return;
        if (auto* layout = actor->GetComponent<UIVerticalLayoutComponent>()) {
            DrawLayout(context, *actor, *layout, "UI Vertical Layout");
        }
        if (auto* layout = actor->GetComponent<UIHorizontalLayoutComponent>()) {
            DrawLayout(context, *actor, *layout, "UI Horizontal Layout");
        }
        if (auto* grid = actor->GetComponent<UIGridLayoutComponent>()) {
            DrawGrid(context, *actor, *grid);
        }
    }

private:
    template <typename T>
    bool DrawLayout(EditorContext& context, Actor& actor, T& layout, const char* label)
    {
        ImGui::Separator();
        ImGui::PushID(label);
        if (!SectionHeaderWithIcon(context, EditorIcons::Input, label)) {
            ImGui::PopID();
            return false;
        }
        float spacing = layout.spacing;
        float padding = layout.padding;
        std::string alignItems = layout.alignItems;
        std::string justifyContent = layout.justifyContent;
        std::string className = layout.GetClassName();
        bool changed = ImGui::DragFloat("Spacing", &spacing, 1.0f, 0.0f, 256.0f);
        changed |= ImGui::DragFloat("Padding", &padding, 1.0f, 0.0f, 256.0f);
        changed |= DrawStringField("Align Items", alignItems);
        changed |= DrawStringField("Justify Content", justifyContent);
        changed |= DrawStringField("Class", className);
        if (changed) {
            CommitComponentEdit(context, actor, layout, label, [&]() {
                layout.spacing = spacing;
                layout.padding = padding;
                layout.alignItems = alignItems;
                layout.justifyContent = justifyContent;
                layout.SetClassName(className);
            });
        }
        ImGui::PopID();
        return true;
    }

    void DrawGrid(EditorContext& context, Actor& actor, UIGridLayoutComponent& grid)
    {
        if (!DrawLayout(context, actor, grid, "UI Grid Layout")) return;
        int columns = grid.columns;
        int rows = grid.rows;
        float gap = grid.gap;
        bool changed = ImGui::DragInt("Columns", &columns, 1.0f, 1, 64);
        changed |= ImGui::DragInt("Rows", &rows, 1.0f, 1, 64);
        changed |= ImGui::DragFloat("Gap", &gap, 1.0f, 0.0f, 256.0f);
        if (changed) {
            CommitComponentEdit(context, actor, grid, "Grid", [&]() {
                grid.columns = columns;
                grid.rows = rows;
                grid.gap = gap;
            });
        }
    }
};

class AddComponentInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "addComponent"; }
    int GetOrder() const override { return 1000; }

    void Draw(EditorContext& context) override
    {
        LoadRecentComponents(context);
        Actor* actor = SelectedActor(context);
        if (!actor) return;

        ImGui::Separator();
        ImGui::SetNextWindowViewport(ImGui::GetWindowViewport()->ID);
        if (!ImGui::BeginCombo("##AddComponent", "Add Component...")) return;

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##AddComponentSearch", "Search components...",
                                 m_ComponentSearch, sizeof(m_ComponentSearch));
        ImGui::Separator();

        std::vector<std::string> types = ComponentRegistry::Get().GetRegisteredTypes();
        std::sort(types.begin(), types.end(), [](const std::string& left,
                                                 const std::string& right) {
            const std::string leftCategory = ComponentCategory(left);
            const std::string rightCategory = ComponentCategory(right);
            if (leftCategory != rightCategory) return leftCategory < rightCategory;
            return ComponentDisplayName(left) < ComponentDisplayName(right);
        });

        auto addRegisteredComponent = [&](const std::string& type) {
            const bool exists = actor->HasComponentType(type);
            if (exists) ImGui::BeginDisabled();
            const std::string label = ComponentDisplayName(type);
            if (ImGui::Selectable(label.c_str()) && !exists) {
                AddComponentByType(context, *actor, type, nlohmann::json::object());
                RecordRecentComponent(context, type);
                if (auto* renderer = actor->GetComponent<MeshRendererComponent>()) {
                    if (!renderer->GetMesh()) renderer->SetMesh(AssetManager::Get().GetCubeMesh());
                    if (!renderer->GetMaterial()) {
                        renderer->SetMaterial(AssetManager::Get().GetDefaultMaterial());
                    }
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", type.c_str());
            }
            if (exists) ImGui::EndDisabled();
        };

        if (!m_RecentComponents.empty()) {
            if (ImGui::BeginMenu("Recently Used")) {
                for (const std::string& type : m_RecentComponents) {
                    if (!ComponentRegistry::Get().IsRegistered(type) ||
                        !ComponentMatchesFilter(type, m_ComponentSearch)) {
                        continue;
                    }
                    addRegisteredComponent(type);
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
        }

        constexpr std::array<const char*, 6> kCategories = {
            "Rendering", "Physics", "Audio", "Scripting", "UI", "Gameplay"
        };
        for (const char* category : kCategories) {
            bool hasVisible = false;
            for (const std::string& type : types) {
                if (std::strcmp(ComponentCategory(type), category) == 0 &&
                    ComponentMatchesFilter(type, m_ComponentSearch)) {
                    hasVisible = true;
                    break;
                }
            }
            if (!hasVisible) continue;
            if (!ImGui::BeginMenu(category)) continue;
            for (const std::string& type : types) {
                if (std::strcmp(ComponentCategory(type), category) != 0 ||
                    !ComponentMatchesFilter(type, m_ComponentSearch)) {
                    continue;
                }
                addRegisteredComponent(type);
            }
            ImGui::EndMenu();
        }

        if (auto* registry = context.GetAssetRegistry()) {
            const bool hasScript = actor->HasComponent<ScriptComponent>();
            const auto scripts = registry->GetAssets(EditorAssetType::Script);
            if (!scripts.empty() && ImGui::BeginMenu("Scripts")) {
                for (const auto& scriptInfo : scripts) {
                    if (scriptInfo.absolutePath.extension() != ".as") continue;
                    auto scriptAsset = AssetManager::Get().Load<ScriptAsset>(scriptInfo.absolutePath.string());
                    if (!scriptAsset || !scriptAsset.Get()) continue;
                    const std::string scriptLabel = scriptInfo.absolutePath.stem().string();
                    const bool scriptFileMatches =
                        ContainsCaseInsensitive(scriptLabel, m_ComponentSearch) ||
                        ContainsCaseInsensitive(scriptInfo.relativePath, m_ComponentSearch);
                    bool hasMatchingClass = false;
                    for (const auto& scriptClass : scriptAsset->GetClasses()) {
                        if (scriptFileMatches ||
                            ContainsCaseInsensitive(scriptClass.name, m_ComponentSearch)) {
                            hasMatchingClass = true;
                            break;
                        }
                    }
                    if (!hasMatchingClass) continue;
                    if (ImGui::BeginMenu(scriptLabel.c_str())) {
                        for (const auto& scriptClass : scriptAsset->GetClasses()) {
                            if (!scriptFileMatches &&
                                !ContainsCaseInsensitive(scriptClass.name, m_ComponentSearch)) {
                                continue;
                            }
                            if (hasScript) ImGui::BeginDisabled();
                            if (ImGui::Selectable(scriptClass.name.c_str()) && !hasScript) {
                                nlohmann::json properties = nlohmann::json::object();
                                for (const auto& field : scriptClass.fields) {
                                    properties[field.name] = field.defaultValue;
                                }
                                nlohmann::json initialData = {
                                    {"language", "angelscript"},
                                    {"scriptPath", AssetManager::Get().MakeProjectRelativePath(scriptInfo.absolutePath.string())},
                                    {"className", scriptClass.name},
                                    {"properties", properties},
                                    {"state", nlohmann::json::object()}
                                };
                                AddComponentByType(context, *actor, "Script", initialData);
                                RecordRecentComponent(context, "Script");
                            }
                            if (hasScript) ImGui::EndDisabled();
                        }
                        if (!scriptAsset->GetLastError().empty()) {
                            ImGui::TextDisabled("%s", scriptAsset->GetLastError().c_str());
                        }
                        ImGui::EndMenu();
                    }
                }
                ImGui::EndMenu();
            }
        }
        ImGui::EndCombo();
    }

private:
    static constexpr const char* kInspectorPanelStateID = "inspector";
    static constexpr const char* kRecentComponentsKey = "addComponentRecent";

    void LoadRecentComponents(EditorContext& context)
    {
        if (m_RecentComponentsLoaded) return;
        m_RecentComponentsLoaded = true;
        EditorWorkspace* workspace = context.GetWorkspace();
        if (!workspace) return;
        const auto value = workspace->GetPanelStateValue(
            kInspectorPanelStateID, kRecentComponentsKey);
        if (!value) return;

        size_t start = 0;
        while (start <= value->size()) {
            const size_t separator = value->find(';', start);
            std::string type = value->substr(
                start, separator == std::string::npos ? std::string::npos : separator - start);
            if (!type.empty() && ComponentRegistry::Get().IsRegistered(type) &&
                std::find(m_RecentComponents.begin(), m_RecentComponents.end(), type) ==
                    m_RecentComponents.end()) {
                m_RecentComponents.push_back(std::move(type));
            }
            if (separator == std::string::npos || m_RecentComponents.size() >= 5) break;
            start = separator + 1;
        }
    }

    void SaveRecentComponents(EditorContext& context) const
    {
        EditorWorkspace* workspace = context.GetWorkspace();
        if (!workspace) return;
        std::string serialized;
        for (const std::string& type : m_RecentComponents) {
            if (!serialized.empty()) serialized += ';';
            serialized += type;
        }
        workspace->SetPanelStateValue(
            kInspectorPanelStateID, kRecentComponentsKey, std::move(serialized));
    }

    void RecordRecentComponent(EditorContext& context, const std::string& type)
    {
        m_RecentComponents.erase(
            std::remove(m_RecentComponents.begin(), m_RecentComponents.end(), type),
            m_RecentComponents.end());
        m_RecentComponents.insert(m_RecentComponents.begin(), type);
        if (m_RecentComponents.size() > 5) m_RecentComponents.resize(5);
        SaveRecentComponents(context);
    }

    char m_ComponentSearch[128] = {};
    bool m_RecentComponentsLoaded = false;
    std::vector<std::string> m_RecentComponents;
};

void RegisterAssetInspectorSections(std::vector<std::unique_ptr<EditorInspectorSection>>& sections)
{
    sections.push_back(std::make_unique<ModelAssetInspectorSection>());
    sections.push_back(std::make_unique<PrefabAssetInspectorSection>());
    sections.push_back(std::make_unique<MaterialAssetInspectorSection>());
    sections.push_back(std::make_unique<TextureAssetInspectorSection>());
    sections.push_back(std::make_unique<AudioAssetInspectorSection>());
    sections.push_back(std::make_unique<GenericAssetInspectorSection>());
}

void RegisterTransformInspectorSections(std::vector<std::unique_ptr<EditorInspectorSection>>& sections)
{
    sections.push_back(std::make_unique<TransformInspectorSection>());
}

void RegisterRenderInspectorSections(std::vector<std::unique_ptr<EditorInspectorSection>>& sections)
{
    sections.push_back(std::make_unique<MeshRendererInspectorSection>());
    sections.push_back(std::make_unique<SkinnedMeshInspectorSection>());
    sections.push_back(std::make_unique<AnimatorInspectorSection>());
    sections.push_back(std::make_unique<ThirdPersonCameraInspectorSection>());
    sections.push_back(std::make_unique<GameplayInspectorSection>());
    sections.push_back(std::make_unique<MaterialInspectorSection>());
    sections.push_back(std::make_unique<CameraInspectorSection>());
    sections.push_back(std::make_unique<LightInspectorSection>());
    sections.push_back(std::make_unique<PostProcessInspectorSection>());
}

void RegisterAudioInspectorSections(std::vector<std::unique_ptr<EditorInspectorSection>>& sections)
{
    sections.push_back(std::make_unique<AudioSourceInspectorSection>());
}

void RegisterPhysicsInspectorSections(std::vector<std::unique_ptr<EditorInspectorSection>>& sections)
{
    sections.push_back(std::make_unique<PhysicsInspectorSection>());
}

void RegisterScriptingInspectorSections(std::vector<std::unique_ptr<EditorInspectorSection>>& sections)
{
    sections.push_back(std::make_unique<ScriptInspectorSection>());
}

void RegisterUIInspectorSections(std::vector<std::unique_ptr<EditorInspectorSection>>& sections)
{
    sections.push_back(std::make_unique<UICanvasInspectorSection>());
    sections.push_back(std::make_unique<UIRectTransformInspectorSection>());
    sections.push_back(std::make_unique<UIWidgetInspectorSection>());
    sections.push_back(std::make_unique<UILayoutInspectorSection>());
}
} // namespace

std::vector<std::unique_ptr<EditorInspectorSection>> CreateDefaultInspectorSections()
{
    std::vector<std::unique_ptr<EditorInspectorSection>> sections;
    sections.push_back(std::make_unique<SceneSettingsInspectorSection>());
    RegisterAssetInspectorSections(sections);
    RegisterTransformInspectorSections(sections);
    RegisterRenderInspectorSections(sections);
    RegisterAudioInspectorSections(sections);
    RegisterPhysicsInspectorSections(sections);
    RegisterScriptingInspectorSections(sections);
    RegisterUIInspectorSections(sections);
    sections.push_back(std::make_unique<AddComponentInspectorSection>());
    return sections;
}
