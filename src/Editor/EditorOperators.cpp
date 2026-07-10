#include "Editor/EditorOperators.h"

#include "Assets/Asset.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetManager.h"
#include "Assets/AssetMeta.h"
#include "Assets/ModelAsset.h"
#include "Camera/Camera.h"
#include "Camera/CameraComponent.h"
#include "Editor/EditorAction.h"
#include "Editor/EditorAssetRegistry.h"
#include "Editor/EditorCommand.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorImportService.h"
#include "Editor/EditorProject.h"
#include "Editor/EditorProfiler.h"
#include "Editor/EditorService.h"
#include "Core/Logger.h"
#include "Game/GameViewport.h"
#include "Game/SceneRenderLayer.h"
#include "Game/SceneViewportController.h"
#include "Scene/Actor.h"
#include "Scene/ActorSubtreeSerializer.h"
#include "Scene/Component.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/MeshRendererComponent.h"
#include "Scene/PrefabSystem.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"
#include "UI/Core/UICanvasComponent.h"
#include "UI/Core/UIComponents.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iterator>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <unordered_map>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace {

using EditorOperatorClock = std::chrono::steady_clock;

double ElapsedOperatorMs(EditorOperatorClock::time_point start)
{
    return std::chrono::duration<double, std::milli>(
        EditorOperatorClock::now() - start).count();
}

void RecordAssetOperatorEvent(EditorContext& context, const char* operation,
                              const std::string& path, double durationMs,
                              bool success, std::string details = {})
{
    std::string eventDetails = "path=" + path;
    eventDetails += success ? ";success=true" : ";success=false";
    if (!details.empty()) {
        eventDetails += ";";
        eventDetails += std::move(details);
    }
    if (EditorProfiler* profiler = context.GetProfiler()) {
        profiler->RecordEvent("EditorAsset", operation, durationMs,
                              std::move(eventDetails));
    }
    if (!success) {
        Logger::Warn("[EditorAsset] ", operation, " failed: ", path);
    }
}

void RecordPrefabOperatorEvent(EditorContext& context, const char* operation,
                               uint64_t actorID, double durationMs,
                               bool success, std::string details = {})
{
    std::string eventDetails = "actorID=" + std::to_string(actorID);
    eventDetails += success ? ";success=true" : ";success=false";
    if (!details.empty()) {
        eventDetails += ";";
        eventDetails += std::move(details);
    }
    if (EditorProfiler* profiler = context.GetProfiler()) {
        profiler->RecordEvent("EditorPrefab", operation, durationMs,
                              std::move(eventDetails));
    }
    if (!success) {
        Logger::Warn("[EditorPrefab] ", operation, " failed: actorID=", actorID);
    }
}

EditorSelectionMode ToSelectionMode(EditorSelectionIntentMode mode)
{
    switch (mode) {
        case EditorSelectionIntentMode::Add: return EditorSelectionMode::Add;
        case EditorSelectionIntentMode::Toggle: return EditorSelectionMode::Toggle;
        default: return EditorSelectionMode::Replace;
    }
}

enum class EditorUIPreset {
    Canvas,
    Text,
    Image,
    Button,
    Slider,
    ProgressBar,
    ScrollView,
    VerticalLayout,
    HorizontalLayout,
    GridLayout,
};

std::optional<EditorUIPreset> EditorUIPresetFromID(const std::string& id)
{
    if (id == "canvas") return EditorUIPreset::Canvas;
    if (id == "text") return EditorUIPreset::Text;
    if (id == "image") return EditorUIPreset::Image;
    if (id == "button") return EditorUIPreset::Button;
    if (id == "slider") return EditorUIPreset::Slider;
    if (id == "progressBar") return EditorUIPreset::ProgressBar;
    if (id == "scrollView") return EditorUIPreset::ScrollView;
    if (id == "verticalLayout") return EditorUIPreset::VerticalLayout;
    if (id == "horizontalLayout") return EditorUIPreset::HorizontalLayout;
    if (id == "gridLayout") return EditorUIPreset::GridLayout;
    return std::nullopt;
}

const char* EditorUIPresetName(EditorUIPreset preset)
{
    switch (preset) {
    case EditorUIPreset::Canvas: return "UI Canvas";
    case EditorUIPreset::Text: return "Text";
    case EditorUIPreset::Image: return "Image";
    case EditorUIPreset::Button: return "Button";
    case EditorUIPreset::Slider: return "Slider";
    case EditorUIPreset::ProgressBar: return "Progress Bar";
    case EditorUIPreset::ScrollView: return "Scroll View";
    case EditorUIPreset::VerticalLayout: return "Vertical Layout";
    case EditorUIPreset::HorizontalLayout: return "Horizontal Layout";
    case EditorUIPreset::GridLayout: return "Grid Layout";
    }
    return "UI Actor";
}

void ConfigureUIRect(UIRectTransformComponent& rect, EditorUIPreset preset)
{
    RectTransform& value = rect.GetRect();
    if (preset == EditorUIPreset::Canvas) {
        value.anchorMin = {0.0f, 0.0f};
        value.anchorMax = {1.0f, 1.0f};
        value.offsetMin = {0.0f, 0.0f};
        value.offsetMax = {0.0f, 0.0f};
        return;
    }
    value.anchorMin = {0.0f, 0.0f};
    value.anchorMax = {0.0f, 0.0f};
    value.offsetMin = {24.0f, 24.0f};
    value.offsetMax = {224.0f, 72.0f};
}

void AddUIPresetComponents(Actor& actor, EditorUIPreset preset)
{
    if (auto* rect = actor.AddComponent<UIRectTransformComponent>()) {
        ConfigureUIRect(*rect, preset);
    }
    switch (preset) {
    case EditorUIPreset::Canvas: {
        if (auto* canvas = actor.AddComponent<UICanvasComponent>()) {
            canvas->SetSourceMode(UICanvasSourceMode::ActorTree);
            canvas->SetDefaultFontPaths({"Content/UI/Fonts/LatoLatin-Regular.ttf"});
            canvas->SetGeneratedStylePaths({"Content/UI/RmlExample.rcss"});
        }
        break;
    }
    case EditorUIPreset::Text: {
        auto* text = actor.AddComponent<UITextComponent>();
        if (text) text->text = "Text";
        break;
    }
    case EditorUIPreset::Image:
        actor.AddComponent<UIImageComponent>();
        break;
    case EditorUIPreset::Button: {
        auto* button = actor.AddComponent<UIButtonComponent>();
        if (button) button->text = "Button";
        break;
    }
    case EditorUIPreset::Slider:
        actor.AddComponent<UISliderComponent>();
        break;
    case EditorUIPreset::ProgressBar:
        actor.AddComponent<UIProgressBarComponent>();
        break;
    case EditorUIPreset::ScrollView:
        actor.AddComponent<UIScrollViewComponent>();
        break;
    case EditorUIPreset::VerticalLayout:
        actor.AddComponent<UIVerticalLayoutComponent>();
        break;
    case EditorUIPreset::HorizontalLayout:
        actor.AddComponent<UIHorizontalLayoutComponent>();
        break;
    case EditorUIPreset::GridLayout:
        actor.AddComponent<UIGridLayoutComponent>();
        break;
    }
}

