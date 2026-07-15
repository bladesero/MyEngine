#include "Editor/EditorOperators.h"

#include "Assets/Asset.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetManager.h"
#include "Assets/AssetMeta.h"
#include "Assets/ModelAsset.h"
#include "Assets/ShaderAsset.h"
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

double ElapsedOperatorMs(EditorOperatorClock::time_point start) {
    return std::chrono::duration<double, std::milli>(EditorOperatorClock::now() - start).count();
}

void RecordAssetOperatorEvent(EditorContext& context, const char* operation, const std::string& path, double durationMs,
                              bool success, std::string details = {}) {
    std::string eventDetails = "path=" + path;
    eventDetails += success ? ";success=true" : ";success=false";
    if (!details.empty()) {
        eventDetails += ";";
        eventDetails += std::move(details);
    }
    if (EditorProfiler* profiler = context.GetProfiler()) {
        profiler->RecordEvent("EditorAsset", operation, durationMs, std::move(eventDetails));
    }
    if (!success) {
        Logger::Warn("[EditorAsset] ", operation, " failed: ", path);
    }
}

void RecordPrefabOperatorEvent(EditorContext& context, const char* operation, uint64_t actorID, double durationMs,
                               bool success, std::string details = {}) {
    std::string eventDetails = "actorID=" + std::to_string(actorID);
    eventDetails += success ? ";success=true" : ";success=false";
    if (!details.empty()) {
        eventDetails += ";";
        eventDetails += std::move(details);
    }
    if (EditorProfiler* profiler = context.GetProfiler()) {
        profiler->RecordEvent("EditorPrefab", operation, durationMs, std::move(eventDetails));
    }
    if (!success) {
        Logger::Warn("[EditorPrefab] ", operation, " failed: actorID=", actorID);
    }
}