std::string ReadFileContent(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

bool WriteFileContent(const std::filesystem::path& path, const std::string& content)
{
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    return static_cast<bool>(out);
}

bool EnsureAssetMeta(const std::filesystem::path& path)
{
    if (std::filesystem::exists(AssetMeta::MetaPathFor(path.string()))) return true;
    AssetMeta meta = AssetMeta::Create(path.string());
    return AssetMeta::Save(meta);
}

struct FolderSnapshotFile {
    std::filesystem::path path;
    std::string content;
};

struct FolderSnapshot {
    std::filesystem::path root;
    std::vector<std::filesystem::path> directories;
    std::vector<FolderSnapshotFile> files;
};

bool CaptureFolderSnapshot(const std::filesystem::path& folder,
                           FolderSnapshot& snapshot)
{
    std::error_code error;
    if (!std::filesystem::is_directory(folder, error) || error) return false;
    snapshot.root = folder;
    snapshot.directories.clear();
    snapshot.files.clear();
    snapshot.directories.push_back(folder);

    for (std::filesystem::recursive_directory_iterator it(folder, error), end;
         !error && it != end; it.increment(error)) {
        const std::filesystem::path path = it->path();
        if (it->is_directory(error)) {
            snapshot.directories.push_back(path);
        } else if (it->is_regular_file(error)) {
            snapshot.files.push_back({path, ReadFileContent(path)});
        }
    }
    return !error;
}

void RefreshAssetRegistryIfPresent(EditorContext& context)
{
    if (EditorAssetRegistry* registry = context.GetAssetRegistry()) {
        registry->Refresh();
    }
}

bool RestoreFolderSnapshot(const FolderSnapshot& snapshot)
{
    std::error_code error;
    for (const auto& directory : snapshot.directories) {
        std::filesystem::create_directories(directory, error);
        if (error) return false;
    }
    for (const auto& file : snapshot.files) {
        if (!WriteFileContent(file.path, file.content)) return false;
    }
    return true;
}

bool DeleteFolderSnapshot(const FolderSnapshot& snapshot)
{
    std::error_code error;
    for (const auto& file : snapshot.files) {
        if (std::filesystem::exists(file.path, error)) {
            AssetManager::Get().Unload(file.path.string());
            std::filesystem::remove(file.path, error);
            if (error) {
                Logger::Warn("[EditorAsset] Failed to delete file in folder snapshot: ",
                             file.path.string(), " (", error.message(), ")");
                return false;
            }
        }
    }
    for (auto it = snapshot.directories.rbegin();
         it != snapshot.directories.rend(); ++it) {
        if (std::filesystem::exists(*it, error)) {
            std::filesystem::remove(*it, error);
            if (error) {
                Logger::Warn("[EditorAsset] Failed to delete directory in folder snapshot: ",
                             it->string(), " (", error.message(), ")");
                return false;
            }
        }
    }
    return true;
}

std::filesystem::path ProjectRootForAssets(const EditorContext& context)
{
    if (!context.GetProjectRoot().empty()) return context.GetProjectRoot();
    if (const EditorAssetRegistry* registry = context.GetAssetRegistry()) {
        const std::filesystem::path& root = registry->GetRoot();
        if (!root.empty()) return root.parent_path();
    }
    return {};
}

std::filesystem::path AssetDatabasePathFor(const EditorContext& context)
{
    const std::filesystem::path projectRoot = ProjectRootForAssets(context);
    return projectRoot.empty()
        ? std::filesystem::path{}
        : projectRoot / ".myengine" / "AssetDatabase.json";
}

std::filesystem::path AssetDatabaseAbsolutePathFor(const std::filesystem::path& path,
                                                   const std::filesystem::path& projectRoot)
{
    std::error_code error;
    const std::filesystem::path value = path.is_absolute() || projectRoot.empty()
        ? path
        : projectRoot / path;
    std::filesystem::path absolute = std::filesystem::absolute(value, error);
    if (error) absolute = value;
    return absolute.lexically_normal();
}

bool AssetDatabasePathUnderOrEqual(const std::filesystem::path& path,
                                   const std::filesystem::path& root)
{
    const std::filesystem::path candidate = path.lexically_normal();
    const std::filesystem::path base = root.lexically_normal();
    if (candidate == base) return true;
    const std::filesystem::path relative = candidate.lexically_relative(base);
    if (relative.empty()) return false;
    const auto first = *relative.begin();
    return first != ".." && first != ".";
}

bool AssetDatabaseRecordUnderRoot(const AssetRecord& record,
                                  const std::filesystem::path& root,
                                  const std::filesystem::path& projectRoot)
{
    auto matches = [&](const std::string& value) {
        if (value.empty()) return false;
        return AssetDatabasePathUnderOrEqual(
            AssetDatabaseAbsolutePathFor(value, projectRoot),
            AssetDatabaseAbsolutePathFor(root, projectRoot));
    };
    return matches(record.sourcePath) || matches(record.artifactPath);
}

std::vector<AssetRecord> CaptureAssetDatabaseRecordsUnderRoot(
    EditorContext& context, const std::filesystem::path& root)
{
    const std::filesystem::path projectRoot = ProjectRootForAssets(context);
    const std::filesystem::path databasePath = AssetDatabasePathFor(context);
    if (projectRoot.empty() || databasePath.empty() ||
        !std::filesystem::exists(databasePath)) {
        return {};
    }

    AssetDatabase database;
    std::string error;
    if (!database.Open(databasePath, &error)) {
        Logger::Warn("[EditorAsset] Failed to open asset database for folder delete: ",
                     error);
        return {};
    }

    std::vector<AssetRecord> records;
    for (const AssetRecord& record : database.GetAll()) {
        if (AssetDatabaseRecordUnderRoot(record, root, projectRoot)) {
            records.push_back(record);
        }
    }
    return records;
}

bool RemoveAssetDatabaseRecords(EditorContext& context,
                                const std::vector<AssetRecord>& records)
{
    if (records.empty()) return true;
    const std::filesystem::path databasePath = AssetDatabasePathFor(context);
    if (databasePath.empty() || !std::filesystem::exists(databasePath)) return true;

    AssetDatabase database;
    std::string error;
    if (!database.Open(databasePath, &error)) {
        Logger::Warn("[EditorAsset] Failed to open asset database for folder delete: ",
                     error);
        return false;
    }
    bool changed = false;
    for (const AssetRecord& record : records) {
        changed = database.Remove(record.uuid) || changed;
    }
    if (!changed) return true;
    if (!database.Save(&error)) {
        Logger::Warn("[EditorAsset] Failed to save asset database after folder delete: ",
                     error);
        return false;
    }
    return true;
}

bool RestoreAssetDatabaseRecords(EditorContext& context,
                                 const std::vector<AssetRecord>& records)
{
    if (records.empty()) return true;
    const std::filesystem::path databasePath = AssetDatabasePathFor(context);
    if (databasePath.empty()) return true;

    AssetDatabase database;
    std::string error;
    if (!database.Open(databasePath, &error)) {
        Logger::Warn("[EditorAsset] Failed to open asset database for folder delete undo: ",
                     error);
        return false;
    }
    for (AssetRecord record : records) {
        if (!database.Upsert(std::move(record), &error)) {
            Logger::Warn("[EditorAsset] Failed to restore asset database record: ",
                         error);
            return false;
        }
    }
    if (!database.Save(&error)) {
        Logger::Warn("[EditorAsset] Failed to save asset database after folder delete undo: ",
                     error);
        return false;
    }
    return true;
}

bool ReadImportSettings(EditorContext& context, const std::string& uuid,
                        std::string& settingsJson)
{
    if (uuid.empty()) return false;
    const std::filesystem::path databasePath = AssetDatabasePathFor(context);
    if (databasePath.empty()) return false;
    AssetDatabase database;
    if (!database.Open(databasePath)) return false;
    const AssetRecord* record = database.FindByUuid(uuid);
    if (!record) return false;
    settingsJson = record->settingsJson.empty() ? std::string("{}") : record->settingsJson;
    return true;
}

bool ApplyImportSettings(EditorContext& context, const std::string& uuid,
                         const std::string& settingsJson)
{
    EditorImportService* importer = context.GetService<EditorImportService>();
    if (!importer) return false;
    const bool ok = importer->ReimportWithSettings(uuid, settingsJson);
    if (ok && context.GetAssetRegistry()) context.GetAssetRegistry()->Refresh();
    return ok;
}

void RemoveFileIfExists(const std::filesystem::path& path)
{
    std::error_code error;
    std::filesystem::remove(path, error);
}

void RemoveAssetFileAndMeta(const std::filesystem::path& path)
{
    RemoveFileIfExists(path);
    RemoveFileIfExists(AssetMeta::MetaPathFor(path.string()));
}

bool AssetRenameTargetExists(const std::filesystem::path& source,
                             const std::filesystem::path& target)
{
    std::error_code error;
    if (std::filesystem::exists(target, error)) return true;
    error.clear();
    if (std::filesystem::is_regular_file(source, error) &&
        std::filesystem::exists(AssetMeta::MetaPathFor(target.string()))) {
        return true;
    }
    return false;
}

uint64_t NextSiblingID(const Actor& actor)
{
    Scene* scene = actor.GetScene();
    if (!scene) return 0;
    const auto& siblings = actor.GetParent()
        ? actor.GetParent()->GetChildren()
        : scene->GetRootActors();
    for (size_t index = 0; index < siblings.size(); ++index) {
        if (siblings[index] == &actor) {
            const size_t next = index + 1;
            return next < siblings.size() && siblings[next] ? siblings[next]->GetID() : 0;
        }
    }
    return 0;
}

uint64_t PreviousSiblingID(const Actor& actor)
{
    Scene* scene = actor.GetScene();
    if (!scene) return 0;
    const auto& siblings = actor.GetParent()
        ? actor.GetParent()->GetChildren()
        : scene->GetRootActors();
    Actor* previous = nullptr;
    for (Actor* sibling : siblings) {
        if (sibling == &actor) return previous ? previous->GetID() : 0;
        if (sibling) previous = sibling;
    }
    return 0;
}

uint64_t FindNextActorID(const Scene& scene)
{
    uint64_t maxID = 0;
    scene.ForEach([&](const Actor& actor) {
        if (actor.GetID() > maxID) maxID = actor.GetID();
    });
    return maxID + 1;
}

std::filesystem::path NormalizeAbsolute(const std::filesystem::path& path)
{
    std::error_code error;
    return std::filesystem::weakly_canonical(path, error).lexically_normal();
}

bool IsWithinRoot(const std::filesystem::path& path, const std::filesystem::path& root)
{
    const std::filesystem::path normalizedPath = NormalizeAbsolute(path);
    const std::filesystem::path normalizedRoot = NormalizeAbsolute(root);
    const std::string value = normalizedPath.generic_string();
    const std::string prefix = normalizedRoot.generic_string();
    return value == prefix ||
        (value.size() > prefix.size() && value.compare(0, prefix.size(), prefix) == 0 &&
         value[prefix.size()] == '/');
}

std::filesystem::path ContentOrSourceRoot(EditorContext& context,
                                          const std::filesystem::path& path)
{
    const std::filesystem::path content = context.GetContentRoot();
    const std::filesystem::path source = content.parent_path() / "SourceAssets";
    if (IsWithinRoot(path, content)) return content;
    if (IsWithinRoot(path, source)) return source;
    return {};
}

std::filesystem::path ResolveEditorAssetPath(EditorContext& context, const std::string& path)
{
    if (path.empty()) return {};
    std::filesystem::path value(path);
    if (value.is_absolute()) return value.lexically_normal();
    const std::string generic = value.generic_string();
    if (generic.rfind("Content/", 0) == 0 || generic == "Content" ||
        generic.rfind("SourceAssets/", 0) == 0 || generic == "SourceAssets") {
        return (context.GetContentRoot().parent_path() / value).lexically_normal();
    }
    return (context.GetContentRoot() / value).lexically_normal();
}

bool IsTextLikeAsset(const std::filesystem::path& path, EditorAssetType type)
{
    if (type == EditorAssetType::Script || type == EditorAssetType::Shader ||
        type == EditorAssetType::UI) {
        return true;
    }
    if (type != EditorAssetType::Unknown) return false;
    const std::string ext = path.extension().generic_string();
    return ext == ".json" || ext == ".txt" || ext == ".md";
}

std::string JsonPointerForChild(const std::string& parent, const std::string& key)
{
    std::string escaped;
    escaped.reserve(key.size());
    for (char ch : key) {
        if (ch == '~') escaped += "~0";
        else if (ch == '/') escaped += "~1";
        else escaped.push_back(ch);
    }
    return parent + "/" + escaped;
}

std::string JsonPointerForChild(const std::string& parent, size_t index)
{
    return parent + "/" + std::to_string(index);
}

std::string NormalizeReferenceString(const std::string& value)
{
    return std::filesystem::path(value).lexically_normal().generic_string();
}

bool AssetReferenceStringMatches(EditorContext& context,
                                 const std::filesystem::path& targetAbsolute,
                                 const std::string& value)
{
    if (value.empty()) return false;
    const std::string normalizedValue = NormalizeReferenceString(value);
    const std::string targetGeneric = targetAbsolute.lexically_normal().generic_string();
    if (normalizedValue == targetGeneric) return true;

    const std::filesystem::path contentRoot = context.GetContentRoot();
    if (!contentRoot.empty()) {
        const std::filesystem::path projectRoot = contentRoot.parent_path();
        std::error_code error;
        const std::filesystem::path targetRelativeToProject =
            std::filesystem::relative(targetAbsolute, projectRoot, error);
        if (!error && !targetRelativeToProject.empty() &&
            normalizedValue == targetRelativeToProject.lexically_normal().generic_string()) {
            return true;
        }
        error.clear();
        const std::filesystem::path targetRelativeToContent =
            std::filesystem::relative(targetAbsolute, contentRoot, error);
        if (!error && !targetRelativeToContent.empty() &&
            normalizedValue == targetRelativeToContent.lexically_normal().generic_string()) {
            return true;
        }
    }

    const std::filesystem::path resolved = ResolveEditorAssetPath(context, value);
    return !resolved.empty() &&
        NormalizeAbsolute(resolved) == NormalizeAbsolute(targetAbsolute);
}

void FindAssetReferencesInJson(EditorContext& context,
                               const std::filesystem::path& targetAbsolute,
                               const nlohmann::json& value,
                               const std::string& path,
                               std::vector<EditorAssetOperator::SceneReferenceInfo>& out,
                               const std::string& scenePath,
                               uint64_t actorID,
                               const std::string& actorName,
                               const std::string& componentType)
{
    if (value.is_string()) {
        const std::string text = value.get<std::string>();
        if (AssetReferenceStringMatches(context, targetAbsolute, text)) {
            EditorAssetOperator::SceneReferenceInfo info;
            info.scenePath = scenePath;
            info.actorID = actorID;
            info.actorName = actorName;
            info.componentType = componentType;
            info.jsonPath = path.empty() ? "/" : path;
            info.valuePreview = text;
            out.push_back(std::move(info));
        }
        return;
    }
    if (value.is_object()) {
        for (auto it = value.begin(); it != value.end(); ++it) {
            FindAssetReferencesInJson(
                context, targetAbsolute, it.value(),
                JsonPointerForChild(path, it.key()), out,
                scenePath,
                actorID, actorName, componentType);
        }
        return;
    }
    if (value.is_array()) {
        for (size_t index = 0; index < value.size(); ++index) {
            FindAssetReferencesInJson(
                context, targetAbsolute, value[index],
                JsonPointerForChild(path, index), out,
                scenePath,
                actorID, actorName, componentType);
        }
    }
}

void FindAssetReferencesInSceneJson(EditorContext& context,
                                    const std::filesystem::path& targetAbsolute,
                                    const nlohmann::json& sceneJson,
                                    const std::string& scenePath,
                                    std::vector<EditorAssetOperator::SceneReferenceInfo>& out)
{
    const auto actors = sceneJson.find("actors");
    if (actors == sceneJson.end() || !actors->is_array()) return;

    for (const auto& actorJson : *actors) {
        if (!actorJson.is_object()) continue;
        const uint64_t actorID = actorJson.value("id", uint64_t{0});
        const std::string actorName =
            actorJson.value("name", std::string{"Actor"});

        const auto prefab = actorJson.find("prefabInstance");
        if (prefab != actorJson.end() && prefab->is_object()) {
            const std::string asset = prefab->value("asset", std::string{});
            if (AssetReferenceStringMatches(context, targetAbsolute, asset)) {
                EditorAssetOperator::SceneReferenceInfo info;
                info.scenePath = scenePath;
                info.actorID = actorID;
                info.actorName = actorName;
                info.componentType = "Prefab";
                info.jsonPath = "/prefabInstance/asset";
                info.valuePreview = asset;
                out.push_back(std::move(info));
            }
        }

        const auto components = actorJson.find("components");
        if (components == actorJson.end() || !components->is_array()) continue;
        for (const auto& componentJson : *components) {
            if (!componentJson.is_object()) continue;
            const std::string componentType =
                componentJson.value("type", std::string{"Component"});
            const auto data = componentJson.find("data");
            if (data == componentJson.end()) continue;
            FindAssetReferencesInJson(
                context, targetAbsolute, *data, "", out, scenePath,
                actorID, actorName, componentType);
        }
    }
}

std::string ProjectRelativeReferencePath(EditorContext& context,
                                         const std::filesystem::path& absolutePath)
{
    const std::filesystem::path contentRoot = context.GetContentRoot();
    if (contentRoot.empty()) return absolutePath.lexically_normal().generic_string();
    const std::filesystem::path projectRoot = contentRoot.parent_path();
    std::error_code error;
    const std::filesystem::path relative =
        std::filesystem::relative(absolutePath, projectRoot, error);
    return error || relative.empty()
        ? absolutePath.lexically_normal().generic_string()
        : relative.lexically_normal().generic_string();
}

std::string ContentRelativeReferencePath(EditorContext& context,
                                         const std::filesystem::path& absolutePath)
{
    const std::filesystem::path contentRoot = context.GetContentRoot();
    if (contentRoot.empty()) return absolutePath.lexically_normal().generic_string();
    std::error_code error;
    const std::filesystem::path relative =
        std::filesystem::relative(absolutePath, contentRoot, error);
    return error || relative.empty()
        ? ProjectRelativeReferencePath(context, absolutePath)
        : relative.lexically_normal().generic_string();
}

std::string ReplacementAssetReferenceString(EditorContext& context,
                                            const std::string& oldValue,
                                            const std::filesystem::path& newAbsolute)
{
    const std::filesystem::path oldPath(oldValue);
    const std::string oldGeneric = oldPath.lexically_normal().generic_string();
    if (oldPath.is_absolute()) {
        return newAbsolute.lexically_normal().generic_string();
    }
    if (oldGeneric.rfind("Content/", 0) == 0 || oldGeneric == "Content" ||
        oldGeneric.rfind("SourceAssets/", 0) == 0 || oldGeneric == "SourceAssets") {
        return ProjectRelativeReferencePath(context, newAbsolute);
    }
    return ContentRelativeReferencePath(context, newAbsolute);
}

size_t RetargetAssetReferencesInJson(EditorContext& context,
                                     const std::filesystem::path& oldAbsolute,
                                     const std::filesystem::path& newAbsolute,
                                     nlohmann::json& value)
{
    if (value.is_string()) {
        const std::string text = value.get<std::string>();
        if (!AssetReferenceStringMatches(context, oldAbsolute, text)) return 0;
        value = ReplacementAssetReferenceString(context, text, newAbsolute);
        return 1;
    }
    size_t count = 0;
    if (value.is_object()) {
        for (auto it = value.begin(); it != value.end(); ++it) {
            count += RetargetAssetReferencesInJson(
                context, oldAbsolute, newAbsolute, it.value());
        }
        return count;
    }
    if (value.is_array()) {
        for (auto& item : value) {
            count += RetargetAssetReferencesInJson(
                context, oldAbsolute, newAbsolute, item);
        }
    }
    return count;
}

bool OpenExternalFile(const std::filesystem::path& path)
{
#if defined(_WIN32)
    const auto result = reinterpret_cast<intptr_t>(
        ShellExecuteW(nullptr, L"open", path.wstring().c_str(),
                      nullptr, nullptr, SW_SHOWNORMAL));
    return result > 32;
#else
    (void)path;
    return false;
#endif
}

bool RevealExternalPath(const std::filesystem::path& path)
{
#if defined(_WIN32)
    const std::wstring parameters = L"/select,\"" + path.wstring() + L"\"";
    const auto result = reinterpret_cast<intptr_t>(
        ShellExecuteW(nullptr, L"open", L"explorer.exe", parameters.c_str(),
                      nullptr, SW_SHOWNORMAL));
    return result > 32;
#else
    (void)path;
    return false;
#endif
}

std::string MakeUniqueSiblingName(Scene& scene, const std::string& base)
{
    if (!scene.FindByName(base)) return base;
    for (int index = 1; index < 10000; ++index) {
        const std::string candidate = base + " (" + std::to_string(index) + ")";
        if (!scene.FindByName(candidate)) return candidate;
    }
    return base + " (Copy)";
}

Actor* InstantiatePrefabNodesAsCopy(Scene& scene, const std::vector<PrefabNode>& nodes,
                                    Actor* parent, Actor* beforeSibling,
                                    std::string* error,
                                    uint64_t firstPersistentID = 0)
{
    if (nodes.empty()) {
        if (error) *error = "actor subtree is empty";
        return nullptr;
    }

    std::unordered_map<std::string, ActorHandle> handles;
    handles.reserve(nodes.size());
    for (size_t index = 0; index < nodes.size(); ++index) {
        const PrefabNode& node = nodes[index];
        ActorCreateDesc desc;
        desc.name = index == 0
            ? MakeUniqueSiblingName(scene, node.name + " (Copy)")
            : node.name;
        desc.transform = node.transform;
        desc.activeSelf = node.activeSelf;
        desc.components = node.components;
        if (firstPersistentID != 0) desc.persistentID = firstPersistentID + index;
        handles[node.localId] = scene.QueueCreateActor(desc);
    }

    if (!scene.FlushCommands()) {
        if (error) *error = "failed to create actor copy";
        return nullptr;
    }

    for (size_t index = 0; index < nodes.size(); ++index) {
        const PrefabNode& node = nodes[index];
        auto childIt = handles.find(node.localId);
        if (childIt == handles.end()) continue;
        if (index == 0) {
            scene.QueueMoveActor(
                childIt->second,
                parent ? parent->GetHandle() : ActorHandle{},
                beforeSibling ? beforeSibling->GetHandle() : ActorHandle{});
        } else {
            auto parentIt = handles.find(node.parentLocalId);
            if (parentIt != handles.end()) scene.QueueSetParent(childIt->second, parentIt->second);
        }
    }
    if (!scene.FlushCommands()) {
        if (error) *error = "failed to restore actor copy hierarchy";
        return nullptr;
    }

    const auto rootIt = handles.find(nodes.front().localId);
    return rootIt == handles.end() ? nullptr : scene.TryGetActor(rootIt->second);
}

bool LoadClipboardNodes(const std::string& actorClipboardJson,
                        std::vector<PrefabNode>& nodes)
{
    nodes.clear();
    if (actorClipboardJson.empty()) return false;
    try {
        const nlohmann::json value = nlohmann::json::parse(actorClipboardJson);
        if (!value.is_array()) return false;
        for (const auto& item : value) {
            PrefabNode node;
            if (!PrefabNodeFromJson(item, node)) return false;
            nodes.push_back(std::move(node));
        }
        return !nodes.empty();
    } catch (...) {
        return false;
    }
}

std::vector<PrefabNode> LoadClipboardRootNodes(const nlohmann::json& value)
{
    std::vector<PrefabNode> nodes;
    if (!value.is_array()) return nodes;
    nodes.reserve(value.size());
    for (const auto& item : value) {
        PrefabNode node;
        if (!PrefabNodeFromJson(item, node)) return {};
        nodes.push_back(std::move(node));
    }
    return nodes;
}

bool LoadClipboardRoots(const std::string& actorClipboardJson,
                        std::vector<std::vector<PrefabNode>>& roots)
{
    roots.clear();
    if (actorClipboardJson.empty()) return false;
    try {
        const nlohmann::json value = nlohmann::json::parse(actorClipboardJson);
        if (value.is_array()) {
            std::vector<PrefabNode> nodes = LoadClipboardRootNodes(value);
            if (nodes.empty()) return false;
            roots.push_back(std::move(nodes));
            return true;
        }
        if (!value.is_object()) return false;
        const nlohmann::json& rootValues = value.contains("roots")
            ? value["roots"]
            : value["actorRoots"];
        if (!rootValues.is_array()) return false;
        for (const auto& rootValue : rootValues) {
            std::vector<PrefabNode> nodes = LoadClipboardRootNodes(rootValue);
            if (nodes.empty()) return false;
            roots.push_back(std::move(nodes));
        }
        return !roots.empty();
    } catch (...) {
        return false;
    }
}

nlohmann::json ClipboardRootToJson(const std::vector<PrefabNode>& nodes)
{
    nlohmann::json root = nlohmann::json::array();
    for (const PrefabNode& node : nodes) root.push_back(PrefabNodeToJson(node));
    return root;
}

std::string TemplateExtensionFor(const std::string& templateID)
{
    if (templateID == "angelscript" || templateID == "as") return ".as";
    if (templateID == "lua") return ".lua";
    if (templateID == "shader") return ".shader";
    if (templateID == "material" || templateID == "mat") return ".mat";
    if (templateID == "texture" || templateID == "tex") return ".tex";
    if (templateID == "prefab") return ".prefab.json";
    if (templateID == "ui") return ".rml";
    if (templateID == "scene") return ".scene.json";
    return ".json";
}

std::string TemplateBaseNameFor(const std::string& templateID)
{
    if (templateID == "angelscript" || templateID == "as") return "NewScript";
    if (templateID == "lua") return "NewLuaScript";
    if (templateID == "shader") return "NewShader";
    if (templateID == "material" || templateID == "mat") return "NewMaterial";
    if (templateID == "texture" || templateID == "tex") return "NewTexture";
    if (templateID == "prefab") return "NewPrefab";
    if (templateID == "ui") return "NewDocument";
    if (templateID == "scene") return "NewScene";
    return "NewAsset";
}

std::string TemplateContentFor(const std::string& templateID)
{
    if (templateID == "angelscript" || templateID == "as") {
        return "class NewScript\n{\n    void Start() {}\n    void Update(float dt) {}\n}\n";
    }
    if (templateID == "lua") return "function update(dt)\nend\n";
    if (templateID == "shader") return "{\n  \"name\": \"NewShader\",\n  \"stages\": []\n}\n";
    if (templateID == "material" || templateID == "mat") {
        return "{\n"
            "  \"type\": \"Material\",\n"
            "  \"name\": \"NewMaterial\",\n"
            "  \"blendMode\": \"Opaque\",\n"
            "  \"twoSided\": false,\n"
            "  \"wireframe\": false,\n"
            "  \"alphaThreshold\": 0.5,\n"
            "  \"params\": {},\n"
            "  \"textures\": {}\n"
            "}\n";
    }
    if (templateID == "texture" || templateID == "tex") return "{}\n";
    if (templateID == "prefab") {
        return "{\n"
            "  \"version\": 1,\n"
            "  \"uuid\": \"\",\n"
            "  \"rootLocalId\": \"root\",\n"
            "  \"nodes\": [\n"
            "    {\n"
            "      \"localId\": \"root\",\n"
            "      \"parentLocalId\": \"\",\n"
            "      \"name\": \"NewPrefab\",\n"
            "      \"active\": true,\n"
            "      \"transform\": {\n"
            "        \"position\": [0.0, 0.0, 0.0],\n"
            "        \"rotation\": [0.0, 0.0, 0.0],\n"
            "        \"scale\": [1.0, 1.0, 1.0]\n"
            "      },\n"
            "      \"components\": []\n"
            "    }\n"
            "  ]\n"
            "}\n";
    }
    if (templateID == "ui") return "<rml>\n  <body>\n  </body>\n</rml>\n";
    if (templateID == "scene") return "{\n  \"version\": 1,\n  \"name\": \"New Scene\",\n  \"actors\": []\n}\n";
    return "{}";
}

std::filesystem::path MakeUniquePath(const std::filesystem::path& directory,
                                     const std::string& baseName,
                                     const std::string& extension)
{
    std::filesystem::path path = directory / (baseName + extension);
    auto isTaken = [](const std::filesystem::path& candidate) {
        return std::filesystem::exists(candidate) ||
            std::filesystem::exists(AssetMeta::MetaPathFor(candidate.string()));
    };
    for (int index = 1; isTaken(path) && index < 10000; ++index) {
        path = directory / (baseName + "_" + std::to_string(index) + extension);
    }
    return path;
}

std::string PrefabTemplateContentForUuid(const std::string& uuid)
{
    nlohmann::json root = {
        {"version", 1},
        {"uuid", uuid},
        {"rootLocalId", "root"},
        {"nodes", nlohmann::json::array({
            {
                {"localId", "root"},
                {"parentLocalId", ""},
                {"name", "NewPrefab"},
                {"active", true},
                {"transform", {
                    {"position", nlohmann::json::array({0.0f, 0.0f, 0.0f})},
                    {"rotation", nlohmann::json::array({0.0f, 0.0f, 0.0f})},
                    {"scale", nlohmann::json::array({1.0f, 1.0f, 1.0f})}
                }},
                {"components", nlohmann::json::array()}
            }
        })}
    };
    return root.dump(2) + "\n";
}

bool CreatePrefabTemplateAsset(EditorContext& context,
                               const std::filesystem::path& path)
{
    auto execute = [path](EditorContext&) {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) return false;

        AssetMeta meta = AssetMeta::Create(path.string());
        const std::string content = PrefabTemplateContentForUuid(meta.uuid);
        {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(content.data(), static_cast<std::streamsize>(content.size()));
            output.close();
            if (!output.good()) return false;
        }
        if (!AssetMeta::Save(meta)) {
            std::filesystem::remove(path, error);
            return false;
        }
        return true;
    };
    auto undo = [path](EditorContext&) {
        std::error_code error;
        const bool removed = std::filesystem::remove(path, error);
        if (error || !removed) return false;
        std::filesystem::remove(AssetMeta::MetaPathFor(path.string()), error);
        return true;
    };
    EditorCommandStack* stack = context.GetCommandStack();
    return stack
        ? stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>(
              "Create Prefab", execute, undo), context)
        : execute(context);
}

bool CommitPrefabSceneSnapshot(EditorContext& context, Actor& actor, const char* label,
                               const std::function<bool(Actor&, std::string*)>& edit)
{
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack || !context.CanEditScene()) return false;
    const uint64_t selection = actor.GetID();
    const std::string before = SceneSerializer::SaveToString(*scene);
    std::string error;
    if (!edit(actor, &error)) {
        if (!error.empty()) Logger::Warn("[Editor] ", label, " failed: ", error);
        return false;
    }
    const std::string after = SceneSerializer::SaveToString(*scene);
    SceneSerializer::LoadFromString(*scene, before);
    return stack->ExecuteCommand(
        EditorUndoUtil::MakeSceneSnapshotCommand(label, before, after, selection, selection),
        context);
}

std::string JsonPreview(const nlohmann::json& value)
{
    if (value.is_string()) return value.get<std::string>();
    std::string text = value.dump();
    constexpr size_t kMaxPreview = 80;
    if (text.size() > kMaxPreview) text = text.substr(0, kMaxPreview - 3) + "...";
    return text;
}

std::string DecodeJsonPointerToken(std::string token)
{
    size_t position = 0;
    while ((position = token.find("~1", position)) != std::string::npos) {
        token.replace(position, 2, "/");
        ++position;
    }
    position = 0;
    while ((position = token.find("~0", position)) != std::string::npos) {
        token.replace(position, 2, "~");
        ++position;
    }
    return token;
}

std::string PropertyLabelFromPointer(const std::string& path)
{
    if (path.empty() || path == "/") return "(self)";
    std::string label;
    size_t start = path.front() == '/' ? 1 : 0;
    while (start <= path.size()) {
        const size_t slash = path.find('/', start);
        std::string token = path.substr(
            start, slash == std::string::npos ? std::string::npos : slash - start);
        if (!token.empty()) {
            if (!label.empty()) label += ".";
            label += DecodeJsonPointerToken(std::move(token));
        }
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return label.empty() ? path : label;
}

std::string OverrideCategory(const nlohmann::json& item)
{
    const std::string kind = item.value("kind", std::string{});
    const std::string componentType = item.value("componentType", std::string{});
    if (kind == "AddActorSubtree" || kind == "RemoveActorSubtree") return "Hierarchy";
    if (kind == "AddComponent" || kind == "RemoveComponent" || !componentType.empty()) {
        return "Component";
    }
    if (kind == "SetProperty" || kind == "RemoveProperty") return "Actor";
    return "Unsupported";
}

int OverrideCategoryRank(const std::string& category)
{
    if (category == "Actor") return 0;
    if (category == "Component") return 1;
    if (category == "Hierarchy") return 2;
    return 3;
}

std::string OverrideTargetLabel(const nlohmann::json& item)
{
    const std::string kind = item.value("kind", std::string{});
    const std::string localId = item.value("localId", std::string{});
    const std::string componentType = item.value("componentType", std::string{});
    if (kind == "AddActorSubtree") return "Parent " + localId;
    if (kind == "RemoveActorSubtree") return "Actor " + localId;
    if (!componentType.empty()) return componentType + " on " + localId;
    return "Actor " + localId;
}

std::string OverridePropertyLabel(const nlohmann::json& item)
{
    const std::string kind = item.value("kind", std::string{});
    if (kind == "AddActorSubtree") return "Added subtree";
    if (kind == "RemoveActorSubtree") return "Removed subtree";
    if (kind == "AddComponent") return "Added component";
    if (kind == "RemoveComponent") return "Removed component";
    return PropertyLabelFromPointer(item.value("path", std::string{}));
}

std::string OverrideValuePreview(const nlohmann::json& item)
{
    const std::string kind = item.value("kind", std::string{});
    if (item.contains("value")) return JsonPreview(item["value"]);
    if (kind == "RemoveProperty") return "(removed)";
    if (kind == "AddComponent") {
        const nlohmann::json* data = item.contains("data") ? &item["data"] : nullptr;
        return data ? JsonPreview(*data) : "(added)";
    }
    if (kind == "RemoveComponent") return "(removed)";
    if (kind == "AddActorSubtree") {
        const size_t count = item.contains("nodes") && item["nodes"].is_array()
            ? item["nodes"].size()
            : 0;
        return std::to_string(count) + (count == 1 ? " node" : " nodes");
    }
    if (kind == "RemoveActorSubtree") return "(removed)";
    return {};
}

std::string OverrideLabel(const nlohmann::json& item)
{
    const std::string kind = item.value("kind", std::string{});
    const std::string id = item.value("localId", std::string{});
    const std::string type = item.value("componentType", std::string{});
    const std::string path = item.value("path", std::string{});
    if (kind == "AddActorSubtree") return "Add actor subtree under " + id;
    if (kind == "RemoveActorSubtree") return "Remove actor subtree " + id;
    if (kind == "AddComponent") return "Add " + type + " on " + id;
    if (kind == "RemoveComponent") return "Remove " + type + " from " + id;
    if (!type.empty()) return type + (path.empty() ? std::string{} : " " + path);
    if (!path.empty()) return id + " " + path;
    return kind.empty() ? "Unknown override" : kind + " " + id;
}

bool IsSupportedPrefabOverride(const nlohmann::json& item)
{
    const std::string kind = item.value("kind", std::string{});
    return kind == "SetProperty" || kind == "RemoveProperty" ||
        kind == "AddComponent" || kind == "RemoveComponent" ||
        kind == "AddActorSubtree" || kind == "RemoveActorSubtree";
}

nlohmann::json RemoveOverrideAt(nlohmann::json overrides, size_t index)
{
    if (!overrides.is_array() || index >= overrides.size()) return nlohmann::json::array();
    overrides.erase(overrides.begin() + static_cast<nlohmann::json::difference_type>(index));
    return overrides;
}

Actor* ResolveEditablePrefabActor(EditorContext& context, uint64_t actorID)
{
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    return actor && actor->IsPrefabRoot() ? actor : nullptr;
}

bool RestorePrefabEditorState(EditorContext& context, const std::filesystem::path& prefabPath,
                              const std::string& prefabContent,
                              const std::string& sceneJson,
                              uint64_t actorID)
{
    Scene* scene = context.GetScene();
    if (!scene || !WriteFileContent(prefabPath, prefabContent) ||
        !SceneSerializer::LoadFromString(*scene, sceneJson)) {
        return false;
    }
    if (scene->FindByID(actorID)) context.GetSelection().SelectActorID(actorID);
    else context.GetSelection().Clear();
    if (EditorAssetRegistry* registry = context.GetAssetRegistry()) registry->Refresh();
    return true;
}

bool ApplySinglePrefabOverrideNow(EditorContext& context, uint64_t actorID,
                                  size_t overrideIndex, std::string* error)
{
    Actor* actor = ResolveEditablePrefabActor(context, actorID);
    if (!actor) {
        if (error) *error = "actor is not a prefab instance root";
        return false;
    }
    nlohmann::json overrides;
    if (!PrefabSystem::BuildOverrides(*actor, overrides, error)) return false;
    if (!overrides.is_array() || overrideIndex >= overrides.size()) {
        if (error) *error = "prefab override index is out of range";
        return false;
    }
    const nlohmann::json item = overrides[overrideIndex];
    if (!IsSupportedPrefabOverride(item)) {
        if (error) *error = "unsupported prefab override kind: " +
            item.value("kind", std::string{});
        return false;
    }
    const std::filesystem::path prefabPath =
        PrefabSystem::ResolvePrefabPath(actor->GetPrefabAssetPath());
    PrefabAsset asset;
    if (!PrefabAsset::Load(prefabPath, asset, error)) return false;
    nlohmann::json single = nlohmann::json::array();
    single.push_back(item);
    if (!PrefabSystem::ApplyOverridesToAsset(asset, single, error) ||
        !asset.Save(prefabPath, error)) {
        return false;
    }
    nlohmann::json remaining;
    if (!PrefabSystem::BuildOverrides(*actor, remaining, error) ||
        !PrefabSystem::SetInstanceOverrides(*actor, std::move(remaining), error)) {
        return false;
    }
    return actor->GetScene() &&
        PrefabSystem::RefreshInstances(*actor->GetScene(), actor->GetPrefabAssetUuid(), error);
}

bool RevertSinglePrefabOverrideNow(EditorContext& context, uint64_t actorID,
                                   size_t overrideIndex, std::string* error)
{
    Actor* actor = ResolveEditablePrefabActor(context, actorID);
    if (!actor) {
        if (error) *error = "actor is not a prefab instance root";
        return false;
    }
    nlohmann::json overrides;
    if (!PrefabSystem::BuildOverrides(*actor, overrides, error)) return false;
    if (!overrides.is_array() || overrideIndex >= overrides.size()) {
        if (error) *error = "prefab override index is out of range";
        return false;
    }
    if (!IsSupportedPrefabOverride(overrides[overrideIndex])) {
        if (error) *error = "unsupported prefab override kind: " +
            overrides[overrideIndex].value("kind", std::string{});
        return false;
    }
    nlohmann::json remaining = RemoveOverrideAt(std::move(overrides), overrideIndex);
    if (!PrefabSystem::SetInstanceOverrides(*actor, std::move(remaining), error)) return false;
    return actor->GetScene() &&
        PrefabSystem::RefreshInstances(*actor->GetScene(), actor->GetPrefabAssetUuid(), error);
}

bool ResolveViewportFrameTarget(EditorContext& context, Vec3& target, float& radius)
{
    Scene* scene = context.GetInspectorScene();
    Actor* actor = scene ? context.GetSelection().ResolveActor(*scene) : nullptr;
    if (!actor) return false;
    target = actor->GetWorldMatrix().TransformPoint(Vec3::Zero());
    radius = 1.0f;
    return true;
}

} // namespace

bool EditorSelectionOperator::SelectActor(EditorContext& context, uint64_t actorID,
                                          EditorSelectionIntentMode mode) const
{
    Scene* scene = context.GetInspectorScene();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    if (!actor) return false;
    const EditorSelectionWorldKind world =
        context.GetSceneViewMode() == EditorWorldViewMode::PlayWorldInspect
            ? EditorSelectionWorldKind::Play
            : EditorSelectionWorldKind::Editor;
    context.GetSelection().Select(EditorSelectObject::MakeActor(
        actor->GetHandle(), actor->GetID(), world), ToSelectionMode(mode));
    return true;
}

EditorOperators::EditorOperators()
    : m_Commands(&m_Clipboard)
{
}

bool EditorSelectionOperator::SelectActorRange(
    EditorContext& context, uint64_t anchorActorID, uint64_t targetActorID,
    const std::vector<uint64_t>& orderedActorIDs) const
{
    Scene* scene = context.GetInspectorScene();
    if (!scene || anchorActorID == 0 || targetActorID == 0) return false;
    Actor* targetActor = scene->FindByID(targetActorID);
    if (!scene->FindByID(anchorActorID) || !targetActor) return false;

    std::vector<uint64_t> ordered = orderedActorIDs;
    if (ordered.empty()) {
        std::function<void(Actor*)> collect = [&](Actor* actor) {
            if (!actor) return;
            ordered.push_back(actor->GetID());
            for (Actor* child : actor->GetChildren()) collect(child);
        };
        for (Actor* root : scene->GetRootActors()) collect(root);
    }

    auto anchorIt = std::find(ordered.begin(), ordered.end(), anchorActorID);
    auto targetIt = std::find(ordered.begin(), ordered.end(), targetActorID);
    if (anchorIt == ordered.end() || targetIt == ordered.end()) return false;
    if (targetIt < anchorIt) std::swap(anchorIt, targetIt);

    const EditorSelectionWorldKind world =
        context.GetSceneViewMode() == EditorWorldViewMode::PlayWorldInspect
            ? EditorSelectionWorldKind::Play
            : EditorSelectionWorldKind::Editor;

    context.GetSelection().Clear();
    for (auto it = anchorIt; it <= targetIt; ++it) {
        if (Actor* actor = scene->FindByID(*it)) {
            context.GetSelection().Select(
                EditorSelectObject::MakeActor(actor->GetHandle(), actor->GetID(), world),
                EditorSelectionMode::Add);
        }
    }
    context.GetSelection().Select(
        EditorSelectObject::MakeActor(targetActor->GetHandle(), targetActor->GetID(), world),
        EditorSelectionMode::Add);
    return context.GetSelection().IsSelected(targetActorID, world);
}

bool EditorSelectionOperator::SelectActorSubtree(EditorContext& context,
                                                 uint64_t actorID,
                                                 bool includeRoot) const
{
    Scene* scene = context.GetInspectorScene();
    Actor* root = scene ? scene->FindByID(actorID) : nullptr;
    if (!root) return false;

    std::vector<Actor*> actors;
    if (includeRoot) actors.push_back(root);
    std::function<void(Actor*)> collectChildren = [&](Actor* actor) {
        if (!actor) return;
        for (Actor* child : actor->GetChildren()) {
            if (!child) continue;
            actors.push_back(child);
            collectChildren(child);
        }
    };
    collectChildren(root);
    if (actors.empty()) return false;

    const EditorSelectionWorldKind world =
        context.GetSceneViewMode() == EditorWorldViewMode::PlayWorldInspect
            ? EditorSelectionWorldKind::Play
            : EditorSelectionWorldKind::Editor;

    context.GetSelection().Clear();
    for (Actor* actor : actors) {
        context.GetSelection().Select(
            EditorSelectObject::MakeActor(actor->GetHandle(), actor->GetID(), world),
            EditorSelectionMode::Add);
    }
    return context.GetSelection().IsSelected(actors.back()->GetID(), world);
}

bool EditorSelectionOperator::SelectAsset(EditorContext& context, const std::string& path) const
{
    if (path.empty()) return false;
    context.GetSelection().Select(EditorSelectObject::MakeAsset(path));
    if (EditorProject* project = context.GetProject()) {
        project->GetState().selectedAssetPath = context.GetSelection().GetAssetPath();
    }
    return true;
}

void EditorSelectionOperator::Clear(EditorContext& context) const
{
    context.GetSelection().Clear();
}

EditorSelectionSnapshot EditorSelectionOperator::GetSelectionSnapshot(
    const EditorContext& context) const
{
    const EditorSelection& selection = context.GetSelection();
    EditorSelectionSnapshot snapshot;
    snapshot.actorID = selection.GetActorID();
    snapshot.assetPath = selection.GetAssetPath();
    snapshot.hasActor = selection.HasActor();
    snapshot.hasAsset = selection.HasAsset();
    return snapshot;
}

bool EditorCommandOperator::ExecuteAction(EditorContext& context,
                                          const std::string& actionID) const
{
    EditorActionRegistry* actions = context.GetActionRegistry();
    return actions && actions->Execute(actionID, context);
}

uint64_t EditorCommandOperator::CreateActor(EditorContext& context,
                                            const std::string& name) const
{
    if (!context.CanEditScene() || !context.GetScene() || !context.GetCommandStack()) return 0;
    ActorCreateDesc desc;
    desc.name = name.empty() ? "Actor" : name;
    const uint64_t newID = FindNextActorID(*context.GetScene());
    auto command = EditorUndoUtil::MakeCreateActorCommand(desc, newID);
    if (!command || !context.GetCommandStack()->ExecuteCommand(std::move(command), context)) return 0;
    return newID;
}

uint64_t EditorCommandOperator::CreateChildActor(EditorContext& context,
                                                 const std::string& name,
                                                 uint64_t parentActorID) const
{
    Scene* scene = context.GetScene();
    Actor* parent = scene && parentActorID ? scene->FindByID(parentActorID) : nullptr;
    if (!context.CanEditScene() || !scene || !context.GetCommandStack() || !parent) {
        return 0;
    }
    ActorCreateDesc desc;
    desc.name = name.empty() ? "Actor" : name;
    desc.parent = parent->GetHandle();
    const uint64_t newID = FindNextActorID(*scene);
    auto command = EditorUndoUtil::MakeCreateActorCommand(desc, newID);
    if (!command || !context.GetCommandStack()->ExecuteCommand(std::move(command), context)) {
        return 0;
    }
    return newID;
}

uint64_t EditorCommandOperator::CreateUIActor(EditorContext& context,
                                              const std::string& presetID,
                                              uint64_t parentActorID) const
{
    if (!context.CanEditScene() || !context.GetScene() || !context.GetCommandStack()) {
        return 0;
    }
    const std::optional<EditorUIPreset> preset = EditorUIPresetFromID(presetID);
    if (!preset) return 0;

    Scene* scene = context.GetScene();
    Actor* parent = parentActorID ? scene->FindByID(parentActorID) : nullptr;
    if (parentActorID && !parent) return 0;

    const std::string before = SceneSerializer::SaveToString(*scene);
    const uint64_t oldSelection = context.GetSelection().GetActorID();
    Actor* actor = scene->CreateActor(EditorUIPresetName(*preset), parent);
    if (!actor) return 0;
    AddUIPresetComponents(*actor, *preset);
    const uint64_t newSelection = actor->GetID();
    const std::string after = SceneSerializer::SaveToString(*scene);
    SceneSerializer::LoadFromString(*scene, before);
    if (!context.GetCommandStack()->ExecuteCommand(
            EditorUndoUtil::MakeSceneSnapshotCommand(
                "Create UI Actor", before, after, oldSelection, newSelection),
            context)) {
        return 0;
    }
    return newSelection;
}

uint64_t EditorCommandOperator::CreateEmptyParent(EditorContext& context,
                                                  uint64_t actorID) const
{
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    if (!scene || !actor || !context.GetCommandStack() || !context.CanEditScene()) {
        return 0;
    }

    const uint64_t beforeSelection = context.GetSelection().GetActorID();
    const std::string before = SceneSerializer::SaveToString(*scene);

    Actor* oldParent = actor->GetParent();
    const ActorHandle actorHandle = actor->GetHandle();
    const ActorHandle oldParentHandle = oldParent ? oldParent->GetHandle() : ActorHandle{};
    Actor* newParent = scene->CreateActor("Empty Parent", oldParent);
    if (!newParent) return 0;
    const uint64_t newParentID = newParent->GetID();

    scene->QueueMoveActor(newParent->GetHandle(), oldParentHandle, actorHandle);
    scene->QueueMoveActor(actorHandle, newParent->GetHandle(), ActorHandle{});
    scene->FlushCommands();

    const std::string after = SceneSerializer::SaveToString(*scene);
    SceneSerializer::LoadFromString(*scene, before);
    if (before == after) return 0;

    if (!context.GetCommandStack()->ExecuteCommand(
            EditorUndoUtil::MakeSceneSnapshotCommand(
                "Create Empty Parent", before, after, beforeSelection, newParentID),
            context)) {
        return 0;
    }
    return newParentID;
}

bool EditorCommandOperator::DuplicateActorSubtree(EditorContext& context,
                                                  uint64_t actorID) const
{
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    EditorCommandStack* stack = context.GetCommandStack();
    if (!actor || !stack || !context.CanEditScene()) return false;

    std::vector<PrefabNode> nodes;
    std::string error;
    if (!ActorSubtreeSerializer::Serialize(*actor, nodes, &error)) {
        if (!error.empty()) Logger::Warn("[Editor] Duplicate actor failed: ", error);
        return false;
    }

    const std::string before = SceneSerializer::SaveToString(*scene);
    const uint64_t beforeSelection = context.GetSelection().GetActorID();
    const uint64_t firstCloneID = FindNextActorID(*scene);
    Actor* nextSibling = nullptr;
    if (const uint64_t nextID = NextSiblingID(*actor)) nextSibling = scene->FindByID(nextID);
    Actor* clone = InstantiatePrefabNodesAsCopy(*scene, nodes, actor->GetParent(),
                                                nextSibling, &error, firstCloneID);
    if (!clone) {
        SceneSerializer::LoadFromString(*scene, before);
        if (!error.empty()) Logger::Warn("[Editor] Duplicate actor failed: ", error);
        return false;
    }

    const uint64_t cloneID = clone->GetID();
    const std::string after = SceneSerializer::SaveToString(*scene);
    SceneSerializer::LoadFromString(*scene, before);
    return stack->ExecuteCommand(
        EditorUndoUtil::MakeSceneSnapshotCommand("Duplicate Actor", before, after,
                                                 beforeSelection, cloneID),
        context);
}

bool IsSelectedActorDescendant(const std::vector<uint64_t>& selectedIDs,
                               const Actor& actor)
{
    for (Actor* parent = actor.GetParent(); parent; parent = parent->GetParent()) {
        if (std::find(selectedIDs.begin(), selectedIDs.end(), parent->GetID()) !=
            selectedIDs.end()) {
            return true;
        }
    }
    return false;
}