EditorSelectionMode ToSelectionMode(EditorSelectionIntentMode mode) {
    switch (mode) {
    case EditorSelectionIntentMode::Add:
        return EditorSelectionMode::Add;
    case EditorSelectionIntentMode::Toggle:
        return EditorSelectionMode::Toggle;
    default:
        return EditorSelectionMode::Replace;
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

std::optional<EditorUIPreset> EditorUIPresetFromID(const std::string& id) {
    if (id == "canvas")
        return EditorUIPreset::Canvas;
    if (id == "text")
        return EditorUIPreset::Text;
    if (id == "image")
        return EditorUIPreset::Image;
    if (id == "button")
        return EditorUIPreset::Button;
    if (id == "slider")
        return EditorUIPreset::Slider;
    if (id == "progressBar")
        return EditorUIPreset::ProgressBar;
    if (id == "scrollView")
        return EditorUIPreset::ScrollView;
    if (id == "verticalLayout")
        return EditorUIPreset::VerticalLayout;
    if (id == "horizontalLayout")
        return EditorUIPreset::HorizontalLayout;
    if (id == "gridLayout")
        return EditorUIPreset::GridLayout;
    return std::nullopt;
}

const char* EditorUIPresetName(EditorUIPreset preset) {
    switch (preset) {
    case EditorUIPreset::Canvas:
        return "UI Canvas";
    case EditorUIPreset::Text:
        return "Text";
    case EditorUIPreset::Image:
        return "Image";
    case EditorUIPreset::Button:
        return "Button";
    case EditorUIPreset::Slider:
        return "Slider";
    case EditorUIPreset::ProgressBar:
        return "Progress Bar";
    case EditorUIPreset::ScrollView:
        return "Scroll View";
    case EditorUIPreset::VerticalLayout:
        return "Vertical Layout";
    case EditorUIPreset::HorizontalLayout:
        return "Horizontal Layout";
    case EditorUIPreset::GridLayout:
        return "Grid Layout";
    }
    return "UI Actor";
}

void ConfigureUIRect(UIRectTransformComponent& rect, EditorUIPreset preset) {
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

void AddUIPresetComponents(Actor& actor, EditorUIPreset preset) {
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
        if (text)
            text->text = "Text";
        break;
    }
    case EditorUIPreset::Image:
        actor.AddComponent<UIImageComponent>();
        break;
    case EditorUIPreset::Button: {
        auto* button = actor.AddComponent<UIButtonComponent>();
        if (button)
            button->text = "Button";
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

std::string ReadFileContent(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

bool WriteFileContent(const std::filesystem::path& path, const std::string& content) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    return static_cast<bool>(out);
}

bool EnsureAssetMeta(const std::filesystem::path& path) {
    if (std::filesystem::exists(AssetMeta::MetaPathFor(path.string())))
        return true;
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

bool CaptureFolderSnapshot(const std::filesystem::path& folder, FolderSnapshot& snapshot) {
    std::error_code error;
    if (!std::filesystem::is_directory(folder, error) || error)
        return false;
    snapshot.root = folder;
    snapshot.directories.clear();
    snapshot.files.clear();
    snapshot.directories.push_back(folder);

    for (std::filesystem::recursive_directory_iterator it(folder, error), end; !error && it != end;
         it.increment(error)) {
        const std::filesystem::path path = it->path();
        if (it->is_directory(error)) {
            snapshot.directories.push_back(path);
        } else if (it->is_regular_file(error)) {
            snapshot.files.push_back({path, ReadFileContent(path)});
        }
    }
    return !error;
}

void RefreshAssetRegistryIfPresent(EditorContext& context) {
    if (EditorAssetRegistry* registry = context.GetAssetRegistry()) {
        registry->Refresh();
    }
}

bool RestoreFolderSnapshot(const FolderSnapshot& snapshot) {
    std::error_code error;
    for (const auto& directory : snapshot.directories) {
        std::filesystem::create_directories(directory, error);
        if (error)
            return false;
    }
    for (const auto& file : snapshot.files) {
        if (!WriteFileContent(file.path, file.content))
            return false;
    }
    return true;
}

bool DeleteFolderSnapshot(const FolderSnapshot& snapshot) {
    std::error_code error;
    for (const auto& file : snapshot.files) {
        if (std::filesystem::exists(file.path, error)) {
            AssetManager::Get().Unload(file.path.string());
            std::filesystem::remove(file.path, error);
            if (error) {
                Logger::Warn("[EditorAsset] Failed to delete file in folder snapshot: ", file.path.string(), " (",
                             error.message(), ")");
                return false;
            }
        }
    }
    for (auto it = snapshot.directories.rbegin(); it != snapshot.directories.rend(); ++it) {
        if (std::filesystem::exists(*it, error)) {
            std::filesystem::remove(*it, error);
            if (error) {
                Logger::Warn("[EditorAsset] Failed to delete directory in folder snapshot: ", it->string(), " (",
                             error.message(), ")");
                return false;
            }
        }
    }
    return true;
}

std::filesystem::path ProjectRootForAssets(const EditorContext& context) {
    if (!context.GetProjectRoot().empty())
        return context.GetProjectRoot();
    if (const EditorAssetRegistry* registry = context.GetAssetRegistry()) {
        const std::filesystem::path& root = registry->GetRoot();
        if (!root.empty())
            return root.parent_path();
    }
    return {};
}

std::filesystem::path AssetDatabasePathFor(const EditorContext& context) {
    const std::filesystem::path projectRoot = ProjectRootForAssets(context);
    return projectRoot.empty() ? std::filesystem::path{} : projectRoot / ".myengine" / "AssetDatabase.json";
}

std::filesystem::path AssetDatabaseAbsolutePathFor(const std::filesystem::path& path,
                                                   const std::filesystem::path& projectRoot) {
    std::error_code error;
    const std::filesystem::path value = path.is_absolute() || projectRoot.empty() ? path : projectRoot / path;
    std::filesystem::path absolute = std::filesystem::absolute(value, error);
    if (error)
        absolute = value;
    return absolute.lexically_normal();
}

bool AssetDatabasePathUnderOrEqual(const std::filesystem::path& path, const std::filesystem::path& root) {
    const std::filesystem::path candidate = path.lexically_normal();
    const std::filesystem::path base = root.lexically_normal();
    if (candidate == base)
        return true;
    const std::filesystem::path relative = candidate.lexically_relative(base);
    if (relative.empty())
        return false;
    const auto first = *relative.begin();
    return first != ".." && first != ".";
}

bool AssetDatabaseRecordUnderRoot(const AssetRecord& record, const std::filesystem::path& root,
                                  const std::filesystem::path& projectRoot) {
    auto matches = [&](const std::string& value) {
        if (value.empty())
            return false;
        return AssetDatabasePathUnderOrEqual(AssetDatabaseAbsolutePathFor(value, projectRoot),
                                             AssetDatabaseAbsolutePathFor(root, projectRoot));
    };
    return matches(record.sourcePath) || matches(record.artifactPath);
}

std::vector<AssetRecord> CaptureAssetDatabaseRecordsUnderRoot(EditorContext& context,
                                                              const std::filesystem::path& root) {
    const std::filesystem::path projectRoot = ProjectRootForAssets(context);
    const std::filesystem::path databasePath = AssetDatabasePathFor(context);
    if (projectRoot.empty() || databasePath.empty() || !std::filesystem::exists(databasePath)) {
        return {};
    }

    AssetDatabase database;
    std::string error;
    if (!database.Open(databasePath, &error)) {
        Logger::Warn("[EditorAsset] Failed to open asset database for folder delete: ", error);
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

bool RemoveAssetDatabaseRecords(EditorContext& context, const std::vector<AssetRecord>& records) {
    if (records.empty())
        return true;
    const std::filesystem::path databasePath = AssetDatabasePathFor(context);
    if (databasePath.empty() || !std::filesystem::exists(databasePath))
        return true;

    AssetDatabase database;
    std::string error;
    if (!database.Open(databasePath, &error)) {
        Logger::Warn("[EditorAsset] Failed to open asset database for folder delete: ", error);
        return false;
    }
    bool changed = false;
    for (const AssetRecord& record : records) {
        changed = database.Remove(record.uuid) || changed;
    }
    if (!changed)
        return true;
    if (!database.Save(&error)) {
        Logger::Warn("[EditorAsset] Failed to save asset database after folder delete: ", error);
        return false;
    }
    return true;
}

bool RestoreAssetDatabaseRecords(EditorContext& context, const std::vector<AssetRecord>& records) {
    if (records.empty())
        return true;
    const std::filesystem::path databasePath = AssetDatabasePathFor(context);
    if (databasePath.empty())
        return true;

    AssetDatabase database;
    std::string error;
    if (!database.Open(databasePath, &error)) {
        Logger::Warn("[EditorAsset] Failed to open asset database for folder delete undo: ", error);
        return false;
    }
    for (AssetRecord record : records) {
        if (!database.Upsert(std::move(record), &error)) {
            Logger::Warn("[EditorAsset] Failed to restore asset database record: ", error);
            return false;
        }
    }
    if (!database.Save(&error)) {
        Logger::Warn("[EditorAsset] Failed to save asset database after folder delete undo: ", error);
        return false;
    }
    return true;
}

bool ReadImportSettings(EditorContext& context, const std::string& uuid, std::string& settingsJson) {
    if (uuid.empty())
        return false;
    const std::filesystem::path databasePath = AssetDatabasePathFor(context);
    if (databasePath.empty())
        return false;
    AssetDatabase database;
    if (!database.Open(databasePath))
        return false;
    const AssetRecord* record = database.FindByUuid(uuid);
    if (!record)
        return false;
    settingsJson = record->settingsJson.empty() ? std::string("{}") : record->settingsJson;
    return true;
}

bool ApplyImportSettings(EditorContext& context, const std::string& uuid, const std::string& settingsJson) {
    EditorImportService* importer = context.GetService<EditorImportService>();
    if (!importer)
        return false;
    const bool ok = importer->ReimportWithSettings(uuid, settingsJson);
    if (ok && context.GetAssetRegistry())
        context.GetAssetRegistry()->Refresh();
    return ok;
}

void RemoveFileIfExists(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::remove(path, error);
}

void RemoveAssetFileAndMeta(const std::filesystem::path& path) {
    RemoveFileIfExists(path);
    RemoveFileIfExists(AssetMeta::MetaPathFor(path.string()));
}

bool AssetRenameTargetExists(const std::filesystem::path& source, const std::filesystem::path& target) {
    std::error_code error;
    if (std::filesystem::exists(target, error))
        return true;
    error.clear();
    if (std::filesystem::is_regular_file(source, error) &&
        std::filesystem::exists(AssetMeta::MetaPathFor(target.string()))) {
        return true;
    }
    return false;
}

uint64_t NextSiblingID(const Actor& actor) {
    Scene* scene = actor.GetScene();
    if (!scene)
        return 0;
    const auto& siblings = actor.GetParent() ? actor.GetParent()->GetChildren() : scene->GetRootActors();
    for (size_t index = 0; index < siblings.size(); ++index) {
        if (siblings[index] == &actor) {
            const size_t next = index + 1;
            return next < siblings.size() && siblings[next] ? siblings[next]->GetID() : 0;
        }
    }
    return 0;
}

uint64_t PreviousSiblingID(const Actor& actor) {
    Scene* scene = actor.GetScene();
    if (!scene)
        return 0;
    const auto& siblings = actor.GetParent() ? actor.GetParent()->GetChildren() : scene->GetRootActors();
    Actor* previous = nullptr;
    for (Actor* sibling : siblings) {
        if (sibling == &actor)
            return previous ? previous->GetID() : 0;
        if (sibling)
            previous = sibling;
    }
    return 0;
}

uint64_t FindNextActorID(const Scene& scene) {
    uint64_t maxID = 0;
    scene.ForEach([&](const Actor& actor) {
        if (actor.GetID() > maxID)
            maxID = actor.GetID();
    });
    return maxID + 1;
}

std::filesystem::path NormalizeAbsolute(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::weakly_canonical(path, error).lexically_normal();
}

bool IsWithinRoot(const std::filesystem::path& path, const std::filesystem::path& root) {
    const std::filesystem::path normalizedPath = NormalizeAbsolute(path);
    const std::filesystem::path normalizedRoot = NormalizeAbsolute(root);
    const std::string value = normalizedPath.generic_string();
    const std::string prefix = normalizedRoot.generic_string();
    return value == prefix || (value.size() > prefix.size() && value.compare(0, prefix.size(), prefix) == 0 &&
                               value[prefix.size()] == '/');
}

std::filesystem::path ContentOrSourceRoot(EditorContext& context, const std::filesystem::path& path) {
    const std::filesystem::path content = context.GetContentRoot();
    const std::filesystem::path source = content.parent_path() / "SourceAssets";
    if (IsWithinRoot(path, content))
        return content;
    if (IsWithinRoot(path, source))
        return source;
    return {};
}

std::filesystem::path ResolveEditorAssetPath(EditorContext& context, const std::string& path) {
    if (path.empty())
        return {};
    std::filesystem::path value(path);
    if (value.is_absolute())
        return value.lexically_normal();
    const std::string generic = value.generic_string();
    if (generic.rfind("Content/", 0) == 0 || generic == "Content" || generic.rfind("SourceAssets/", 0) == 0 ||
        generic == "SourceAssets") {
        return (context.GetContentRoot().parent_path() / value).lexically_normal();
    }
    return (context.GetContentRoot() / value).lexically_normal();
}

bool IsTextLikeAsset(const std::filesystem::path& path, EditorAssetType type) {
    if (type == EditorAssetType::Script || type == EditorAssetType::Shader || type == EditorAssetType::UI) {
        return true;
    }
    if (type != EditorAssetType::Unknown)
        return false;
    const std::string ext = path.extension().generic_string();
    return ext == ".json" || ext == ".txt" || ext == ".md";
}

std::string JsonPointerForChild(const std::string& parent, const std::string& key) {
    std::string escaped;
    escaped.reserve(key.size());
    for (char ch : key) {
        if (ch == '~')
            escaped += "~0";
        else if (ch == '/')
            escaped += "~1";
        else
            escaped.push_back(ch);
    }
    return parent + "/" + escaped;
}

std::string JsonPointerForChild(const std::string& parent, size_t index) {
    return parent + "/" + std::to_string(index);
}

std::string NormalizeReferenceString(const std::string& value) {
    return std::filesystem::path(value).lexically_normal().generic_string();
}

bool AssetReferenceStringMatches(EditorContext& context, const std::filesystem::path& targetAbsolute,
                                 const std::string& value) {
    if (value.empty())
        return false;
    const std::string normalizedValue = NormalizeReferenceString(value);
    const std::string targetGeneric = targetAbsolute.lexically_normal().generic_string();
    if (normalizedValue == targetGeneric)
        return true;

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
    return !resolved.empty() && NormalizeAbsolute(resolved) == NormalizeAbsolute(targetAbsolute);
}

void FindAssetReferencesInJson(EditorContext& context, const std::filesystem::path& targetAbsolute,
                               const nlohmann::json& value, const std::string& path,
                               std::vector<EditorAssetOperator::SceneReferenceInfo>& out, const std::string& scenePath,
                               uint64_t actorID, const std::string& actorName, const std::string& componentType) {
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
            FindAssetReferencesInJson(context, targetAbsolute, it.value(), JsonPointerForChild(path, it.key()), out,
                                      scenePath, actorID, actorName, componentType);
        }
        return;
    }
    if (value.is_array()) {
        for (size_t index = 0; index < value.size(); ++index) {
            FindAssetReferencesInJson(context, targetAbsolute, value[index], JsonPointerForChild(path, index), out,
                                      scenePath, actorID, actorName, componentType);
        }
    }
}

void FindAssetReferencesInSceneJson(EditorContext& context, const std::filesystem::path& targetAbsolute,
                                    const nlohmann::json& sceneJson, const std::string& scenePath,
                                    std::vector<EditorAssetOperator::SceneReferenceInfo>& out) {
    const auto actors = sceneJson.find("actors");
    if (actors == sceneJson.end() || !actors->is_array())
        return;

    for (const auto& actorJson : *actors) {
        if (!actorJson.is_object())
            continue;
        const uint64_t actorID = actorJson.value("id", uint64_t{0});
        const std::string actorName = actorJson.value("name", std::string{"Actor"});

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
        if (components == actorJson.end() || !components->is_array())
            continue;
        for (const auto& componentJson : *components) {
            if (!componentJson.is_object())
                continue;
            const std::string componentType = componentJson.value("type", std::string{"Component"});
            const auto data = componentJson.find("data");
            if (data == componentJson.end())
                continue;
            FindAssetReferencesInJson(context, targetAbsolute, *data, "", out, scenePath, actorID, actorName,
                                      componentType);
        }
    }
}

std::string ProjectRelativeReferencePath(EditorContext& context, const std::filesystem::path& absolutePath) {
    const std::filesystem::path contentRoot = context.GetContentRoot();
    if (contentRoot.empty())
        return absolutePath.lexically_normal().generic_string();
    const std::filesystem::path projectRoot = contentRoot.parent_path();
    std::error_code error;
    const std::filesystem::path relative = std::filesystem::relative(absolutePath, projectRoot, error);
    return error || relative.empty() ? absolutePath.lexically_normal().generic_string()
                                     : relative.lexically_normal().generic_string();
}

std::string ContentRelativeReferencePath(EditorContext& context, const std::filesystem::path& absolutePath) {
    const std::filesystem::path contentRoot = context.GetContentRoot();
    if (contentRoot.empty())
        return absolutePath.lexically_normal().generic_string();
    std::error_code error;
    const std::filesystem::path relative = std::filesystem::relative(absolutePath, contentRoot, error);
    return error || relative.empty() ? ProjectRelativeReferencePath(context, absolutePath)
                                     : relative.lexically_normal().generic_string();
}

std::string ReplacementAssetReferenceString(EditorContext& context, const std::string& oldValue,
                                            const std::filesystem::path& newAbsolute) {
    const std::filesystem::path oldPath(oldValue);
    const std::string oldGeneric = oldPath.lexically_normal().generic_string();
    if (oldPath.is_absolute()) {
        return newAbsolute.lexically_normal().generic_string();
    }
    if (oldGeneric.rfind("Content/", 0) == 0 || oldGeneric == "Content" || oldGeneric.rfind("SourceAssets/", 0) == 0 ||
        oldGeneric == "SourceAssets") {
        return ProjectRelativeReferencePath(context, newAbsolute);
    }
    return ContentRelativeReferencePath(context, newAbsolute);
}

size_t RetargetAssetReferencesInJson(EditorContext& context, const std::filesystem::path& oldAbsolute,
                                     const std::filesystem::path& newAbsolute, nlohmann::json& value) {
    if (value.is_string()) {
        const std::string text = value.get<std::string>();
        if (!AssetReferenceStringMatches(context, oldAbsolute, text))
            return 0;
        value = ReplacementAssetReferenceString(context, text, newAbsolute);
        return 1;
    }
    size_t count = 0;
    if (value.is_object()) {
        for (auto it = value.begin(); it != value.end(); ++it) {
            count += RetargetAssetReferencesInJson(context, oldAbsolute, newAbsolute, it.value());
        }
        return count;
    }
    if (value.is_array()) {
        for (auto& item : value) {
            count += RetargetAssetReferencesInJson(context, oldAbsolute, newAbsolute, item);
        }
    }
    return count;
}

bool OpenExternalFile(const std::filesystem::path& path) {
#if defined(_WIN32)
    const auto result = reinterpret_cast<intptr_t>(
        ShellExecuteW(nullptr, L"open", path.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    return result > 32;
#else
    (void)path;
    return false;
#endif
}

bool RevealExternalPath(const std::filesystem::path& path) {
#if defined(_WIN32)
    const std::wstring parameters = L"/select,\"" + path.wstring() + L"\"";
    const auto result = reinterpret_cast<intptr_t>(
        ShellExecuteW(nullptr, L"open", L"explorer.exe", parameters.c_str(), nullptr, SW_SHOWNORMAL));
    return result > 32;
#else
    (void)path;
    return false;
#endif
}

std::string MakeUniqueSiblingName(Scene& scene, const std::string& base) {
    if (!scene.FindByName(base))
        return base;
    for (int index = 1; index < 10000; ++index) {
        const std::string candidate = base + " (" + std::to_string(index) + ")";
        if (!scene.FindByName(candidate))
            return candidate;
    }
    return base + " (Copy)";
}

Actor* InstantiatePrefabNodesAsCopy(Scene& scene, const std::vector<PrefabNode>& nodes, Actor* parent,
                                    Actor* beforeSibling, std::string* error, uint64_t firstPersistentID = 0) {
    if (nodes.empty()) {
        if (error)
            *error = "actor subtree is empty";
        return nullptr;
    }

    std::unordered_map<std::string, ActorHandle> handles;
    handles.reserve(nodes.size());
    for (size_t index = 0; index < nodes.size(); ++index) {
        const PrefabNode& node = nodes[index];
        ActorCreateDesc desc;
        desc.name = index == 0 ? MakeUniqueSiblingName(scene, node.name + " (Copy)") : node.name;
        desc.transform = node.transform;
        desc.activeSelf = node.activeSelf;
        desc.components = node.components;
        if (firstPersistentID != 0)
            desc.persistentID = firstPersistentID + index;
        handles[node.localId] = scene.QueueCreateActor(desc);
    }

    if (!scene.FlushCommands()) {
        if (error)
            *error = "failed to create actor copy";
        return nullptr;
    }

    for (size_t index = 0; index < nodes.size(); ++index) {
        const PrefabNode& node = nodes[index];
        auto childIt = handles.find(node.localId);
        if (childIt == handles.end())
            continue;
        if (index == 0) {
            scene.QueueMoveActor(childIt->second, parent ? parent->GetHandle() : ActorHandle{},
                                 beforeSibling ? beforeSibling->GetHandle() : ActorHandle{});
        } else {
            auto parentIt = handles.find(node.parentLocalId);
            if (parentIt != handles.end())
                scene.QueueSetParent(childIt->second, parentIt->second);
        }
    }
    if (!scene.FlushCommands()) {
        if (error)
            *error = "failed to restore actor copy hierarchy";
        return nullptr;
    }

    const auto rootIt = handles.find(nodes.front().localId);
    return rootIt == handles.end() ? nullptr : scene.TryGetActor(rootIt->second);
}

bool LoadClipboardNodes(const std::string& actorClipboardJson, std::vector<PrefabNode>& nodes) {
    nodes.clear();
    if (actorClipboardJson.empty())
        return false;
    try {
        const nlohmann::json value = nlohmann::json::parse(actorClipboardJson);
        if (!value.is_array())
            return false;
        for (const auto& item : value) {
            PrefabNode node;
            if (!PrefabNodeFromJson(item, node))
                return false;
            nodes.push_back(std::move(node));
        }
        return !nodes.empty();
    } catch (...) {
        return false;
    }
}

std::vector<PrefabNode> LoadClipboardRootNodes(const nlohmann::json& value) {
    std::vector<PrefabNode> nodes;
    if (!value.is_array())
        return nodes;
    nodes.reserve(value.size());
    for (const auto& item : value) {
        PrefabNode node;
        if (!PrefabNodeFromJson(item, node))
            return {};
        nodes.push_back(std::move(node));
    }
    return nodes;
}

bool LoadClipboardRoots(const std::string& actorClipboardJson, std::vector<std::vector<PrefabNode>>& roots) {
    roots.clear();
    if (actorClipboardJson.empty())
        return false;
    try {
        const nlohmann::json value = nlohmann::json::parse(actorClipboardJson);
        if (value.is_array()) {
            std::vector<PrefabNode> nodes = LoadClipboardRootNodes(value);
            if (nodes.empty())
                return false;
            roots.push_back(std::move(nodes));
            return true;
        }
        if (!value.is_object())
            return false;
        const nlohmann::json& rootValues = value.contains("roots") ? value["roots"] : value["actorRoots"];
        if (!rootValues.is_array())
            return false;
        for (const auto& rootValue : rootValues) {
            std::vector<PrefabNode> nodes = LoadClipboardRootNodes(rootValue);
            if (nodes.empty())
                return false;
            roots.push_back(std::move(nodes));
        }
        return !roots.empty();
    } catch (...) {
        return false;
    }
}

nlohmann::json ClipboardRootToJson(const std::vector<PrefabNode>& nodes) {
    nlohmann::json root = nlohmann::json::array();
    for (const PrefabNode& node : nodes)
        root.push_back(PrefabNodeToJson(node));
    return root;
}

std::string TemplateExtensionFor(const std::string& templateID) {
    if (templateID == "angelscript" || templateID == "as")
        return ".as";
    if (templateID == "lua")
        return ".lua";
    if (templateID == "shader")
        return ".shader";
    if (templateID == "material" || templateID == "mat")
        return ".mat";
    if (templateID == "texture" || templateID == "tex")
        return ".tex";
    if (templateID == "prefab")
        return ".prefab.json";
    if (templateID == "ui")
        return ".rml";
    if (templateID == "scene")
        return ".scene.json";
    return ".json";
}

std::string TemplateBaseNameFor(const std::string& templateID) {
    if (templateID == "angelscript" || templateID == "as")
        return "NewScript";
    if (templateID == "lua")
        return "NewLuaScript";
    if (templateID == "shader")
        return "NewShader";
    if (templateID == "material" || templateID == "mat")
        return "NewMaterial";
    if (templateID == "texture" || templateID == "tex")
        return "NewTexture";
    if (templateID == "prefab")
        return "NewPrefab";
    if (templateID == "ui")
        return "NewDocument";
    if (templateID == "scene")
        return "NewScene";
    return "NewAsset";
}

std::string TemplateContentFor(const std::string& templateID) {
    if (templateID == "angelscript" || templateID == "as") {
        return "class NewScript\n{\n    void Start() {}\n    void Update(float dt) {}\n}\n";
    }
    if (templateID == "lua")
        return "function update(dt)\nend\n";
    if (templateID == "shader")
        return R"JSON({
  "type": "Shader",
  "version": 2,
  "name": "NewShader",
  "mode": "Graph",
  "domain": "Surface",
  "shadingModel": "Lit",
  "surfaceType": "Opaque",
  "properties": [
    { "id": "surface.baseColor", "name": "Base Color", "type": "Color", "default": [1, 1, 1, 1], "sRGB": true },
    { "id": "surface.metallic", "name": "Metallic", "type": "Float", "default": 0, "range": [0, 1] },
    { "id": "surface.roughness", "name": "Roughness", "type": "Float", "default": 0.5, "range": [0.04, 1] }
  ],
  "graph": {
    "version": 1,
    "nodes": [
      { "id": 10, "type": "Property", "property": "surface.baseColor", "position": [-360, -120], "pins": [{"id":11,"name":"Out","type":"Any","direction":"Output"}] },
      { "id": 20, "type": "Property", "property": "surface.metallic", "position": [-360, 20], "pins": [{"id":21,"name":"Out","type":"Any","direction":"Output"}] },
      { "id": 30, "type": "Property", "property": "surface.roughness", "position": [-360, 150], "pins": [{"id":31,"name":"Out","type":"Any","direction":"Output"}] },
      { "id": 100, "type": "SurfaceOutputLit", "position": [80, 0], "pins": [
        {"id":101,"name":"BaseColor","type":"Any","direction":"Input"}, {"id":102,"name":"Normal","type":"Any","direction":"Input"},
        {"id":103,"name":"Metallic","type":"Any","direction":"Input"}, {"id":104,"name":"Roughness","type":"Any","direction":"Input"},
        {"id":105,"name":"AmbientOcclusion","type":"Any","direction":"Input"}, {"id":106,"name":"Emissive","type":"Any","direction":"Input"},
        {"id":107,"name":"Opacity","type":"Any","direction":"Input"}, {"id":108,"name":"AlphaClip","type":"Any","direction":"Input"}
      ]}
    ],
    "links": [
      {"id":200,"fromNode":10,"fromPin":"Out","fromPinId":11,"toNode":100,"toPin":"BaseColor","toPinId":101},
      {"id":201,"fromNode":20,"fromPin":"Out","fromPinId":21,"toNode":100,"toPin":"Metallic","toPinId":103},
      {"id":202,"fromNode":30,"fromPin":"Out","fromPinId":31,"toNode":100,"toPin":"Roughness","toPinId":104}
    ]
  }
})JSON";
    if (templateID == "material" || templateID == "mat") {
        return "{\n"
               "  \"type\": \"Material\",\n"
               "  \"version\": 2,\n"
               "  \"name\": \"NewMaterial\",\n"
               "  \"shader\": \"Content/Engine/Shaders/StandardSurface.shader\",\n"
               "  \"surface\": {},\n"
               "  \"overrides\": { \"properties\": {}, \"textures\": {} }\n"
               "}\n";
    }
    if (templateID == "texture" || templateID == "tex")
        return "{}\n";
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
    if (templateID == "ui")
        return "<rml>\n  <body>\n  </body>\n</rml>\n";
    if (templateID == "scene")
        return "{\n  \"version\": 1,\n  \"name\": \"New Scene\",\n  \"actors\": []\n}\n";
    return "{}";
}

std::filesystem::path MakeUniquePath(const std::filesystem::path& directory, const std::string& baseName,
                                     const std::string& extension) {
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

std::string PrefabTemplateContentForUuid(const std::string& uuid) {
    nlohmann::json root = {{"version", 1},
                           {"uuid", uuid},
                           {"rootLocalId", "root"},
                           {"nodes", nlohmann::json::array({{{"localId", "root"},
                                                             {"parentLocalId", ""},
                                                             {"name", "NewPrefab"},
                                                             {"active", true},
                                                             {"transform",
                                                              {{"position", nlohmann::json::array({0.0f, 0.0f, 0.0f})},
                                                               {"rotation", nlohmann::json::array({0.0f, 0.0f, 0.0f})},
                                                               {"scale", nlohmann::json::array({1.0f, 1.0f, 1.0f})}}},
                                                             {"components", nlohmann::json::array()}}})}};
    return root.dump(2) + "\n";
}

bool CreatePrefabTemplateAsset(EditorContext& context, const std::filesystem::path& path) {
    auto execute = [path](EditorContext&) {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error)
            return false;

        AssetMeta meta = AssetMeta::Create(path.string());
        const std::string content = PrefabTemplateContentForUuid(meta.uuid);
        {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(content.data(), static_cast<std::streamsize>(content.size()));
            output.close();
            if (!output.good())
                return false;
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
        if (error || !removed)
            return false;
        std::filesystem::remove(AssetMeta::MetaPathFor(path.string()), error);
        return true;
    };
    EditorCommandStack* stack = context.GetCommandStack();
    return stack ? stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>("Create Prefab", execute, undo), context)
                 : execute(context);
}

bool CommitPrefabSceneSnapshot(EditorContext& context, Actor& actor, const char* label,
                               const std::function<bool(Actor&, std::string*)>& edit) {
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack || !context.CanEditScene())
        return false;
    const uint64_t selection = actor.GetID();
    const std::string before = SceneSerializer::SaveToString(*scene);
    std::string error;
    if (!edit(actor, &error)) {
        if (!error.empty())
            Logger::Warn("[Editor] ", label, " failed: ", error);
        return false;
    }
    const std::string after = SceneSerializer::SaveToString(*scene);
    SceneSerializer::LoadFromString(*scene, before);
    return stack->ExecuteCommand(EditorUndoUtil::MakeSceneSnapshotCommand(label, before, after, selection, selection),
                                 context);
}

std::string JsonPreview(const nlohmann::json& value) {
    if (value.is_string())
        return value.get<std::string>();
    std::string text = value.dump();
    constexpr size_t kMaxPreview = 80;
    if (text.size() > kMaxPreview)
        text = text.substr(0, kMaxPreview - 3) + "...";
    return text;
}

std::string DecodeJsonPointerToken(std::string token) {
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

std::string PropertyLabelFromPointer(const std::string& path) {
    if (path.empty() || path == "/")
        return "(self)";
    std::string label;
    size_t start = path.front() == '/' ? 1 : 0;
    while (start <= path.size()) {
        const size_t slash = path.find('/', start);
        std::string token = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (!token.empty()) {
            if (!label.empty())
                label += ".";
            label += DecodeJsonPointerToken(std::move(token));
        }
        if (slash == std::string::npos)
            break;
        start = slash + 1;
    }
    return label.empty() ? path : label;
}

std::string OverrideCategory(const nlohmann::json& item) {
    const std::string kind = item.value("kind", std::string{});
    const std::string componentType = item.value("componentType", std::string{});
    if (kind == "AddActorSubtree" || kind == "RemoveActorSubtree" || kind == "SetNestedInstance")
        return "Hierarchy";
    if (kind == "AddComponent" || kind == "RemoveComponent" || !componentType.empty()) {
        return "Component";
    }
    if (kind == "SetProperty" || kind == "RemoveProperty")
        return "Actor";
    return "Unsupported";
}

int OverrideCategoryRank(const std::string& category) {
    if (category == "Actor")
        return 0;
    if (category == "Component")
        return 1;
    if (category == "Hierarchy")
        return 2;
    return 3;
}

std::string OverrideTargetLabel(const nlohmann::json& item) {
    const std::string kind = item.value("kind", std::string{});
    const std::string localId = item.value("localId", std::string{});
    const std::string componentType = item.value("componentType", std::string{});
    if (kind == "AddActorSubtree")
        return "Parent " + localId;
    if (kind == "RemoveActorSubtree")
        return "Actor " + localId;
    if (kind == "SetNestedInstance")
        return "Nested " + item.value("nestedInstanceLocalId", std::string{});
    if (!componentType.empty())
        return componentType + " on " + localId;
    return "Actor " + localId;
}

std::string OverridePropertyLabel(const nlohmann::json& item) {
    const std::string kind = item.value("kind", std::string{});
    if (kind == "AddActorSubtree")
        return "Added subtree";
    if (kind == "RemoveActorSubtree")
        return "Removed subtree";
    if (kind == "SetNestedInstance")
        return "Nested instance state";
    if (kind == "AddComponent")
        return "Added component";
    if (kind == "RemoveComponent")
        return "Removed component";
    return PropertyLabelFromPointer(item.value("path", std::string{}));
}

std::string OverrideValuePreview(const nlohmann::json& item) {
    const std::string kind = item.value("kind", std::string{});
    if (item.contains("value"))
        return JsonPreview(item["value"]);
    if (kind == "RemoveProperty")
        return "(removed)";
    if (kind == "AddComponent") {
        const nlohmann::json* data = item.contains("data") ? &item["data"] : nullptr;
        return data ? JsonPreview(*data) : "(added)";
    }
    if (kind == "RemoveComponent")
        return "(removed)";
    if (kind == "AddActorSubtree") {
        const size_t count = item.contains("nodes") && item["nodes"].is_array() ? item["nodes"].size() : 0;
        return std::to_string(count) + (count == 1 ? " node" : " nodes");
    }
    if (kind == "RemoveActorSubtree")
        return "(removed)";
    if (kind == "SetNestedInstance") {
        return item.contains("overrides") ? JsonPreview(item["overrides"]) : "(updated)";
    }
    return {};
}

std::string OverrideLabel(const nlohmann::json& item) {
    const std::string kind = item.value("kind", std::string{});
    const std::string id = item.value("localId", std::string{});
    const std::string type = item.value("componentType", std::string{});
    const std::string path = item.value("path", std::string{});
    if (kind == "AddActorSubtree")
        return "Add actor subtree under " + id;
    if (kind == "RemoveActorSubtree")
        return "Remove actor subtree " + id;
    if (kind == "SetNestedInstance")
        return "Update nested prefab " + item.value("nestedInstanceLocalId", std::string{});
    if (kind == "AddComponent")
        return "Add " + type + " on " + id;
    if (kind == "RemoveComponent")
        return "Remove " + type + " from " + id;
    if (!type.empty())
        return type + (path.empty() ? std::string{} : " " + path);
    if (!path.empty())
        return id + " " + path;
    return kind.empty() ? "Unknown override" : kind + " " + id;
}

bool IsSupportedPrefabOverride(const nlohmann::json& item) {
    const std::string kind = item.value("kind", std::string{});
    return kind == "SetProperty" || kind == "RemoveProperty" || kind == "AddComponent" || kind == "RemoveComponent" ||
           kind == "AddActorSubtree" || kind == "RemoveActorSubtree" || kind == "SetNestedInstance";
}

nlohmann::json RemoveOverrideAt(nlohmann::json overrides, size_t index) {
    if (!overrides.is_array() || index >= overrides.size())
        return nlohmann::json::array();
    overrides.erase(overrides.begin() + static_cast<nlohmann::json::difference_type>(index));
    return overrides;
}

Actor* ResolveEditablePrefabActor(EditorContext& context, uint64_t actorID) {
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    return actor && actor->IsPrefabRoot() ? actor : nullptr;
}

bool RestorePrefabEditorState(EditorContext& context, const std::filesystem::path& prefabPath,
                              const std::string& prefabContent, const std::string& sceneJson, uint64_t actorID) {
    Scene* scene = context.GetScene();
    if (!scene || !WriteFileContent(prefabPath, prefabContent) || !SceneSerializer::LoadFromString(*scene, sceneJson)) {
        return false;
    }
    if (scene->FindByID(actorID))
        context.GetSelection().SelectActorID(actorID);
    else
        context.GetSelection().Clear();
    if (EditorAssetRegistry* registry = context.GetAssetRegistry())
        registry->Refresh();
    return true;
}

bool ApplySinglePrefabOverrideNow(EditorContext& context, uint64_t actorID, size_t overrideIndex, std::string* error) {
    Actor* actor = ResolveEditablePrefabActor(context, actorID);
    if (!actor) {
        if (error)
            *error = "actor is not a prefab instance root";
        return false;
    }
    nlohmann::json overrides;
    if (!PrefabSystem::BuildOverrides(*actor, overrides, error))
        return false;
    if (!overrides.is_array() || overrideIndex >= overrides.size()) {
        if (error)
            *error = "prefab override index is out of range";
        return false;
    }
    const nlohmann::json item = overrides[overrideIndex];
    if (!IsSupportedPrefabOverride(item)) {
        if (error)
            *error = "unsupported prefab override kind: " + item.value("kind", std::string{});
        return false;
    }
    const std::filesystem::path prefabPath = PrefabSystem::ResolvePrefabPath(actor->GetPrefabAssetPath());
    PrefabAsset asset;
    if (!PrefabAsset::Load(prefabPath, asset, error))
        return false;
    nlohmann::json single = nlohmann::json::array();
    single.push_back(item);
    if (!PrefabSystem::ApplyOverridesToAsset(asset, single, error) || !asset.Save(prefabPath, error)) {
        return false;
    }
    nlohmann::json remaining;
    if (!PrefabSystem::BuildOverrides(*actor, remaining, error) ||
        !PrefabSystem::SetInstanceOverrides(*actor, std::move(remaining), error)) {
        return false;
    }
    return actor->GetScene() && PrefabSystem::RefreshInstances(*actor->GetScene(), actor->GetPrefabAssetUuid(), error);
}

bool RevertSinglePrefabOverrideNow(EditorContext& context, uint64_t actorID, size_t overrideIndex, std::string* error) {
    Actor* actor = ResolveEditablePrefabActor(context, actorID);
    if (!actor) {
        if (error)
            *error = "actor is not a prefab instance root";
        return false;
    }
    nlohmann::json overrides;
    if (!PrefabSystem::BuildOverrides(*actor, overrides, error))
        return false;
    if (!overrides.is_array() || overrideIndex >= overrides.size()) {
        if (error)
            *error = "prefab override index is out of range";
        return false;
    }
    if (!IsSupportedPrefabOverride(overrides[overrideIndex])) {
        if (error)
            *error = "unsupported prefab override kind: " + overrides[overrideIndex].value("kind", std::string{});
        return false;
    }
    nlohmann::json remaining = RemoveOverrideAt(std::move(overrides), overrideIndex);
    if (!PrefabSystem::SetInstanceOverrides(*actor, std::move(remaining), error))
        return false;
    return actor->GetScene() && PrefabSystem::RefreshInstances(*actor->GetScene(), actor->GetPrefabAssetUuid(), error);
}

bool ResolveViewportFrameTarget(EditorContext& context, Vec3& target, float& radius) {
    Scene* scene = context.GetInspectorScene();
    Actor* actor = scene ? context.GetSelection().ResolveActor(*scene) : nullptr;
    if (!actor)
        return false;
    target = actor->GetWorldMatrix().TransformPoint(Vec3::Zero());
    radius = 1.0f;
    return true;
}

} // namespace