std::vector<uint64_t> OrderedSelectedActorRoots(Scene& scene,
                                                const std::vector<uint64_t>& selectedIDs)
{
    std::vector<uint64_t> roots;
    roots.reserve(selectedIDs.size());
    scene.ForEach([&](Actor& actor) {
        const uint64_t actorID = actor.GetID();
        if (std::find(selectedIDs.begin(), selectedIDs.end(), actorID) ==
            selectedIDs.end()) {
            return;
        }
        if (IsSelectedActorDescendant(selectedIDs, actor)) return;
        roots.push_back(actorID);
    });
    return roots;
}

bool EditorCommandOperator::DuplicateSelection(EditorContext& context) const
{
    const EditorSelectionSnapshot selection =
        EditorSelectionOperator{}.GetSelectionSnapshot(context);
    if (!selection.hasActor) {
        if (selection.hasAsset) {
            return EditorAssetOperator{}.DuplicateAsset(context, selection.assetPath);
        }
        return false;
    }

    const std::vector<uint64_t>& selectedIDs = context.GetSelection().GetActorIDs();
    if (selectedIDs.size() <= 1) return DuplicateActorSubtree(context, selection.actorID);

    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack || !context.CanEditScene()) return false;

    const std::vector<uint64_t> rootIDs = OrderedSelectedActorRoots(*scene, selectedIDs);
    if (rootIDs.empty()) return false;

    const std::string before = SceneSerializer::SaveToString(*scene);
    const uint64_t beforeSelection = context.GetSelection().GetActorID();
    std::vector<uint64_t> cloneIDs;
    cloneIDs.reserve(rootIDs.size());

    std::string error;
    for (uint64_t actorID : rootIDs) {
        Actor* actor = scene->FindByID(actorID);
        if (!actor) continue;

        std::vector<PrefabNode> nodes;
        error.clear();
        if (!ActorSubtreeSerializer::Serialize(*actor, nodes, &error)) {
            SceneSerializer::LoadFromString(*scene, before);
            if (!error.empty()) Logger::Warn("[Editor] Duplicate actors failed: ", error);
            return false;
        }

        Actor* nextSibling = nullptr;
        if (const uint64_t nextID = NextSiblingID(*actor)) nextSibling = scene->FindByID(nextID);
        const uint64_t firstCloneID = FindNextActorID(*scene);
        Actor* clone = InstantiatePrefabNodesAsCopy(*scene, nodes, actor->GetParent(),
                                                    nextSibling, &error, firstCloneID);
        if (!clone) {
            SceneSerializer::LoadFromString(*scene, before);
            if (!error.empty()) Logger::Warn("[Editor] Duplicate actors failed: ", error);
            return false;
        }
        cloneIDs.push_back(clone->GetID());
    }

    if (cloneIDs.empty()) {
        SceneSerializer::LoadFromString(*scene, before);
        return false;
    }

    const std::string after = SceneSerializer::SaveToString(*scene);
    if (before == after) {
        SceneSerializer::LoadFromString(*scene, before);
        return false;
    }
    const uint64_t afterSelection = cloneIDs.back();
    SceneSerializer::LoadFromString(*scene, before);
    return stack->ExecuteCommand(
        EditorUndoUtil::MakeSceneSnapshotCommand("Duplicate Actors", before, after,
                                                 beforeSelection, afterSelection),
        context);
}

bool EditorCommandOperator::RenameActor(EditorContext& context, uint64_t actorID,
                                        const std::string& name) const
{
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    if (!actor || !context.GetCommandStack() || !context.CanEditScene()) return false;
    if (actor->GetName() == name) return false;
    return context.GetCommandStack()->ExecuteCommand(
        EditorUndoUtil::MakeSetNameCommand(*actor, name), context);
}

bool EditorCommandOperator::DeleteActor(EditorContext& context, uint64_t actorID) const
{
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    if (!actor || !context.GetCommandStack() || !context.CanEditScene()) return false;
    return context.GetCommandStack()->ExecuteCommand(
        EditorUndoUtil::MakeDestroyActorCommand(*actor), context);
}

bool EditorCommandOperator::DeleteSelection(EditorContext& context) const
{
    const EditorSelectionSnapshot selection =
        EditorSelectionOperator{}.GetSelectionSnapshot(context);
    if (selection.hasActor && context.GetSelection().GetActorIDs().size() > 1) {
        Scene* scene = context.GetScene();
        EditorCommandStack* stack = context.GetCommandStack();
        if (!scene || !stack || !context.CanEditScene()) return false;

        const std::vector<uint64_t> selectedIDs = context.GetSelection().GetActorIDs();
        std::vector<uint64_t> deleteIDs;
        deleteIDs.reserve(selectedIDs.size());
        for (uint64_t actorID : selectedIDs) {
            Actor* actor = scene->FindByID(actorID);
            if (!actor) continue;
            if (IsSelectedActorDescendant(selectedIDs, *actor)) continue;
            deleteIDs.push_back(actorID);
        }
        if (deleteIDs.empty()) return false;

        const std::string before = SceneSerializer::SaveToString(*scene);
        const uint64_t beforeSelection = context.GetSelection().GetActorID();
        for (uint64_t actorID : deleteIDs) {
            if (Actor* actor = scene->FindByID(actorID)) scene->DestroyActor(actor);
        }
        const std::string after = SceneSerializer::SaveToString(*scene);
        if (before == after) return false;
        SceneSerializer::LoadFromString(*scene, before);
        return stack->ExecuteCommand(
            EditorUndoUtil::MakeSceneSnapshotCommand(
                "Delete Actors", before, after, beforeSelection, 0),
            context);
    }
    if (selection.hasActor) return DeleteActor(context, selection.actorID);
    if (selection.hasAsset) return EditorAssetOperator{}.DeleteAsset(context, selection.assetPath);
    return false;
}

bool EditorCommandOperator::RenameSelection(EditorContext& context,
                                            const std::string& name) const
{
    const EditorSelectionSnapshot selection =
        EditorSelectionOperator{}.GetSelectionSnapshot(context);
    if (selection.hasActor) return RenameActor(context, selection.actorID, name);
    if (selection.hasAsset) return EditorAssetOperator{}.RenameAsset(context, selection.assetPath, name);
    return false;
}

bool EditorCommandOperator::CopySelection(EditorContext& context) const
{
    if (!m_Clipboard) return false;
    const EditorSelectionSnapshot selection =
        EditorSelectionOperator{}.GetSelectionSnapshot(context);
    if (selection.hasAsset) {
        return CopyAssets(context, {selection.assetPath});
    }

    Scene* scene = context.GetScene();
    if (!scene || !context.GetSelection().HasActor()) return false;

    std::vector<uint64_t> rootIDs;
    const std::vector<uint64_t>& selectedIDs = context.GetSelection().GetActorIDs();
    if (selectedIDs.size() > 1) {
        rootIDs = OrderedSelectedActorRoots(*scene, selectedIDs);
    } else {
        rootIDs.push_back(context.GetSelection().GetActorID());
    }
    if (rootIDs.empty()) return false;

    nlohmann::json roots = nlohmann::json::array();
    std::string error;
    for (uint64_t actorID : rootIDs) {
        Actor* actor = scene->FindByID(actorID);
        if (!actor) continue;
        std::vector<PrefabNode> nodes;
        error.clear();
        if (!ActorSubtreeSerializer::Serialize(*actor, nodes, &error)) {
            if (!error.empty()) Logger::Warn("[Editor] Copy actors failed: ", error);
            return false;
        }
        roots.push_back(ClipboardRootToJson(nodes));
    }
    if (roots.empty()) return false;

    nlohmann::json json;
    json["type"] = "ActorClipboard";
    json["version"] = 1;
    json["roots"] = std::move(roots);
    m_Clipboard->StoreActors(json.dump());
    return true;
}

bool EditorCommandOperator::CopyAssets(
    EditorContext& context, const std::vector<std::string>& paths) const
{
    if (!m_Clipboard) return false;
    std::vector<std::string> validPaths;
    validPaths.reserve(paths.size());
    for (const std::string& path : paths) {
        const std::filesystem::path assetPath =
            ResolveEditorAssetPath(context, path);
        if (assetPath.empty() || !std::filesystem::is_regular_file(assetPath) ||
            ContentOrSourceRoot(context, assetPath).empty()) {
            continue;
        }
        const std::string normalized = assetPath.lexically_normal().string();
        if (std::find(validPaths.begin(), validPaths.end(), normalized) ==
            validPaths.end()) {
            validPaths.push_back(normalized);
        }
    }
    if (validPaths.empty()) return false;
    m_Clipboard->StoreAssets(std::move(validPaths));
    return true;
}

bool EditorCommandOperator::HasActorClipboard() const
{
    return m_Clipboard && m_Clipboard->HasActors();
}

bool EditorCommandOperator::HasAssetClipboard() const
{
    return m_Clipboard && m_Clipboard->HasAssets();
}

bool EditorCommandOperator::PasteSelection(EditorContext& context) const
{
    if (!m_Clipboard) return false;
    if (m_Clipboard->GetKind() == EditorClipboardService::Kind::Asset) {
        const EditorSelectionSnapshot selection =
            EditorSelectionOperator{}.GetSelectionSnapshot(context);
        const std::filesystem::path targetFolder = selection.hasAsset
            ? ResolveEditorAssetPath(context, selection.assetPath).parent_path()
            : context.GetContentRoot();
        return PasteAssetToFolder(context, targetFolder.string());
    }
    if (m_Clipboard->GetKind() != EditorClipboardService::Kind::Actors) return false;

    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack || !context.CanEditScene()) return false;
    std::vector<std::vector<PrefabNode>> roots;
    if (!LoadClipboardRoots(m_Clipboard->GetActorJson(), roots)) return false;

    Actor* selected = context.GetSelection().ResolveActor(*scene);
    Actor* parent = selected ? selected->GetParent() : nullptr;
    Actor* nextSibling = selected ? scene->FindByID(NextSiblingID(*selected)) : nullptr;
    const uint64_t beforeSelection = context.GetSelection().GetActorID();
    const std::string before = SceneSerializer::SaveToString(*scene);
    std::string error;
    std::vector<uint64_t> pastedIDs;
    pastedIDs.reserve(roots.size());
    for (const std::vector<PrefabNode>& nodes : roots) {
        const uint64_t firstCloneID = FindNextActorID(*scene);
        Actor* pasted = InstantiatePrefabNodesAsCopy(*scene, nodes, parent, nextSibling,
                                                     &error, firstCloneID);
        if (!pasted) {
            SceneSerializer::LoadFromString(*scene, before);
            if (!error.empty()) Logger::Warn("[Editor] Paste actors failed: ", error);
            return false;
        }
        pastedIDs.push_back(pasted->GetID());
    }
    if (pastedIDs.empty()) {
        SceneSerializer::LoadFromString(*scene, before);
        return false;
    }
    const std::string after = SceneSerializer::SaveToString(*scene);
    SceneSerializer::LoadFromString(*scene, before);
    return stack->ExecuteCommand(
        EditorUndoUtil::MakeSceneSnapshotCommand(
            pastedIDs.size() > 1 ? "Paste Actors" : "Paste Actor", before, after,
            beforeSelection, pastedIDs.back()),
        context);
}

bool EditorCommandOperator::PasteAssetToFolder(EditorContext& context,
                                               const std::string& targetFolder) const
{
    if (!m_Clipboard ||
        m_Clipboard->GetKind() != EditorClipboardService::Kind::Asset ||
        m_Clipboard->GetAssetPaths().empty()) {
        return false;
    }
    bool pastedAny = false;
    for (const std::string& path : m_Clipboard->GetAssetPaths()) {
        pastedAny = EditorAssetOperator{}.CopyAssetToFolder(
            context, path, targetFolder) || pastedAny;
    }
    return pastedAny;
}

bool EditorCommandOperator::SetActorActive(EditorContext& context, uint64_t actorID,
                                           bool active) const
{
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    if (!actor || !context.GetCommandStack() || !context.CanEditScene()) return false;
    if (actor->IsActiveSelf() == active) return false;
    return context.GetCommandStack()->ExecuteCommand(
        EditorUndoUtil::MakeSetActiveCommand(*actor, active), context);
}

bool EditorCommandOperator::SetActorTag(EditorContext& context, uint64_t actorID,
                                        const std::string& tag) const
{
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    EditorCommandStack* stack = context.GetCommandStack();
    if (!actor || !stack || !context.CanEditScene()) return false;
    const std::string before = actor->GetTag();
    if (before == tag) return false;
    return stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>(
        "Set Actor Tag",
        [actorID, tag](EditorContext& value) {
            Scene* scene = value.GetScene();
            Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
            if (!actor) return false;
            actor->SetTag(tag);
            return true;
        },
        [actorID, before](EditorContext& value) {
            Scene* scene = value.GetScene();
            Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
            if (!actor) return false;
            actor->SetTag(before);
            return true;
        }), context);
}

bool EditorCommandOperator::SetActorLayer(EditorContext& context, uint64_t actorID,
                                          uint32_t layer) const
{
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    EditorCommandStack* stack = context.GetCommandStack();
    if (!actor || !stack || !context.CanEditScene()) return false;
    const uint32_t before = actor->GetLayer();
    if (before == layer) return false;
    return stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>(
        "Set Actor Layer",
        [actorID, layer](EditorContext& value) {
            Scene* scene = value.GetScene();
            Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
            if (!actor) return false;
            actor->SetLayer(layer);
            return true;
        },
        [actorID, before](EditorContext& value) {
            Scene* scene = value.GetScene();
            Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
            if (!actor) return false;
            actor->SetLayer(before);
            return true;
        }), context);
}

bool EditorCommandOperator::SetActorEditorFlags(EditorContext& context,
                                                uint64_t actorID,
                                                uint32_t flags) const
{
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    EditorCommandStack* stack = context.GetCommandStack();
    if (!actor || !stack || !context.CanEditScene()) return false;
    const uint32_t before = actor->GetEditorFlags();
    if (before == flags) return false;
    return stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>(
        "Set Actor Flags",
        [actorID, flags](EditorContext& value) {
            Scene* scene = value.GetScene();
            Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
            if (!actor) return false;
            actor->SetEditorFlags(flags);
            return true;
        },
        [actorID, before](EditorContext& value) {
            Scene* scene = value.GetScene();
            Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
            if (!actor) return false;
            actor->SetEditorFlags(before);
            return true;
        }), context);
}

bool EditorCommandOperator::SetActorStatic(EditorContext& context,
                                           uint64_t actorID,
                                           bool isStatic) const
{
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    if (!actor) return false;
    uint32_t flags = actor->GetEditorFlags();
    if (isStatic) flags |= 1u;
    else flags &= ~1u;
    return SetActorEditorFlags(context, actorID, flags);
}

bool EditorCommandOperator::SetSceneName(EditorContext& context, const std::string& name) const
{
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack || !context.CanEditScene()) return false;
    const std::string before = scene->GetName();
    if (before == name) return false;

    return stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>(
        "Set Scene Name",
        [name](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            scene->SetName(name);
            return true;
        },
        [before](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            scene->SetName(before);
            return true;
        }), context);
}

bool EditorCommandOperator::SetSceneGravity(EditorContext& context, const Vec3& gravity) const
{
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack || !context.CanEditScene()) return false;
    const Vec3 before = scene->GetPhysicsWorld().GetGravity();
    if (before.x == gravity.x && before.y == gravity.y && before.z == gravity.z) {
        return false;
    }

    return stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>(
        "Set Scene Gravity",
        [gravity](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            scene->GetPhysicsWorld().SetGravity(gravity);
            return true;
        },
        [before](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            scene->GetPhysicsWorld().SetGravity(before);
            return true;
        }), context);
}

bool EditorCommandOperator::SetSceneMainCameraHint(EditorContext& context,
                                                   uint64_t actorID) const
{
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack || !context.CanEditScene()) return false;
    if (actorID != 0) {
        Actor* actor = scene->FindByID(actorID);
        if (!actor || !actor->GetComponent<CameraComponent>()) return false;
    }
    const uint64_t before = scene->GetMainCameraHintActorID();
    if (before == actorID) return false;

    return stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>(
        "Set Scene Main Camera",
        [actorID](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            scene->SetMainCameraHintActorID(actorID);
            return true;
        },
        [before](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            scene->SetMainCameraHintActorID(before);
            return true;
        }), context);
}

bool EditorCommandOperator::SetSceneAmbientIntensity(EditorContext& context,
                                                     float intensity) const
{
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack || !context.CanEditScene()) return false;
    if (intensity < 0.0f) intensity = 0.0f;
    const float before = scene->GetAmbientIntensity();
    if (before == intensity) return false;

    return stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>(
        "Set Scene Ambient Intensity",
        [intensity](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            scene->SetAmbientIntensity(intensity);
            return true;
        },
        [before](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            scene->SetAmbientIntensity(before);
            return true;
        }), context);
}

bool EditorCommandOperator::SetActorsActive(
    EditorContext& context, const std::vector<uint64_t>& actorIDs, bool active) const
{
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack || !context.CanEditScene() || actorIDs.empty()) return false;

    std::vector<std::pair<uint64_t, bool>> before;
    for (uint64_t actorID : actorIDs) {
        Actor* actor = scene->FindByID(actorID);
        if (actor && actor->IsActiveSelf() != active) {
            before.emplace_back(actorID, actor->IsActiveSelf());
        }
    }
    if (before.empty()) return false;

    return stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>(
        "Set Actors Active",
        [before, active](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            bool changed = false;
            for (const auto& [actorID, ignored] : before) {
                (void)ignored;
                if (Actor* actor = scene->FindByID(actorID)) {
                    actor->SetActive(active);
                    changed = true;
                }
            }
            return changed;
        },
        [before](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            bool changed = false;
            for (const auto& [actorID, activeSelf] : before) {
                if (Actor* actor = scene->FindByID(actorID)) {
                    actor->SetActive(activeSelf);
                    changed = true;
                }
            }
            return changed;
        }), context);
}

bool EditorCommandOperator::SetActorsTag(
    EditorContext& context, const std::vector<uint64_t>& actorIDs,
    const std::string& tag) const
{
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack || !context.CanEditScene() || actorIDs.empty()) return false;

    std::vector<std::pair<uint64_t, std::string>> before;
    for (uint64_t actorID : actorIDs) {
        Actor* actor = scene->FindByID(actorID);
        if (actor && actor->GetTag() != tag) before.emplace_back(actorID, actor->GetTag());
    }
    if (before.empty()) return false;

    return stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>(
        "Set Actors Tag",
        [before, tag](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            bool changed = false;
            for (const auto& [actorID, ignored] : before) {
                (void)ignored;
                if (Actor* actor = scene->FindByID(actorID)) {
                    actor->SetTag(tag);
                    changed = true;
                }
            }
            return changed;
        },
        [before](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            bool changed = false;
            for (const auto& [actorID, tagBefore] : before) {
                if (Actor* actor = scene->FindByID(actorID)) {
                    actor->SetTag(tagBefore);
                    changed = true;
                }
            }
            return changed;
        }), context);
}

bool EditorCommandOperator::SetActorsLayer(
    EditorContext& context, const std::vector<uint64_t>& actorIDs,
    uint32_t layer) const
{
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack || !context.CanEditScene() || actorIDs.empty()) return false;

    std::vector<std::pair<uint64_t, uint32_t>> before;
    for (uint64_t actorID : actorIDs) {
        Actor* actor = scene->FindByID(actorID);
        if (actor && actor->GetLayer() != layer) before.emplace_back(actorID, actor->GetLayer());
    }
    if (before.empty()) return false;

    return stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>(
        "Set Actors Layer",
        [before, layer](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            bool changed = false;
            for (const auto& [actorID, ignored] : before) {
                (void)ignored;
                if (Actor* actor = scene->FindByID(actorID)) {
                    actor->SetLayer(layer);
                    changed = true;
                }
            }
            return changed;
        },
        [before](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            bool changed = false;
            for (const auto& [actorID, layerBefore] : before) {
                if (Actor* actor = scene->FindByID(actorID)) {
                    actor->SetLayer(layerBefore);
                    changed = true;
                }
            }
            return changed;
        }), context);
}

bool EditorCommandOperator::SetActorsEditorFlags(
    EditorContext& context, const std::vector<uint64_t>& actorIDs,
    uint32_t flags) const
{
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack || !context.CanEditScene() || actorIDs.empty()) return false;

    std::vector<std::pair<uint64_t, uint32_t>> before;
    for (uint64_t actorID : actorIDs) {
        Actor* actor = scene->FindByID(actorID);
        if (actor && actor->GetEditorFlags() != flags) {
            before.emplace_back(actorID, actor->GetEditorFlags());
        }
    }
    if (before.empty()) return false;

    return stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>(
        "Set Actors Flags",
        [before, flags](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            bool changed = false;
            for (const auto& [actorID, ignored] : before) {
                (void)ignored;
                if (Actor* actor = scene->FindByID(actorID)) {
                    actor->SetEditorFlags(flags);
                    changed = true;
                }
            }
            return changed;
        },
        [before](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            bool changed = false;
            for (const auto& [actorID, flagsBefore] : before) {
                if (Actor* actor = scene->FindByID(actorID)) {
                    actor->SetEditorFlags(flagsBefore);
                    changed = true;
                }
            }
            return changed;
        }), context);
}

bool EditorCommandOperator::SetActorsStatic(
    EditorContext& context, const std::vector<uint64_t>& actorIDs,
    bool isStatic) const
{
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack || !context.CanEditScene() || actorIDs.empty()) return false;

    std::vector<std::pair<uint64_t, uint32_t>> before;
    for (uint64_t actorID : actorIDs) {
        Actor* actor = scene->FindByID(actorID);
        if (!actor || actor->IsStatic() == isStatic) continue;
        before.emplace_back(actorID, actor->GetEditorFlags());
    }
    if (before.empty()) return false;

    return stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>(
        "Set Actors Static",
        [before, isStatic](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            bool changed = false;
            for (const auto& [actorID, ignored] : before) {
                (void)ignored;
                if (Actor* actor = scene->FindByID(actorID)) {
                    actor->SetStatic(isStatic);
                    changed = true;
                }
            }
            return changed;
        },
        [before](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            bool changed = false;
            for (const auto& [actorID, flagsBefore] : before) {
                if (Actor* actor = scene->FindByID(actorID)) {
                    actor->SetEditorFlags(flagsBefore);
                    changed = true;
                }
            }
            return changed;
        }), context);
}

bool EditorCommandOperator::SetActorsPosition(
    EditorContext& context, const std::vector<uint64_t>& actorIDs,
    const Vec3& position) const
{
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack || !context.CanEditScene() || actorIDs.empty()) return false;

    std::vector<std::pair<uint64_t, Vec3>> before;
    before.reserve(actorIDs.size());
    for (uint64_t actorID : actorIDs) {
        Actor* actor = scene->FindByID(actorID);
        if (!actor) continue;
        const Vec3 current = actor->GetTransform().position;
        if (current.x != position.x || current.y != position.y || current.z != position.z) {
            before.emplace_back(actorID, current);
        }
    }
    if (before.empty()) return false;

    return stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>(
        "Set Actors Position",
        [before, position](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            bool changed = false;
            for (const auto& [actorID, ignored] : before) {
                (void)ignored;
                if (Actor* actor = scene->FindByID(actorID)) {
                    actor->GetTransform().position = position;
                    changed = true;
                }
            }
            return changed;
        },
        [before](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            bool changed = false;
            for (const auto& [actorID, positionBefore] : before) {
                if (Actor* actor = scene->FindByID(actorID)) {
                    actor->GetTransform().position = positionBefore;
                    changed = true;
                }
            }
            return changed;
        }), context);
}

bool EditorCommandOperator::SetActorsRotation(
    EditorContext& context, const std::vector<uint64_t>& actorIDs,
    const Vec3& rotation) const
{
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack || !context.CanEditScene() || actorIDs.empty()) return false;

    std::vector<std::pair<uint64_t, Vec3>> before;
    before.reserve(actorIDs.size());
    for (uint64_t actorID : actorIDs) {
        Actor* actor = scene->FindByID(actorID);
        if (!actor) continue;
        const Vec3 current = actor->GetTransform().rotation;
        if (current.x != rotation.x || current.y != rotation.y ||
            current.z != rotation.z) {
            before.emplace_back(actorID, current);
        }
    }
    if (before.empty()) return false;

    return stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>(
        "Set Actors Rotation",
        [before, rotation](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            bool changed = false;
            for (const auto& [actorID, ignored] : before) {
                (void)ignored;
                if (Actor* actor = scene->FindByID(actorID)) {
                    actor->GetTransform().rotation = rotation;
                    changed = true;
                }
            }
            return changed;
        },
        [before](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            bool changed = false;
            for (const auto& [actorID, rotationBefore] : before) {
                if (Actor* actor = scene->FindByID(actorID)) {
                    actor->GetTransform().rotation = rotationBefore;
                    changed = true;
                }
            }
            return changed;
        }), context);
}

bool EditorCommandOperator::SetActorsScale(
    EditorContext& context, const std::vector<uint64_t>& actorIDs,
    const Vec3& scale) const
{
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack || !context.CanEditScene() || actorIDs.empty()) return false;

    std::vector<std::pair<uint64_t, Vec3>> before;
    before.reserve(actorIDs.size());
    for (uint64_t actorID : actorIDs) {
        Actor* actor = scene->FindByID(actorID);
        if (!actor) continue;
        const Vec3 current = actor->GetTransform().scale;
        if (current.x != scale.x || current.y != scale.y || current.z != scale.z) {
            before.emplace_back(actorID, current);
        }
    }
    if (before.empty()) return false;

    return stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>(
        "Set Actors Scale",
        [before, scale](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            bool changed = false;
            for (const auto& [actorID, ignored] : before) {
                (void)ignored;
                if (Actor* actor = scene->FindByID(actorID)) {
                    actor->GetTransform().scale = scale;
                    changed = true;
                }
            }
            return changed;
        },
        [before](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            bool changed = false;
            for (const auto& [actorID, scaleBefore] : before) {
                if (Actor* actor = scene->FindByID(actorID)) {
                    actor->GetTransform().scale = scaleBefore;
                    changed = true;
                }
            }
            return changed;
        }), context);
}

bool EditorCommandOperator::MoveActor(EditorContext& context, uint64_t actorID,
                                      uint64_t afterParentID,
                                      uint64_t afterNextSiblingID) const
{
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    if (!actor || !context.GetCommandStack() || !context.CanEditScene()) return false;
    const uint64_t beforeParentID = actor->GetParent() ? actor->GetParent()->GetID() : 0;
    const uint64_t beforeNextSiblingID = NextSiblingID(*actor);
    if (beforeParentID == afterParentID && beforeNextSiblingID == afterNextSiblingID) return false;
    return context.GetCommandStack()->ExecuteCommand(
        EditorUndoUtil::MakeMoveActorCommand(
            *actor, beforeParentID, beforeNextSiblingID,
            afterParentID, afterNextSiblingID),
        context);
}

bool EditorCommandOperator::UnparentActor(EditorContext& context, uint64_t actorID) const
{
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    Actor* parent = actor ? actor->GetParent() : nullptr;
    if (!scene || !actor || !parent) return false;
    Actor* grandParent = parent->GetParent();
    const uint64_t grandParentID = grandParent ? grandParent->GetID() : 0;
    const uint64_t nextAfterParentID = NextSiblingID(*parent);
    return MoveActor(context, actorID, grandParentID, nextAfterParentID);
}

bool EditorCommandOperator::MoveActorUp(EditorContext& context, uint64_t actorID) const
{
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    if (!actor) return false;
    const uint64_t previousID = PreviousSiblingID(*actor);
    if (!previousID) return false;
    const uint64_t parentID = actor->GetParent() ? actor->GetParent()->GetID() : 0;
    return MoveActor(context, actorID, parentID, previousID);
}

bool EditorCommandOperator::MoveActorDown(EditorContext& context, uint64_t actorID) const
{
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    if (!actor) return false;
    const uint64_t nextID = NextSiblingID(*actor);
    if (!nextID) return false;
    Actor* next = scene ? scene->FindByID(nextID) : nullptr;
    if (!next) return false;
    const uint64_t parentID = actor->GetParent() ? actor->GetParent()->GetID() : 0;
    return MoveActor(context, actorID, parentID, NextSiblingID(*next));
}

bool EditorDragDropOperator::ApplyActorDrop(EditorContext& context, uint64_t actorID,
                                            uint64_t afterParentID,
                                            uint64_t afterNextSiblingID) const
{
    if (EditorOperators* operators = context.GetOperators()) {
        return operators->Commands().MoveActor(
            context, actorID, afterParentID, afterNextSiblingID);
    }
    EditorCommandOperator commands;
    return commands.MoveActor(context, actorID, afterParentID, afterNextSiblingID);
}

bool EditorDragDropOperator::ApplyAssetDrop(EditorContext& context,
                                            const std::string& assetPath,
                                            const std::string& targetPath) const
{
    (void)context;
    return !assetPath.empty() && !targetPath.empty();
}

bool EditorComponentOperator::AddComponent(EditorContext& context, uint64_t actorID,
                                           const std::string& typeName,
                                           const nlohmann::json& initialData) const
{
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    if (!actor || typeName.empty() || actor->HasComponentType(typeName) ||
        !context.GetCommandStack() || !context.CanEditScene()) {
        return false;
    }
    return context.GetCommandStack()->ExecuteCommand(
        EditorUndoUtil::MakeAddComponentCommand(*actor, typeName, initialData), context);
}

bool EditorComponentOperator::AddComponents(
    EditorContext& context, const std::vector<uint64_t>& actorIDs,
    const std::string& typeName, const nlohmann::json& initialData) const
{
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack || !context.CanEditScene() ||
        actorIDs.empty() || typeName.empty() ||
        !ComponentRegistry::Get().IsRegistered(typeName)) {
        return false;
    }

    std::vector<uint64_t> targets;
    targets.reserve(actorIDs.size());
    for (uint64_t actorID : actorIDs) {
        Actor* actor = scene->FindByID(actorID);
        if (!actor || actor->HasComponentType(typeName)) return false;
        targets.push_back(actorID);
    }
    if (targets.empty()) return false;

    const std::string before = SceneSerializer::SaveToString(*scene);
    const uint64_t beforeSelection = context.GetSelection().GetActorID();
    bool changed = false;
    for (uint64_t actorID : targets) {
        Actor* actor = scene->FindByID(actorID);
        if (!actor) continue;
        Component* component = ComponentRegistry::Get().Create(typeName, *actor);
        if (!component) {
            SceneSerializer::LoadFromString(*scene, before);
            return false;
        }
        if (!initialData.is_null() && !initialData.empty()) {
            component->Deserialize(initialData);
        }
        changed = true;
    }
    const std::string after = SceneSerializer::SaveToString(*scene);
    SceneSerializer::LoadFromString(*scene, before);
    if (!changed || before == after) return false;

    return stack->ExecuteCommand(
        EditorUndoUtil::MakeSceneSnapshotCommand(
            "Add Components", before, after, beforeSelection, beforeSelection),
        context);
}

bool EditorComponentOperator::RemoveComponent(EditorContext& context, uint64_t actorID,
                                              const std::string& typeName) const
{
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    Component* component = actor ? actor->GetComponentByTypeName(typeName) : nullptr;
    return actor && component && RemoveComponent(context, *actor, *component);
}

bool EditorComponentOperator::RemoveComponent(EditorContext& context, Actor& actor,
                                              const Component& component) const
{
    if (!context.GetCommandStack() || !context.CanEditScene()) return false;
    return context.GetCommandStack()->ExecuteCommand(
        EditorUndoUtil::MakeRemoveComponentCommand(actor, component), context);
}

bool EditorComponentOperator::RemoveComponents(
    EditorContext& context, const std::vector<uint64_t>& actorIDs,
    const std::string& typeName) const
{
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack || !context.CanEditScene() ||
        actorIDs.empty() || typeName.empty()) {
        return false;
    }

    std::vector<uint64_t> targets;
    targets.reserve(actorIDs.size());
    for (uint64_t actorID : actorIDs) {
        Actor* actor = scene->FindByID(actorID);
        if (!actor || !actor->HasComponentType(typeName)) return false;
        targets.push_back(actorID);
    }
    if (targets.empty()) return false;

    const std::string before = SceneSerializer::SaveToString(*scene);
    const uint64_t beforeSelection = context.GetSelection().GetActorID();
    bool changed = false;
    for (uint64_t actorID : targets) {
        if (Actor* actor = scene->FindByID(actorID)) {
            changed = actor->RemoveComponentByTypeName(typeName) || changed;
        }
    }
    const std::string after = SceneSerializer::SaveToString(*scene);
    SceneSerializer::LoadFromString(*scene, before);
    if (!changed || before == after) return false;

    return stack->ExecuteCommand(
        EditorUndoUtil::MakeSceneSnapshotCommand(
            "Remove Components", before, after, beforeSelection, beforeSelection),
        context);
}

bool EditorComponentOperator::SetComponentPropertyForActors(
    EditorContext& context, const std::vector<uint64_t>& actorIDs,
    const std::string& typeName, const std::string& propertyName,
    const nlohmann::json& value) const
{
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack || !context.CanEditScene() || actorIDs.empty() ||
        typeName.empty() || propertyName.empty()) {
        return false;
    }

    std::vector<uint64_t> targets;
    targets.reserve(actorIDs.size());
    for (uint64_t actorID : actorIDs) {
        Actor* actor = scene->FindByID(actorID);
        Component* component = actor ? actor->GetComponentByTypeName(typeName) : nullptr;
        if (!component) return false;

        nlohmann::json data = nlohmann::json::object();
        component->Serialize(data);
        const bool compatibleType = data.is_object() && data.contains(propertyName) &&
            (data[propertyName].type() == value.type() ||
             (data[propertyName].is_number() && value.is_number()));
        if (!compatibleType) {
            return false;
        }
        targets.push_back(actorID);
    }
    if (targets.empty()) return false;

    const std::string before = SceneSerializer::SaveToString(*scene);
    const uint64_t beforeSelection = context.GetSelection().GetActorID();
    bool changed = false;
    for (uint64_t actorID : targets) {
        Actor* actor = scene->FindByID(actorID);
        Component* component = actor ? actor->GetComponentByTypeName(typeName) : nullptr;
        if (!component) return false;

        nlohmann::json data = nlohmann::json::object();
        component->Serialize(data);
        if (!data.is_object() || !data.contains(propertyName)) return false;
        if (data[propertyName] == value) continue;
        data[propertyName] = value;
        component->Deserialize(data);
        changed = true;
    }
    const std::string after = SceneSerializer::SaveToString(*scene);
    SceneSerializer::LoadFromString(*scene, before);
    if (!changed || before == after) return false;

    const std::string label = "Set " + typeName + "." + propertyName;
    return stack->ExecuteCommand(
        EditorUndoUtil::MakeSceneSnapshotCommand(
            label.c_str(), before, after, beforeSelection, beforeSelection),
        context);
}

bool EditorComponentOperator::SetJson(EditorContext& context, uint64_t actorID,
                                      const std::string& typeName,
                                      const nlohmann::json& beforeJson,
                                      const nlohmann::json& afterJson,
                                      const std::string& label) const
{
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    if (!actor) return false;
    return SetProperty(context, *actor, typeName,
                       label.empty() ? typeName : label,
                       beforeJson, afterJson);
}

bool EditorComponentOperator::SetProperty(EditorContext& context, Actor& actor,
                                          const std::string& componentType,
                                          const std::string& propertyName,
                                          const nlohmann::json& beforeJson,
                                          const nlohmann::json& afterJson) const
{
    if (componentType.empty() || beforeJson == afterJson ||
        !context.GetCommandStack() || !context.CanEditScene()) {
        return false;
    }
    return context.GetCommandStack()->ExecuteCommand(
        EditorUndoUtil::MakeSetPropertyCommand(
            actor, componentType, propertyName, beforeJson, afterJson),
        context);
}

void EditorTransactionOperator::BeginSnapshot(EditorSceneTransaction& transaction,
                                              const char* label,
                                              const std::string& beforeJson,
                                              uint64_t selection) const
{
    transaction.Begin(label, beforeJson, selection);
}

bool EditorTransactionOperator::CommitIfChanged(EditorContext& context,
                                                EditorSceneTransaction& transaction) const
{
    return transaction.Commit(context);
}

bool EditorTransactionOperator::CommitSceneSnapshot(
    EditorContext& context, const char* label, const std::string& beforeJson,
    const std::string& afterJson, uint64_t beforeSelection,
    uint64_t afterSelection) const
{
    if (beforeJson.empty() || afterJson.empty() || beforeJson == afterJson) return false;
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack || !context.CanEditScene()) return false;
    SceneSerializer::LoadFromString(*scene, beforeJson);
    return stack->ExecuteCommand(EditorUndoUtil::MakeSceneSnapshotCommand(
        label ? label : "Scene Edit", beforeJson, afterJson,
        beforeSelection, afterSelection), context);
}

bool EditorTransactionOperator::CommitComponentProperty(
    EditorContext& context, Actor& actor, const std::string& componentType,
    const std::string& propertyName, const nlohmann::json& beforeJson,
    const nlohmann::json& afterJson) const
{
    if (componentType.empty() || propertyName.empty() || beforeJson == afterJson) return false;
    EditorCommandStack* stack = context.GetCommandStack();
    if (!stack || !context.CanEditScene()) return false;
    return stack->ExecuteCommand(
        EditorUndoUtil::MakeSetPropertyCommand(
            actor, componentType, propertyName, beforeJson, afterJson),
        context);
}

void EditorTransactionOperator::Cancel(EditorSceneTransaction& transaction) const
{
    transaction.Cancel();
}

bool EditorAssetOperator::Refresh(EditorContext& context) const
{
    EditorAssetRegistry* registry = context.GetAssetRegistry();
    if (!registry) return false;
    registry->Refresh();
    return true;
}

bool EditorAssetOperator::WatchIfDue(EditorContext& context, float deltaSeconds,
                                     float& accumulator, float intervalSeconds) const
{
    accumulator += deltaSeconds;
    if (accumulator < intervalSeconds) return false;
    accumulator = 0.0f;
    EditorAssetRegistry* registry = context.GetAssetRegistry();
    return registry && registry->WatchForChanges();
}

bool EditorAssetOperator::CreateFolder(EditorContext& context, const std::string& folderPath) const
{
    const auto start = EditorOperatorClock::now();
    const std::filesystem::path folder = ResolveEditorAssetPath(context, folderPath);
    if (folder.empty() || ContentOrSourceRoot(context, folder).empty() ||
        std::filesystem::exists(folder)) {
        RecordAssetOperatorEvent(context, "Create Folder", folderPath,
                                 ElapsedOperatorMs(start), false);
        return false;
    }
    auto execute = [folder](EditorContext&) {
        std::error_code error;
        return std::filesystem::create_directories(folder, error) && !error;
    };
    auto undo = [folder](EditorContext&) {
        std::error_code error;
        return std::filesystem::remove(folder, error) && !error;
    };
    EditorCommandStack* stack = context.GetCommandStack();
    const bool ok = stack
        ? stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>(
              "Create Folder", execute, undo), context)
        : execute(context);
    if (ok) Refresh(context);
    RecordAssetOperatorEvent(context, "Create Folder", folder.string(),
                             ElapsedOperatorMs(start), ok);
    return ok;
}

bool EditorAssetOperator::RenameFolder(EditorContext& context, const std::string& folderPath,
                                       const std::string& newNameOrPath) const
{
    const std::filesystem::path folder = ResolveEditorAssetPath(context, folderPath);
    const std::filesystem::path requested(newNameOrPath);
    const std::filesystem::path target = requested.is_absolute()
        ? requested
        : folder.parent_path() / requested;
    const std::filesystem::path root = ContentOrSourceRoot(context, folder);
    if (newNameOrPath.empty() || !std::filesystem::is_directory(folder) ||
        root.empty() || NormalizeAbsolute(folder) == NormalizeAbsolute(root) ||
        ContentOrSourceRoot(context, target).empty()) {
        return false;
    }
    const bool ok = RenameAsset(context, folder.string(), target.string());
    if (ok) EditorSelectionOperator{}.Clear(context);
    return ok;
}

bool EditorAssetOperator::DeleteFolder(EditorContext& context, const std::string& folderPath) const
{
    const auto start = EditorOperatorClock::now();
    const std::filesystem::path folder = ResolveEditorAssetPath(context, folderPath);
    const std::filesystem::path root = ContentOrSourceRoot(context, folder);
    if (!std::filesystem::is_directory(folder) || root.empty() ||
        NormalizeAbsolute(folder) == NormalizeAbsolute(root)) {
        RecordAssetOperatorEvent(context, "Delete Folder", folderPath,
                                 ElapsedOperatorMs(start), false);
        return false;
    }

    FolderSnapshot snapshot;
    if (!CaptureFolderSnapshot(folder, snapshot)) {
        RecordAssetOperatorEvent(context, "Delete Folder", folderPath,
                                 ElapsedOperatorMs(start), false);
        return false;
    }
    std::vector<AssetRecord> databaseRecords =
        CaptureAssetDatabaseRecordsUnderRoot(context, folder);

    auto execute = [snapshot, databaseRecords](EditorContext& commandContext) {
        if (!RemoveAssetDatabaseRecords(commandContext, databaseRecords)) return false;
        const bool ok = DeleteFolderSnapshot(snapshot);
        if (!ok) {
            RestoreAssetDatabaseRecords(commandContext, databaseRecords);
            return false;
        }
        RefreshAssetRegistryIfPresent(commandContext);
        return ok;
    };
    auto undo = [snapshot, databaseRecords](EditorContext& commandContext) {
        const bool ok = RestoreFolderSnapshot(snapshot);
        if (!ok) return false;
        if (!RestoreAssetDatabaseRecords(commandContext, databaseRecords)) return false;
        RefreshAssetRegistryIfPresent(commandContext);
        return ok;
    };
    EditorCommandStack* stack = context.GetCommandStack();
    const bool ok = stack
        ? stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>(
              "Delete Folder", execute, undo), context)
        : execute(context);
    if (ok) Refresh(context);
    RecordAssetOperatorEvent(context, "Delete Folder", folder.string(),
                             ElapsedOperatorMs(start), ok);
    return ok;
}

bool EditorAssetOperator::DeleteAsset(EditorContext& context, const std::string& path) const
{
    const auto start = EditorOperatorClock::now();
    const std::filesystem::path resolved = ResolveEditorAssetPath(context, path);
    if (resolved.empty() || ContentOrSourceRoot(context, resolved).empty() ||
        !std::filesystem::is_regular_file(resolved)) {
        RecordAssetOperatorEvent(context, "Delete Asset", path,
                                 ElapsedOperatorMs(start), false);
        return false;
    }
    try {
        const std::string content = ReadFileContent(resolved);
        if (EditorCommandStack* stack = context.GetCommandStack()) {
            if (!stack->ExecuteCommand(
                    std::make_unique<DeleteAssetCommand>(resolved.string(), content), context)) {
                RecordAssetOperatorEvent(context, "Delete Asset", resolved.string(),
                                         ElapsedOperatorMs(start), false);
                return false;
            }
        } else {
            std::filesystem::remove(resolved);
        }
        Refresh(context);
        EditorSelectionOperator{}.Clear(context);
        RecordAssetOperatorEvent(context, "Delete Asset", resolved.string(),
                                 ElapsedOperatorMs(start), true);
        return true;
    } catch (...) {
        RecordAssetOperatorEvent(context, "Delete Asset", resolved.string(),
                                 ElapsedOperatorMs(start), false);
        return false;
    }
}

bool EditorAssetOperator::RenameAsset(EditorContext& context, const std::string& path,
                                      const std::string& newNameOrPath) const
{
    const auto start = EditorOperatorClock::now();
    if (path.empty() || newNameOrPath.empty()) {
        RecordAssetOperatorEvent(context, "Rename Asset", path,
                                 ElapsedOperatorMs(start), false);
        return false;
    }
    namespace fs = std::filesystem;
    const fs::path src = ResolveEditorAssetPath(context, path);
    const fs::path requested(newNameOrPath);
    const fs::path dst = requested.is_absolute()
        ? requested
        : src.parent_path() / requested;
    if (NormalizeAbsolute(src) == NormalizeAbsolute(dst) ||
        ContentOrSourceRoot(context, src).empty() ||
        ContentOrSourceRoot(context, dst).empty() ||
        AssetRenameTargetExists(src, dst)) {
        RecordAssetOperatorEvent(context, "Rename Asset", src.string(),
                                 ElapsedOperatorMs(start), false,
                                 "target=" + dst.string());
        return false;
    }
    try {
        if (EditorCommandStack* stack = context.GetCommandStack()) {
            if (!stack->ExecuteCommand(
                    std::make_unique<RenameAssetCommand>(src.string(), dst.string()), context)) {
                RecordAssetOperatorEvent(context, "Rename Asset", src.string(),
                                         ElapsedOperatorMs(start), false,
                                         "target=" + dst.string());
                return false;
            }
        } else {
            fs::rename(src, dst);
        }
        Refresh(context);
        EditorSelectionOperator{}.SelectAsset(context, dst.string());
        RecordAssetOperatorEvent(context, "Rename Asset", src.string(),
                                 ElapsedOperatorMs(start), true,
                                 "target=" + dst.string());
        return true;
    } catch (...) {
        RecordAssetOperatorEvent(context, "Rename Asset", src.string(),
                                 ElapsedOperatorMs(start), false,
                                 "target=" + dst.string());
        return false;
    }
}

bool EditorAssetOperator::MoveAsset(EditorContext& context, const std::string& path,
                                    const std::string& targetFolder) const
{
    const auto start = EditorOperatorClock::now();
    const std::filesystem::path src = ResolveEditorAssetPath(context, path);
    const std::filesystem::path folder = ResolveEditorAssetPath(context, targetFolder);
    if (!std::filesystem::is_regular_file(src) || !std::filesystem::is_directory(folder) ||
        ContentOrSourceRoot(context, src).empty() || ContentOrSourceRoot(context, folder).empty()) {
        RecordAssetOperatorEvent(context, "Move Asset", path,
                                 ElapsedOperatorMs(start), false,
                                 "targetFolder=" + targetFolder);
        return false;
    }
    const bool ok = RenameAsset(context, src.string(), (folder / src.filename()).string());
    RecordAssetOperatorEvent(context, "Move Asset", src.string(),
                             ElapsedOperatorMs(start), ok,
                             "targetFolder=" + folder.string());
    return ok;
}

bool EditorAssetOperator::MoveFolder(EditorContext& context, const std::string& folderPath,
                                     const std::string& targetFolder) const
{
    const auto start = EditorOperatorClock::now();
    const std::filesystem::path src = ResolveEditorAssetPath(context, folderPath);
    const std::filesystem::path folder = ResolveEditorAssetPath(context, targetFolder);
    const std::filesystem::path root = ContentOrSourceRoot(context, src);
    if (!std::filesystem::is_directory(src) || !std::filesystem::is_directory(folder) ||
        root.empty() || ContentOrSourceRoot(context, folder).empty() ||
        NormalizeAbsolute(src) == NormalizeAbsolute(root)) {
        RecordAssetOperatorEvent(context, "Move Folder", folderPath,
                                 ElapsedOperatorMs(start), false,
                                 "targetFolder=" + targetFolder);
        return false;
    }
    if (IsWithinRoot(folder, src)) {
        RecordAssetOperatorEvent(context, "Move Folder", src.string(),
                                 ElapsedOperatorMs(start), false,
                                 "targetFolder=" + folder.string());
        return false;
    }
    const bool ok = RenameAsset(context, src.string(), (folder / src.filename()).string());
    if (ok) EditorSelectionOperator{}.Clear(context);
    RecordAssetOperatorEvent(context, "Move Folder", src.string(),
                             ElapsedOperatorMs(start), ok,
                             "targetFolder=" + folder.string());
    return ok;
}

bool EditorAssetOperator::CopyAssetToFolder(EditorContext& context, const std::string& path,
                                            const std::string& targetFolder) const
{
    const auto start = EditorOperatorClock::now();
    if (path.empty() || targetFolder.empty()) {
        RecordAssetOperatorEvent(context, "Copy Asset", path,
                                 ElapsedOperatorMs(start), false,
                                 "targetFolder=" + targetFolder);
        return false;
    }
    namespace fs = std::filesystem;
    const fs::path src = ResolveEditorAssetPath(context, path);
    const fs::path folder = ResolveEditorAssetPath(context, targetFolder);
    if (ContentOrSourceRoot(context, src).empty() ||
        ContentOrSourceRoot(context, folder).empty() ||
        !fs::is_regular_file(src) || !fs::is_directory(folder)) {
        RecordAssetOperatorEvent(context, "Copy Asset", src.string(),
                                 ElapsedOperatorMs(start), false,
                                 "targetFolder=" + folder.string());
        return false;
    }

    fs::path dst = folder / src.filename();
    if (NormalizeAbsolute(dst) == NormalizeAbsolute(src) || fs::exists(dst)) {
        dst = MakeUniquePath(folder, src.stem().string() + "_Copy",
                             src.extension().string());
    }

    try {
        const std::string content = ReadFileContent(src);
        EditorCommandStack* stack = context.GetCommandStack();
        const bool ok = stack
            ? stack->ExecuteCommand(
                  std::make_unique<CreateAssetCommand>(dst.string(), content), context)
            : (WriteFileContent(dst, content) && EnsureAssetMeta(dst));
        if (!ok) {
            RecordAssetOperatorEvent(context, "Copy Asset", src.string(),
                                     ElapsedOperatorMs(start), false,
                                     "target=" + dst.string());
            return false;
        }
        AssetManager::Get().Load<Asset>(dst.string());
        Refresh(context);
        EditorSelectionOperator{}.SelectAsset(context, dst.string());
        RecordAssetOperatorEvent(context, "Copy Asset", src.string(),
                                 ElapsedOperatorMs(start), true,
                                 "target=" + dst.string());
        return true;
    } catch (...) {
        RecordAssetOperatorEvent(context, "Copy Asset", src.string(),
                                 ElapsedOperatorMs(start), false,
                                 "targetFolder=" + folder.string());
        return false;
    }
}

bool EditorAssetOperator::DuplicateAsset(EditorContext& context, const std::string& path) const
{
    const auto start = EditorOperatorClock::now();
    if (path.empty()) {
        RecordAssetOperatorEvent(context, "Duplicate Asset", path,
                                 ElapsedOperatorMs(start), false);
        return false;
    }
    namespace fs = std::filesystem;
    const fs::path src = ResolveEditorAssetPath(context, path);
    if (ContentOrSourceRoot(context, src).empty() || !std::filesystem::is_regular_file(src)) {
        RecordAssetOperatorEvent(context, "Duplicate Asset", src.string(),
                                 ElapsedOperatorMs(start), false);
        return false;
    }
    const fs::path dst = MakeUniquePath(
        src.parent_path(), src.stem().string() + "_Copy", src.extension().string());
    try {
        const std::string content = ReadFileContent(src);
        if (EditorCommandStack* stack = context.GetCommandStack()) {
            if (!stack->ExecuteCommand(
                    std::make_unique<CreateAssetCommand>(dst.string(), content), context)) {
                RecordAssetOperatorEvent(context, "Duplicate Asset", src.string(),
                                         ElapsedOperatorMs(start), false,
                                         "target=" + dst.string());
                return false;
            }
        } else {
            if (!WriteFileContent(dst, content) || !EnsureAssetMeta(dst)) {
                RecordAssetOperatorEvent(context, "Duplicate Asset", src.string(),
                                         ElapsedOperatorMs(start), false,
                                         "target=" + dst.string());
                return false;
            }
        }
        AssetManager::Get().Load<Asset>(dst.string());
        Refresh(context);
        EditorSelectionOperator{}.SelectAsset(context, dst.string());
        RecordAssetOperatorEvent(context, "Duplicate Asset", src.string(),
                                 ElapsedOperatorMs(start), true,
                                 "target=" + dst.string());
        return true;
    } catch (...) {
        RecordAssetOperatorEvent(context, "Duplicate Asset", src.string(),
                                 ElapsedOperatorMs(start), false);
        return false;
    }
}

bool EditorAssetOperator::CreateAssetFromTemplate(EditorContext& context,
                                                  const std::string& folderPath,
                                                  const std::string& templateID) const
{
    const auto start = EditorOperatorClock::now();
    const std::filesystem::path folder = ResolveEditorAssetPath(context, folderPath);
    if (folder.empty() || ContentOrSourceRoot(context, folder).empty()) {
        RecordAssetOperatorEvent(context, "Create Asset From Template", folderPath,
                                 ElapsedOperatorMs(start), false,
                                 "template=" + templateID);
        return false;
    }
    std::error_code error;
    if (std::filesystem::exists(folder, error) &&
        !std::filesystem::is_directory(folder, error)) {
        RecordAssetOperatorEvent(context, "Create Asset From Template", folder.string(),
                                 ElapsedOperatorMs(start), false,
                                 "template=" + templateID);
        return false;
    }
    const std::filesystem::path path = MakeUniquePath(
        folder, TemplateBaseNameFor(templateID), TemplateExtensionFor(templateID));
    if (templateID == "prefab") {
        if (!CreatePrefabTemplateAsset(context, path)) {
            RecordAssetOperatorEvent(context, "Create Asset From Template", path.string(),
                                     ElapsedOperatorMs(start), false,
                                     "template=" + templateID);
            return false;
        }
        Refresh(context);
        EditorSelectionOperator{}.SelectAsset(context, path.string());
        RecordAssetOperatorEvent(context, "Create Asset From Template", path.string(),
                                 ElapsedOperatorMs(start), true,
                                 "template=" + templateID);
        return true;
    }
    EditorCommandStack* stack = context.GetCommandStack();
    if (!stack || !stack->ExecuteCommand(
            std::make_unique<CreateAssetCommand>(
                path.string(), TemplateContentFor(templateID)), context)) {
        RecordAssetOperatorEvent(context, "Create Asset From Template", path.string(),
                                 ElapsedOperatorMs(start), false,
                                 "template=" + templateID);
        return false;
    }
    Refresh(context);
    EditorSelectionOperator{}.SelectAsset(context, path.string());
    RecordAssetOperatorEvent(context, "Create Asset From Template", path.string(),
                             ElapsedOperatorMs(start), true,
                             "template=" + templateID);
    return true;
}

bool EditorAssetOperator::OpenAsset(EditorContext& context, const std::string& path) const
{
    const auto start = EditorOperatorClock::now();
    const std::filesystem::path assetPath = ResolveEditorAssetPath(context, path);
    if (assetPath.empty() || !std::filesystem::exists(assetPath)) {
        RecordAssetOperatorEvent(context, "Open Asset", path,
                                 ElapsedOperatorMs(start), false);
        return false;
    }
    const EditorAssetType type = EditorAssetRegistry::Classify(assetPath);
    if (type == EditorAssetType::Scene) {
        SceneRenderLayer* layer = context.GetSceneLayer();
        if (!layer) {
            RecordAssetOperatorEvent(context, "Open Asset", assetPath.string(),
                                     ElapsedOperatorMs(start), false,
                                     "type=Scene");
            return false;
        }
        if (layer->IsDirty()) {
            Logger::Warn("[Editor] Refusing to open scene asset with unsaved changes: ",
                         assetPath.string());
            RecordAssetOperatorEvent(context, "Open Asset", assetPath.string(),
                                     ElapsedOperatorMs(start), false,
                                     "type=Scene;dirtyScene=true");
            return false;
        }
        if (layer->LoadScene(assetPath.string())) {
            context.SetSceneViewMode(EditorWorldViewMode::EditorWorld);
            context.GetSelection().Clear();
            if (EditorCommandStack* stack = context.GetCommandStack()) stack->Clear();
            if (EditorProject* project = context.GetProject()) project->SetLastScenePath(assetPath.string());
            RecordAssetOperatorEvent(context, "Open Asset", assetPath.string(),
                                     ElapsedOperatorMs(start), true,
                                     "type=Scene");
            return true;
        }
        RecordAssetOperatorEvent(context, "Open Asset", assetPath.string(),
                                 ElapsedOperatorMs(start), false,
                                 "type=Scene");
        return false;
    }
    if (IsTextLikeAsset(assetPath, type) && OpenExternalFile(assetPath)) {
        RecordAssetOperatorEvent(context, "Open Asset", assetPath.string(),
                                 ElapsedOperatorMs(start), true,
                                 "type=ExternalText");
        return true;
    }
    EditorSelectionOperator{}.SelectAsset(context, assetPath.string());
    context.RequestPanelFocus("inspector");
    Logger::Info("[Editor] Open asset: ", assetPath.string());
    RecordAssetOperatorEvent(context, "Open Asset", assetPath.string(),
                             ElapsedOperatorMs(start), true,
                             "type=Inspector");
    return true;
}

bool EditorAssetOperator::RevealAsset(EditorContext& context, const std::string& path) const
{
    const auto start = EditorOperatorClock::now();
    const std::filesystem::path assetPath = ResolveEditorAssetPath(context, path);
    if (assetPath.empty() || !std::filesystem::exists(assetPath)) {
        RecordAssetOperatorEvent(context, "Reveal Asset", path,
                                 ElapsedOperatorMs(start), false);
        return false;
    }
#if defined(_WIN32)
    const bool ok = RevealExternalPath(assetPath);
    RecordAssetOperatorEvent(context, "Reveal Asset", assetPath.string(),
                             ElapsedOperatorMs(start), ok);
    return ok;
#else
    Logger::Info("[Editor] Reveal asset: ", assetPath.string());
    RecordAssetOperatorEvent(context, "Reveal Asset", assetPath.string(),
                             ElapsedOperatorMs(start), true);
    return true;
#endif
}

std::vector<EditorAssetOperator::SceneReferenceInfo>
EditorAssetOperator::FindSceneReferences(EditorContext& context,
                                         const std::string& path) const
{
    const auto start = EditorOperatorClock::now();
    std::vector<SceneReferenceInfo> result;
    Scene* scene = context.GetInspectorScene();
    const std::filesystem::path assetPath = ResolveEditorAssetPath(context, path);
    if (!scene || assetPath.empty() ||
        ContentOrSourceRoot(context, assetPath).empty()) {
        RecordAssetOperatorEvent(context, "Find Scene References", path,
                                 ElapsedOperatorMs(start), false);
        return result;
    }

    const std::filesystem::path normalizedAsset = NormalizeAbsolute(assetPath);
    std::string scenePath;
    if (const SceneRenderLayer* layer = context.GetSceneLayer()) {
        if (layer->HasFilePath()) {
            scenePath = ProjectRelativeReferencePath(
                context, std::filesystem::path(layer->GetSceneFilePath()));
        }
    }
    scene->ForEach([&](Actor& actor) {
        if (!actor.GetPrefabAssetPath().empty() &&
            AssetReferenceStringMatches(context, normalizedAsset,
                                        actor.GetPrefabAssetPath())) {
            SceneReferenceInfo info;
            info.scenePath = scenePath;
            info.actorID = actor.GetID();
            info.actorName = actor.GetName();
            info.componentType = "Prefab";
            info.jsonPath = "/prefab";
            info.valuePreview = actor.GetPrefabAssetPath();
            result.push_back(std::move(info));
        }

        actor.ForEachComponent([&](Component& component) {
            nlohmann::json data = nlohmann::json::object();
            component.Serialize(data);
            FindAssetReferencesInJson(
                context, normalizedAsset, data, "", result,
                scenePath,
                actor.GetID(), actor.GetName(), component.GetTypeName());
        });
    });

    std::stable_sort(result.begin(), result.end(),
        [](const SceneReferenceInfo& left, const SceneReferenceInfo& right) {
            if (left.actorName != right.actorName) return left.actorName < right.actorName;
            if (left.actorID != right.actorID) return left.actorID < right.actorID;
            if (left.componentType != right.componentType) {
                return left.componentType < right.componentType;
            }
            return left.jsonPath < right.jsonPath;
        });

    RecordAssetOperatorEvent(context, "Find Scene References",
                             normalizedAsset.string(), ElapsedOperatorMs(start), true,
                             "count=" + std::to_string(result.size()));
    return result;
}

std::vector<EditorAssetOperator::SceneReferenceInfo>
EditorAssetOperator::FindProjectSceneReferences(EditorContext& context,
                                                const std::string& path) const
{
    const auto start = EditorOperatorClock::now();
    std::vector<SceneReferenceInfo> result;
    const std::filesystem::path assetPath = ResolveEditorAssetPath(context, path);
    const std::filesystem::path contentRoot = context.GetContentRoot();
    if (assetPath.empty() || contentRoot.empty() ||
        ContentOrSourceRoot(context, assetPath).empty() ||
        !std::filesystem::exists(contentRoot)) {
        RecordAssetOperatorEvent(context, "Find Project Scene References", path,
                                 ElapsedOperatorMs(start), false);
        return result;
    }

    const std::filesystem::path normalizedAsset = NormalizeAbsolute(assetPath);
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator it(
             contentRoot, std::filesystem::directory_options::skip_permission_denied,
             error);
         !error && it != std::filesystem::recursive_directory_iterator();
         it.increment(error)) {
        if (error || !it->is_regular_file(error)) continue;
        const std::filesystem::path scenePath = it->path();
        if (EditorAssetRegistry::Classify(scenePath) != EditorAssetType::Scene) {
            continue;
        }

        std::ifstream input(scenePath, std::ios::binary);
        if (!input) continue;
        nlohmann::json sceneJson;
        try {
            input >> sceneJson;
        } catch (...) {
            continue;
        }

        FindAssetReferencesInSceneJson(
            context, normalizedAsset, sceneJson,
            ProjectRelativeReferencePath(context, scenePath), result);
    }

    std::stable_sort(result.begin(), result.end(),
        [](const SceneReferenceInfo& left, const SceneReferenceInfo& right) {
            if (left.scenePath != right.scenePath) return left.scenePath < right.scenePath;
            if (left.actorName != right.actorName) return left.actorName < right.actorName;
            if (left.actorID != right.actorID) return left.actorID < right.actorID;
            if (left.componentType != right.componentType) {
                return left.componentType < right.componentType;
            }
            return left.jsonPath < right.jsonPath;
        });

    RecordAssetOperatorEvent(context, "Find Project Scene References",
                             normalizedAsset.string(), ElapsedOperatorMs(start), true,
                             "count=" + std::to_string(result.size()));
    return result;
}

size_t EditorAssetOperator::RetargetSceneReferences(
    EditorContext& context, const std::string& oldPath,
    const std::string& newPath) const
{
    const auto start = EditorOperatorClock::now();
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    const std::filesystem::path oldAssetPath = ResolveEditorAssetPath(context, oldPath);
    const std::filesystem::path newAssetPath = ResolveEditorAssetPath(context, newPath);
    if (!scene || !stack || !context.CanEditScene() ||
        oldAssetPath.empty() || newAssetPath.empty() ||
        ContentOrSourceRoot(context, oldAssetPath).empty() ||
        ContentOrSourceRoot(context, newAssetPath).empty()) {
        RecordAssetOperatorEvent(context, "Retarget Scene References", oldPath,
                                 ElapsedOperatorMs(start), false,
                                 "target=" + newPath);
        return 0;
    }

    const std::string before = SceneSerializer::SaveToString(*scene);
    nlohmann::json sceneJson;
    try {
        sceneJson = nlohmann::json::parse(before);
    } catch (...) {
        RecordAssetOperatorEvent(context, "Retarget Scene References", oldPath,
                                 ElapsedOperatorMs(start), false,
                                 "target=" + newPath);
        return 0;
    }

    const size_t changedCount = RetargetAssetReferencesInJson(
        context, NormalizeAbsolute(oldAssetPath), NormalizeAbsolute(newAssetPath),
        sceneJson);
    if (changedCount == 0) {
        RecordAssetOperatorEvent(context, "Retarget Scene References", oldPath,
                                 ElapsedOperatorMs(start), false,
                                 "target=" + newPath + ";unchanged=true");
        return 0;
    }

    const uint64_t selection = context.GetSelection().GetActorID();
    const std::string candidate = sceneJson.dump(2);
    if (!SceneSerializer::LoadFromString(*scene, candidate)) {
        SceneSerializer::LoadFromString(*scene, before);
        RecordAssetOperatorEvent(context, "Retarget Scene References", oldPath,
                                 ElapsedOperatorMs(start), false,
                                 "target=" + newPath);
        return 0;
    }
    const std::string after = SceneSerializer::SaveToString(*scene);
    SceneSerializer::LoadFromString(*scene, before);
    if (before == after) {
        RecordAssetOperatorEvent(context, "Retarget Scene References", oldPath,
                                 ElapsedOperatorMs(start), false,
                                 "target=" + newPath + ";unchanged=true");
        return 0;
    }

    const bool ok = stack->ExecuteCommand(
        EditorUndoUtil::MakeSceneSnapshotCommand(
            "Retarget Asset References", before, after, selection, selection),
        context);
    RecordAssetOperatorEvent(context, "Retarget Scene References", oldPath,
                             ElapsedOperatorMs(start), ok,
                             "target=" + newPath +
                                 ";count=" + std::to_string(changedCount));
    return ok ? changedCount : 0;
}

size_t EditorAssetOperator::RetargetProjectSceneReferences(
    EditorContext& context, const std::string& oldPath,
    const std::string& newPath) const
{
    const auto start = EditorOperatorClock::now();
    EditorCommandStack* stack = context.GetCommandStack();
    const std::filesystem::path oldAssetPath = ResolveEditorAssetPath(context, oldPath);
    const std::filesystem::path newAssetPath = ResolveEditorAssetPath(context, newPath);
    const std::filesystem::path contentRoot = context.GetContentRoot();
    if (!stack || oldAssetPath.empty() || newAssetPath.empty() ||
        contentRoot.empty() || !std::filesystem::exists(contentRoot) ||
        ContentOrSourceRoot(context, oldAssetPath).empty() ||
        ContentOrSourceRoot(context, newAssetPath).empty()) {
        RecordAssetOperatorEvent(context, "Retarget Project Scene References",
                                 oldPath, ElapsedOperatorMs(start), false,
                                 "target=" + newPath);
        return 0;
    }

    std::vector<ModifyAssetsCommand::Entry> entries;
    size_t changedCount = 0;
    std::filesystem::path currentSceneFile;
    if (const SceneRenderLayer* layer = context.GetSceneLayer()) {
        if (layer->HasFilePath()) {
            currentSceneFile = NormalizeAbsolute(layer->GetSceneFilePath());
        }
    }
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator it(
             contentRoot, std::filesystem::directory_options::skip_permission_denied,
             error);
         !error && it != std::filesystem::recursive_directory_iterator();
         it.increment(error)) {
        if (error || !it->is_regular_file(error)) continue;
        const std::filesystem::path scenePath = it->path();
        if (EditorAssetRegistry::Classify(scenePath) != EditorAssetType::Scene) {
            continue;
        }
        if (!currentSceneFile.empty() &&
            NormalizeAbsolute(scenePath) == currentSceneFile) {
            continue;
        }

        std::ifstream input(scenePath, std::ios::binary);
        if (!input) continue;
        const std::string before{
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
        nlohmann::json sceneJson;
        try {
            sceneJson = nlohmann::json::parse(before);
        } catch (...) {
            continue;
        }

        const size_t fileChanges = RetargetAssetReferencesInJson(
            context, NormalizeAbsolute(oldAssetPath),
            NormalizeAbsolute(newAssetPath), sceneJson);
        if (fileChanges == 0) continue;

        const std::string after = sceneJson.dump(2);
        if (before == after) continue;
        changedCount += fileChanges;
        entries.push_back({scenePath.string(), before, after});
    }

    if (entries.empty()) {
        RecordAssetOperatorEvent(context, "Retarget Project Scene References",
                                 oldPath, ElapsedOperatorMs(start), false,
                                 "target=" + newPath + ";unchanged=true");
        return 0;
    }

    const bool ok = stack->ExecuteCommand(
        std::make_unique<ModifyAssetsCommand>(std::move(entries)), context);
    RecordAssetOperatorEvent(context, "Retarget Project Scene References",
                             oldPath, ElapsedOperatorMs(start), ok,
                             "target=" + newPath +
                                 ";count=" + std::to_string(changedCount));
    return ok ? changedCount : 0;
}

bool EditorAssetOperator::Reimport(EditorContext& context, const std::string& uuid) const
{
    const auto start = EditorOperatorClock::now();
    if (uuid.empty()) {
        RecordAssetOperatorEvent(context, "Reimport Asset", uuid,
                                 ElapsedOperatorMs(start), false);
        return false;
    }
    EditorImportService* importer = context.GetService<EditorImportService>();
    if (!importer) {
        RecordAssetOperatorEvent(context, "Reimport Asset", uuid,
                                 ElapsedOperatorMs(start), false);
        return false;
    }
    const bool result = importer->Reimport(uuid);
    Refresh(context);
    RecordAssetOperatorEvent(context, "Reimport Asset", uuid,
                             ElapsedOperatorMs(start), result);
    return result;
}

bool EditorAssetOperator::ReimportAll(EditorContext& context,
                                      std::vector<std::string>* failures) const
{
    const auto start = EditorOperatorClock::now();
    EditorImportService* importer = context.GetService<EditorImportService>();
    if (!importer) {
        RecordAssetOperatorEvent(context, "Reimport All", "*",
                                 ElapsedOperatorMs(start), false);
        return false;
    }

    std::vector<std::string> localFailures;
    std::vector<std::string>* outputFailures = failures ? failures : &localFailures;
    importer->ReimportAll(outputFailures);
    Refresh(context);
    const bool ok = outputFailures->empty();
    RecordAssetOperatorEvent(context, "Reimport All", "*",
                             ElapsedOperatorMs(start), ok,
                             "failures=" + std::to_string(outputFailures->size()));
    return ok;
}

bool EditorAssetOperator::ReimportWithSettings(EditorContext& context,
                                               const std::string& uuid,
                                               const std::string& settingsJson) const
{
    const auto start = EditorOperatorClock::now();
    if (uuid.empty() || settingsJson.empty()) {
        RecordAssetOperatorEvent(context, "Update Import Settings", uuid,
                                 ElapsedOperatorMs(start), false);
        return false;
    }

    std::string beforeSettings;
    if (!ReadImportSettings(context, uuid, beforeSettings)) {
        RecordAssetOperatorEvent(context, "Update Import Settings", uuid,
                                 ElapsedOperatorMs(start), false);
        return false;
    }
    if (beforeSettings == settingsJson) {
        RecordAssetOperatorEvent(context, "Update Import Settings", uuid,
                                 ElapsedOperatorMs(start), false,
                                 "unchanged=true");
        return false;
    }

    auto applySettings = [uuid](const std::string& settings) {
        return [uuid, settings](EditorContext& commandContext) {
            return ApplyImportSettings(commandContext, uuid, settings);
        };
    };

    if (EditorCommandStack* stack = context.GetCommandStack()) {
        const bool ok = stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>(
            "Update Import Settings",
            applySettings(settingsJson),
            applySettings(beforeSettings)), context);
        RecordAssetOperatorEvent(context, "Update Import Settings", uuid,
                                 ElapsedOperatorMs(start), ok);
        return ok;
    }

    const bool ok = ApplyImportSettings(context, uuid, settingsJson);
    RecordAssetOperatorEvent(context, "Update Import Settings", uuid,
                             ElapsedOperatorMs(start), ok);
    return ok;
}

std::vector<EditorPrefabOperator::OverrideInfo> EditorPrefabOperator::GetOverrides(
    EditorContext& context, uint64_t actorID) const
{
    std::vector<OverrideInfo> result;
    Actor* actor = ResolveEditablePrefabActor(context, actorID);
    if (!actor) return result;
    nlohmann::json overrides;
    std::string error;
    if (!PrefabSystem::BuildOverrides(*actor, overrides, &error)) {
        if (!error.empty()) Logger::Warn("[Editor] Prefab override list failed: ", error);
        return result;
    }
    if (!overrides.is_array()) return result;
    result.reserve(overrides.size());
    for (size_t index = 0; index < overrides.size(); ++index) {
        const nlohmann::json& item = overrides[index];
        OverrideInfo info;
        info.index = index;
        info.kind = item.value("kind", std::string{});
        info.localId = item.value("localId", std::string{});
        info.componentType = item.value("componentType", std::string{});
        info.path = item.value("path", std::string{});
        info.category = OverrideCategory(item);
        info.target = OverrideTargetLabel(item);
        info.property = OverridePropertyLabel(item);
        info.label = OverrideLabel(item);
        info.valuePreview = OverrideValuePreview(item);
        const bool supported = IsSupportedPrefabOverride(item);
        info.canApply = supported;
        info.canRevert = supported;
        if (!supported) {
            info.diagnostic = info.kind.empty()
                ? "Unsupported prefab override kind"
                : "Unsupported prefab override kind: " + info.kind;
        }
        result.push_back(std::move(info));
    }
    const nlohmann::json& persistedOverrides = actor->GetPrefabOverrides();
    if (persistedOverrides.is_array()) {
        for (size_t index = 0; index < persistedOverrides.size(); ++index) {
            const nlohmann::json& item = persistedOverrides[index];
            if (IsSupportedPrefabOverride(item)) continue;
            OverrideInfo info;
            info.index = (std::numeric_limits<size_t>::max)() - index;
            info.kind = item.value("kind", std::string{});
            info.localId = item.value("localId", std::string{});
            info.componentType = item.value("componentType", std::string{});
            info.path = item.value("path", std::string{});
            info.category = OverrideCategory(item);
            info.target = OverrideTargetLabel(item);
            info.property = OverridePropertyLabel(item);
            info.label = OverrideLabel(item);
            info.valuePreview = OverrideValuePreview(item);
            info.diagnostic = info.kind.empty()
                ? "Unsupported prefab override kind"
                : "Unsupported prefab override kind: " + info.kind;
            result.push_back(std::move(info));
        }
    }
    std::stable_sort(result.begin(), result.end(),
        [](const OverrideInfo& left, const OverrideInfo& right) {
            const int leftRank = OverrideCategoryRank(left.category);
            const int rightRank = OverrideCategoryRank(right.category);
            if (leftRank != rightRank) return leftRank < rightRank;
            if (left.target != right.target) return left.target < right.target;
            if (left.property != right.property) return left.property < right.property;
            return left.index < right.index;
        });
    return result;
}

bool EditorPrefabOperator::ApplyAll(EditorContext& context, uint64_t actorID) const
{
    const auto start = EditorOperatorClock::now();
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    if (!scene || !stack || !actor || !actor->IsPrefabRoot() || !context.CanEditScene()) {
        RecordPrefabOperatorEvent(context, "Apply Prefab", actorID,
                                  ElapsedOperatorMs(start), false);
        return false;
    }

    const std::filesystem::path prefabPath =
        PrefabSystem::ResolvePrefabPath(actor->GetPrefabAssetPath());
    if (prefabPath.empty() || !std::filesystem::exists(prefabPath)) {
        RecordPrefabOperatorEvent(context, "Apply Prefab", actorID,
                                  ElapsedOperatorMs(start), false,
                                  "prefab=" + prefabPath.string());
        return false;
    }

    const std::string prefabBefore = ReadFileContent(prefabPath);
    const std::string sceneBefore = SceneSerializer::SaveToString(*scene);
    std::string error;
    if (!PrefabSystem::ApplyAll(*actor, &error)) {
        if (!error.empty()) Logger::Warn("[Editor] Apply Prefab failed: ", error);
        RecordPrefabOperatorEvent(context, "Apply Prefab", actorID,
                                  ElapsedOperatorMs(start), false,
                                  "prefab=" + prefabPath.string());
        return false;
    }
    const std::string prefabAfter = ReadFileContent(prefabPath);
    const std::string sceneAfter = SceneSerializer::SaveToString(*scene);
    if (!WriteFileContent(prefabPath, prefabBefore) ||
        !SceneSerializer::LoadFromString(*scene, sceneBefore)) {
        RecordPrefabOperatorEvent(context, "Apply Prefab", actorID,
                                  ElapsedOperatorMs(start), false,
                                  "prefab=" + prefabPath.string());
        return false;
    }

    auto applyState = [prefabPath, actorID](EditorContext& value,
                                           const std::string& prefabContent,
                                           const std::string& sceneJson) {
        Scene* targetScene = value.GetScene();
        if (!targetScene || !WriteFileContent(prefabPath, prefabContent) ||
            !SceneSerializer::LoadFromString(*targetScene, sceneJson)) {
            return false;
        }
        if (targetScene->FindByID(actorID)) value.GetSelection().SelectActorID(actorID);
        else value.GetSelection().Clear();
        if (EditorAssetRegistry* registry = value.GetAssetRegistry()) registry->Refresh();
        return true;
    };

    const bool ok = stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>(
        "Apply Prefab",
        [applyState, prefabAfter, sceneAfter](EditorContext& value) {
            return applyState(value, prefabAfter, sceneAfter);
        },
        [applyState, prefabBefore, sceneBefore](EditorContext& value) {
            return applyState(value, prefabBefore, sceneBefore);
        }), context);
    RecordPrefabOperatorEvent(context, "Apply Prefab", actorID,
                              ElapsedOperatorMs(start), ok,
                              "prefab=" + prefabPath.string());
    return ok;
}

bool EditorPrefabOperator::RevertAll(EditorContext& context, uint64_t actorID) const
{
    const auto start = EditorOperatorClock::now();
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    if (!actor) {
        RecordPrefabOperatorEvent(context, "Revert Prefab", actorID,
                                  ElapsedOperatorMs(start), false);
        return false;
    }
    const bool ok = CommitPrefabSceneSnapshot(context, *actor, "Revert Prefab",
        [](Actor& value, std::string* error) {
            return PrefabSystem::RevertAll(value, error);
        });
    RecordPrefabOperatorEvent(context, "Revert Prefab", actorID,
                              ElapsedOperatorMs(start), ok);
    return ok;
}

bool EditorPrefabOperator::Unpack(EditorContext& context, uint64_t actorID) const
{
    const auto start = EditorOperatorClock::now();
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    if (!actor) {
        RecordPrefabOperatorEvent(context, "Unpack Prefab", actorID,
                                  ElapsedOperatorMs(start), false);
        return false;
    }
    const bool ok = CommitPrefabSceneSnapshot(context, *actor, "Unpack Prefab",
        [](Actor& value, std::string* error) {
            return PrefabSystem::Unpack(value, error);
        });
    RecordPrefabOperatorEvent(context, "Unpack Prefab", actorID,
                              ElapsedOperatorMs(start), ok);
    return ok;
}

bool EditorPrefabOperator::CreatePrefabFromActor(EditorContext& context,
                                                 uint64_t actorID) const
{
    const auto start = EditorOperatorClock::now();
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    if (!scene || !stack || !actor || !context.CanEditScene()) {
        RecordPrefabOperatorEvent(context, "Create Prefab", actorID,
                                  ElapsedOperatorMs(start), false);
        return false;
    }

    const std::string sceneBefore = SceneSerializer::SaveToString(*scene);
    const ActorHandle parentHandle =
        actor->GetParent() ? actor->GetParent()->GetHandle() : ActorHandle{};
    const Transform transform = actor->GetTransform();
    const std::filesystem::path directory = context.GetContentRoot() / "Prefabs";
    std::error_code fsError;
    std::filesystem::create_directories(directory, fsError);
    const std::filesystem::path prefabPath =
        EditorImportService::MakeUniqueContentPath(
            directory, actor->GetName(), ".prefab.json");
    std::string error;
    if (!PrefabSystem::SaveSubtree(*actor, prefabPath, &error)) {
        if (!error.empty()) Logger::Warn("[Editor] Prefab creation failed: ", error);
        RecordPrefabOperatorEvent(context, "Create Prefab", actorID,
                                  ElapsedOperatorMs(start), false,
                                  "prefab=" + prefabPath.string());
        return false;
    }
    const std::filesystem::path metaPath = AssetMeta::MetaPathFor(prefabPath.string());
    const std::string prefabContent = ReadFileContent(prefabPath);
    const std::string metaContent =
        std::filesystem::exists(metaPath) ? ReadFileContent(metaPath) : std::string{};
    const bool hadMeta = std::filesystem::exists(metaPath);

    scene->QueueDestroyActor(actor->GetHandle());
    if (!scene->FlushCommands()) {
        SceneSerializer::LoadFromString(*scene, sceneBefore);
        RemoveAssetFileAndMeta(prefabPath);
        RecordPrefabOperatorEvent(context, "Create Prefab", actorID,
                                  ElapsedOperatorMs(start), false,
                                  "prefab=" + prefabPath.string());
        return false;
    }
    PrefabInstantiateOptions options;
    options.parent = parentHandle;
    options.rootTransform = transform;
    options.persistentRootID = actorID;
    Actor* instance = PrefabSystem::Instantiate(*scene, prefabPath, options, &error);
    if (!instance) {
        if (!error.empty()) Logger::Warn("[Editor] Prefab instance creation failed: ", error);
        SceneSerializer::LoadFromString(*scene, sceneBefore);
        RemoveAssetFileAndMeta(prefabPath);
        RecordPrefabOperatorEvent(context, "Create Prefab", actorID,
                                  ElapsedOperatorMs(start), false,
                                  "prefab=" + prefabPath.string());
        return false;
    }
    const std::string sceneAfter = SceneSerializer::SaveToString(*scene);
    SceneSerializer::LoadFromString(*scene, sceneBefore);
    RemoveAssetFileAndMeta(prefabPath);

    auto applyState = [prefabPath, metaPath, prefabContent, metaContent, hadMeta, actorID](
                          EditorContext& value, const std::string& sceneJson,
                          bool writePrefab) {
        Scene* targetScene = value.GetScene();
        if (!targetScene) return false;
        if (writePrefab) {
            if (!WriteFileContent(prefabPath, prefabContent)) return false;
            if (hadMeta && !WriteFileContent(metaPath, metaContent)) return false;
        }
        if (!SceneSerializer::LoadFromString(*targetScene, sceneJson)) return false;
        if (!writePrefab) {
            RemoveAssetFileAndMeta(prefabPath);
        }
        if (targetScene->FindByID(actorID)) value.GetSelection().SelectActorID(actorID);
        else value.GetSelection().Clear();
        if (EditorAssetRegistry* registry = value.GetAssetRegistry()) registry->Refresh();
        return true;
    };

    const bool ok = stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>(
        "Create Prefab",
        [applyState, sceneAfter](EditorContext& value) {
            return applyState(value, sceneAfter, true);
        },
        [applyState, sceneBefore](EditorContext& value) {
            return applyState(value, sceneBefore, false);
        }), context);
    RecordPrefabOperatorEvent(context, "Create Prefab", actorID,
                              ElapsedOperatorMs(start), ok,
                              "prefab=" + prefabPath.string());
    return ok;
}

uint64_t EditorPrefabOperator::InstantiatePrefab(
    EditorContext& context, const std::string& path, uint64_t parentActorID,
    const std::optional<Transform>& rootTransform, const char* commandName) const
{
    const auto start = EditorOperatorClock::now();
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack || !context.CanEditScene() || path.empty()) {
        RecordPrefabOperatorEvent(context, "Instantiate Prefab", 0,
                                  ElapsedOperatorMs(start), false,
                                  "prefab=" + path);
        return 0;
    }
    Actor* parent = parentActorID ? scene->FindByID(parentActorID) : nullptr;
    if (parentActorID && !parent) {
        RecordPrefabOperatorEvent(context, "Instantiate Prefab", 0,
                                  ElapsedOperatorMs(start), false,
                                  "prefab=" + path);
        return 0;
    }

    const std::string before = SceneSerializer::SaveToString(*scene);
    const uint64_t oldSelection = context.GetSelection().GetActorID();
    PrefabInstantiateOptions options;
    options.parent = parent ? parent->GetHandle() : ActorHandle{};
    options.rootTransform = rootTransform;
    std::string error;
    Actor* actor = PrefabSystem::Instantiate(*scene, path, options, &error);
    if (!actor) {
        if (!error.empty()) Logger::Warn("[Editor] Instantiate prefab failed: ", error);
        SceneSerializer::LoadFromString(*scene, before);
        RecordPrefabOperatorEvent(context, "Instantiate Prefab", 0,
                                  ElapsedOperatorMs(start), false,
                                  "prefab=" + path);
        return 0;
    }
    const uint64_t newID = actor->GetID();
    const std::string after = SceneSerializer::SaveToString(*scene);
    SceneSerializer::LoadFromString(*scene, before);
    if (!stack->ExecuteCommand(EditorUndoUtil::MakeSceneSnapshotCommand(
            commandName ? commandName : "Instantiate Prefab",
            before, after, oldSelection, newID), context)) {
        RecordPrefabOperatorEvent(context, "Instantiate Prefab", newID,
                                  ElapsedOperatorMs(start), false,
                                  "prefab=" + path);
        return 0;
    }
    RecordPrefabOperatorEvent(context, "Instantiate Prefab", newID,
                              ElapsedOperatorMs(start), true,
                              "prefab=" + path);
    return newID;
}

size_t EditorPrefabOperator::SelectInstances(EditorContext& context,
                                             const std::string& path) const
{
    Scene* scene = context.GetScene();
    if (!scene || path.empty()) return 0;

    const std::filesystem::path targetPath =
        PrefabSystem::ResolvePrefabPath(path).lexically_normal();
    std::vector<uint64_t> rootIDs;
    scene->ForEach([&](Actor& actor) {
        if (!actor.IsPrefabRoot() || actor.GetPrefabAssetPath().empty()) return;
        const std::filesystem::path actorPath =
            PrefabSystem::ResolvePrefabPath(actor.GetPrefabAssetPath())
                .lexically_normal();
        if (actorPath == targetPath) rootIDs.push_back(actor.GetID());
    });
    if (rootIDs.empty()) return 0;

    context.GetSelection().SelectActorID(rootIDs.front());
    for (size_t index = 1; index < rootIDs.size(); ++index) {
        context.GetSelection().AddToMultiSelect(rootIDs[index]);
    }
    context.RequestPanelFocus("sceneHierarchy");
    return rootIDs.size();
}

bool EditorPrefabOperator::ApplyOverride(EditorContext& context, uint64_t actorID,
                                         size_t overrideIndex) const
{
    const auto start = EditorOperatorClock::now();
    Actor* actor = ResolveEditablePrefabActor(context, actorID);
    EditorCommandStack* stack = context.GetCommandStack();
    if (!actor || !stack || !context.CanEditScene()) {
        RecordPrefabOperatorEvent(context, "Apply Prefab Override", actorID,
                                  ElapsedOperatorMs(start), false,
                                  "overrideIndex=" + std::to_string(overrideIndex));
        return false;
    }
    const std::filesystem::path prefabPath =
        PrefabSystem::ResolvePrefabPath(actor->GetPrefabAssetPath());
    if (prefabPath.empty() || !std::filesystem::exists(prefabPath)) {
        RecordPrefabOperatorEvent(context, "Apply Prefab Override", actorID,
                                  ElapsedOperatorMs(start), false,
                                  "overrideIndex=" + std::to_string(overrideIndex));
        return false;
    }
    Scene* scene = actor->GetScene();
    if (!scene) {
        RecordPrefabOperatorEvent(context, "Apply Prefab Override", actorID,
                                  ElapsedOperatorMs(start), false,
                                  "overrideIndex=" + std::to_string(overrideIndex));
        return false;
    }

    const std::string prefabBefore = ReadFileContent(prefabPath);
    const std::string sceneBefore = SceneSerializer::SaveToString(*scene);
    std::string error;
    if (!ApplySinglePrefabOverrideNow(context, actorID, overrideIndex, &error)) {
        if (!error.empty()) Logger::Warn("[Editor] Apply Prefab Override failed: ", error);
        RestorePrefabEditorState(context, prefabPath, prefabBefore, sceneBefore, actorID);
        RecordPrefabOperatorEvent(context, "Apply Prefab Override", actorID,
                                  ElapsedOperatorMs(start), false,
                                  "overrideIndex=" + std::to_string(overrideIndex));
        return false;
    }
    const std::string prefabAfter = ReadFileContent(prefabPath);
    const std::string sceneAfter = SceneSerializer::SaveToString(*scene);
    if (!RestorePrefabEditorState(context, prefabPath, prefabBefore, sceneBefore, actorID)) {
        RecordPrefabOperatorEvent(context, "Apply Prefab Override", actorID,
                                  ElapsedOperatorMs(start), false,
                                  "overrideIndex=" + std::to_string(overrideIndex));
        return false;
    }

    const bool ok = stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>(
        "Apply Prefab Override",
        [prefabPath, prefabAfter, sceneAfter, actorID](EditorContext& value) {
            return RestorePrefabEditorState(value, prefabPath, prefabAfter, sceneAfter, actorID);
        },
        [prefabPath, prefabBefore, sceneBefore, actorID](EditorContext& value) {
            return RestorePrefabEditorState(value, prefabPath, prefabBefore, sceneBefore, actorID);
        }), context);
    RecordPrefabOperatorEvent(context, "Apply Prefab Override", actorID,
                              ElapsedOperatorMs(start), ok,
                              "overrideIndex=" + std::to_string(overrideIndex));
    return ok;
}

bool EditorPrefabOperator::RevertOverride(EditorContext& context, uint64_t actorID,
                                          size_t overrideIndex) const
{
    const auto start = EditorOperatorClock::now();
    Actor* actor = ResolveEditablePrefabActor(context, actorID);
    if (!actor) {
        RecordPrefabOperatorEvent(context, "Revert Prefab Override", actorID,
                                  ElapsedOperatorMs(start), false,
                                  "overrideIndex=" + std::to_string(overrideIndex));
        return false;
    }
    const bool ok = CommitPrefabSceneSnapshot(context, *actor, "Revert Prefab Override",
        [actorID, overrideIndex, &context](Actor&, std::string* error) {
            return RevertSinglePrefabOverrideNow(context, actorID, overrideIndex, error);
        });
    RecordPrefabOperatorEvent(context, "Revert Prefab Override", actorID,
                              ElapsedOperatorMs(start), ok,
                              "overrideIndex=" + std::to_string(overrideIndex));
    return ok;
}

bool EditorPrefabOperator::ApplyOverride(EditorContext& context, uint64_t actorID,
                                         const std::string& overridePath) const
{
    const auto start = EditorOperatorClock::now();
    const auto overrides = GetOverrides(context, actorID);
    for (const auto& item : overrides) {
        if (item.path == overridePath || item.label == overridePath) {
            return ApplyOverride(context, actorID, item.index);
        }
    }
    RecordPrefabOperatorEvent(context, "Apply Prefab Override", actorID,
                              ElapsedOperatorMs(start), false,
                              "overridePath=" + overridePath);
    return false;
}

bool EditorPrefabOperator::RevertOverride(EditorContext& context, uint64_t actorID,
                                          const std::string& overridePath) const
{
    const auto start = EditorOperatorClock::now();
    const auto overrides = GetOverrides(context, actorID);
    for (const auto& item : overrides) {
        if (item.path == overridePath || item.label == overridePath) {
            return RevertOverride(context, actorID, item.index);
        }
    }
    RecordPrefabOperatorEvent(context, "Revert Prefab Override", actorID,
                              ElapsedOperatorMs(start), false,
                              "overridePath=" + overridePath);
    return false;
}

bool EditorViewportOperator::SetSceneViewportRect(EditorContext& context,
                                                  const EditorRect& rect,
                                                  bool hovered) const
{
    SceneViewport* viewport = context.GetSceneViewport();
    if (!viewport || rect.width <= 1.0f || rect.height <= 1.0f) return false;
    viewport->SetViewportRect(static_cast<int>(rect.x), static_cast<int>(rect.y),
                              static_cast<int>(rect.width), static_cast<int>(rect.height));
    viewport->SetInputEnabled(hovered);
    if (SceneRenderLayer* layer = context.GetSceneLayer()) layer->SetSceneViewportActive(true);
    return true;
}

bool EditorViewportOperator::SetGameViewportRect(EditorContext& context,
                                                 const EditorRect& rect) const
{
    GameViewport* viewport = context.GetGameViewport();
    if (!viewport || rect.width <= 1.0f || rect.height <= 1.0f) return false;
    viewport->SetViewportRect(static_cast<int>(rect.x), static_cast<int>(rect.y),
                              static_cast<int>(rect.width), static_cast<int>(rect.height));
    if (SceneRenderLayer* layer = context.GetSceneLayer()) layer->SetGameViewportActive(true);
    return true;
}

bool EditorViewportOperator::FrameSelected(EditorContext& context) const
{
    Vec3 target{};
    float radius = 1.0f;
    if (!ResolveViewportFrameTarget(context, target, radius)) return false;
    return FrameTarget(context, target, radius);
}

bool EditorViewportOperator::FrameTarget(EditorContext& context, const Vec3& target,
                                         float radius) const
{
    SceneViewport* viewport = context.GetSceneViewport();
    if (!viewport) return false;
    viewport->FrameTarget(target, radius);
    return true;
}

bool EditorViewportOperator::FrameDirection(EditorContext& context,
                                            SceneViewDirection direction) const
{
    SceneViewport* viewport = context.GetSceneViewport();
    if (!viewport) return false;
    Vec3 target{};
    float radius = 1.0f;
    if (!ResolveViewportFrameTarget(context, target, radius)) {
        target = Vec3::Zero();
        radius = 1.0f;
    }
    viewport->FrameDirection(direction, target, (std::max)(10.0f, radius * 4.0f));
    return true;
}

bool EditorViewportOperator::OrbitAroundSelection(EditorContext& context,
                                                  float yawDegrees,
                                                  float pitchDegrees) const
{
    SceneViewport* viewport = context.GetSceneViewport();
    if (!viewport) return false;
    Vec3 target{};
    float radius = 1.0f;
    if (!ResolveViewportFrameTarget(context, target, radius)) target = Vec3::Zero();
    viewport->OrbitAroundFocus(target, yawDegrees, pitchDegrees);
    return true;
}

bool EditorViewportOperator::ToggleSceneProjection(EditorContext& context) const
{
    SceneViewport* viewport = context.GetSceneViewport();
    if (!viewport) return false;
    viewport->ToggleProjectionMode();
    return true;
}

bool EditorViewportOperator::DropModel(EditorContext& context, const std::string& path,
                                       float screenX, float screenY) const
{
    if (!context.CanEditScene() || path.empty()) return false;

    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack) return false;

    ModelHandle model = AssetManager::Get().Load<ModelAsset>(path);
    if (!model || !model->GetMesh()) {
        Logger::Warn("[Editor] Failed to load model: ", path);
        return false;
    }

    const std::string before = SceneSerializer::SaveToString(*scene);
    const uint64_t previousSelection = context.GetSelection().GetActorID();

    std::string actorName = std::filesystem::path(path).stem().string();
    Actor* actor = scene->CreateActor(actorName.empty() ? "Mesh" : actorName);
    if (!actor) return false;

    auto* renderer = actor->AddComponent<MeshRendererComponent>();
    if (!renderer) return false;
    renderer->SetMesh(model->GetMesh());
    renderer->SetMaterials(model->GetMaterials());
    if (renderer->GetMaterials().empty()) {
        renderer->SetMaterial(AssetManager::Get().GetDefaultMaterial());
    }

    Math::Ray ray{};
    float distance = 0.0f;
    SceneViewport* sceneViewport = context.GetSceneViewport();
    if (sceneViewport &&
        sceneViewport->BuildRayFromScreen(screenX, screenY, ray) &&
        std::fabs(ray.direction.y) > 1e-5f &&
        (distance = -ray.origin.y / ray.direction.y) > 0.0f) {
        actor->GetTransform().position = ray.At(distance);
    } else if (sceneViewport) {
        Camera& camera = sceneViewport->GetCamera();
        actor->GetTransform().position = camera.GetPosition() + camera.GetForward() * 8.0f;
    }

    const uint64_t actorID = actor->GetID();
    const std::string after = SceneSerializer::SaveToString(*scene);
    SceneSerializer::LoadFromString(*scene, before);
    return stack->ExecuteCommand(
        EditorUndoUtil::MakeSceneSnapshotCommand("Drop Model", before, after,
                                                 previousSelection, actorID),
        context);
}
