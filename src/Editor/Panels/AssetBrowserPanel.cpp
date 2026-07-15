#include "Editor/EditorPanels.h"

#include "Core/Logger.h"
#include "Editor/EditorAssetRegistry.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorContextMenu.h"
#include "Editor/EditorDialogService.h"
#include "Editor/EditorDragDrop.h"
#include "Editor/EditorImportService.h"
#include "Editor/EditorOperators.h"
#include "Editor/EditorPanelHelpers.h"
#include "Editor/EditorProject.h"
#include "Editor/EditorWorkspace.h"
#include "Editor/EditorUI/EditorAngelScriptDomain.h"
#include "Editor/UI/EditorIcons.h"
#include "Editor/UI/EditorViewportPolicy.h"
#include "Editor/UI/EditorWidgets.h"
#include "Game/SceneRenderLayer.h"
#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#endif

#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iterator>
#include <vector>

namespace {
constexpr const char kModelPayload[] = "MYENGINE_MODEL_PATH";
constexpr const char kTexturePayload[] = "MYENGINE_TEXTURE_PATH";
constexpr const char kPrefabPayload[] = "MYENGINE_PREFAB_PATH";
constexpr const char kAssetPayload[] = "MYENGINE_ASSET_PATH";
constexpr const char kFolderPayload[] = "MYENGINE_FOLDER_PATH";
namespace EditorIcons = Editor::UI::EditorIcons;
namespace EditorWidgets = Editor::UI::EditorWidgets;

const char* IconForAssetType(EditorAssetType type) {
    switch (type) {
    case EditorAssetType::Model:
        return EditorIcons::Mesh;
    case EditorAssetType::Texture:
        return EditorIcons::Texture;
    case EditorAssetType::Material:
        return EditorIcons::Material;
    case EditorAssetType::Scene:
        return EditorIcons::Scene;
    case EditorAssetType::Prefab:
        return EditorIcons::Prefab;
    case EditorAssetType::Script:
        return EditorIcons::Script;
    case EditorAssetType::Shader:
        return EditorIcons::Shader;
    case EditorAssetType::Audio:
        return EditorIcons::Audio;
    case EditorAssetType::UI:
        return EditorIcons::Input;
    case EditorAssetType::Particle:
        return EditorIcons::Asset;
    case EditorAssetType::Navigation:
        return EditorIcons::Scene;
    default:
        return EditorIcons::Asset;
    }
}

const char* ImportStateText(AssetImportState state) {
    switch (state) {
    case AssetImportState::Importing:
        return "importing";
    case AssetImportState::Failed:
        return "failed";
    case AssetImportState::Stale:
        return "stale";
    case AssetImportState::MissingSource:
        return "missing source";
    default:
        return "ready";
    }
}

const char* AssetTypeName(EditorAssetType type) {
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

EditorAssetType AssetTypeFromFilterIndex(int index) {
    switch (index) {
    case 1:
        return EditorAssetType::Model;
    case 2:
        return EditorAssetType::Texture;
    case 3:
        return EditorAssetType::Material;
    case 4:
        return EditorAssetType::Scene;
    case 5:
        return EditorAssetType::Prefab;
    case 6:
        return EditorAssetType::Script;
    case 7:
        return EditorAssetType::Shader;
    case 8:
        return EditorAssetType::Audio;
    case 9:
        return EditorAssetType::UI;
    case 10:
        return EditorAssetType::Particle;
    case 11:
        return EditorAssetType::Navigation;
    default:
        return EditorAssetType::Unknown;
    }
}

bool AssetMatchesImportFilter(const EditorAssetInfo& asset, int filter) {
    switch (filter) {
    case 1:
        return asset.imported && asset.importState == AssetImportState::Ready;
    case 2:
        return asset.imported && asset.importState == AssetImportState::Importing;
    case 3:
        return asset.imported && asset.importState == AssetImportState::Failed;
    case 4:
        return asset.imported && asset.importState == AssetImportState::Stale;
    case 5:
        return asset.imported && asset.importState == AssetImportState::MissingSource;
    case 6:
        return asset.imported;
    case 7:
        return !asset.imported;
    default:
        return true;
    }
}

bool AssetMatchesBrowserFilters(const EditorAssetInfo& asset, const char* textFilter, int typeFilter,
                                int importStateFilter, bool diagnosticsOnly) {
    if (textFilter && textFilter[0] && asset.relativePath.find(textFilter) == std::string::npos) {
        return false;
    }
    const EditorAssetType type = AssetTypeFromFilterIndex(typeFilter);
    if (type != EditorAssetType::Unknown && asset.type != type)
        return false;
    if (!AssetMatchesImportFilter(asset, importStateFilter))
        return false;
    if (diagnosticsOnly && asset.diagnostics.empty())
        return false;
    return true;
}

void DrawScriptAssetContextMenu(EditorContext& context, EditorAssetType type) {
    EditorAngelScriptDomain* domain = context.GetEditorScriptDomain();
    if (!domain || !domain->IsLoaded() || !domain->GetConfig().enableContextMenuExtensions) {
        return;
    }
    const std::string typeName = AssetTypeName(type);
    for (const auto& extension : domain->GetRegistry().GetAssetContextMenus()) {
        if (extension.targetType != "*" && extension.targetType != "Any" && extension.targetType != typeName) {
            continue;
        }
        std::string error;
        const std::string stateKey = "assetContext:" + typeName;
        if (!domain->ExecuteExtension(extension.callback, stateKey, context, &error) && !error.empty()) {
            Logger::Warn("[EditorScript] Asset context menu failed for ", typeName, ": ", error);
        }
    }
}

bool StartsWithPath(const std::string& value, const std::string& prefix) {
    return value == prefix || (value.size() > prefix.size() && value.compare(0, prefix.size(), prefix) == 0 &&
                               value[prefix.size()] == '/');
}

bool IsDirectChildFolder(const std::string& parent, const std::string& child) {
    if (!StartsWithPath(child, parent) || child == parent)
        return false;
    const std::string suffix = child.substr(parent.size() + 1);
    return suffix.find('/') == std::string::npos;
}

bool IsRootFolder(const std::string& folder) {
    return folder == "Content" || folder == "SourceAssets";
}

bool IsPathTraversalRelative(const std::filesystem::path& path) {
    for (const auto& part : path) {
        if (part == "..")
            return true;
    }
    return false;
}

std::string AssetParentFolderForBrowser(const EditorContext& context, const std::string& assetPath) {
    namespace fs = std::filesystem;
    if (assetPath.empty())
        return {};

    const fs::path rawPath = fs::path(assetPath).lexically_normal();
    const fs::path parent = rawPath.parent_path().lexically_normal();
    const std::string parentGeneric = parent.generic_string();
    if (StartsWithPath(parentGeneric, "Content") || StartsWithPath(parentGeneric, "SourceAssets")) {
        return parentGeneric;
    }

    auto relativeToRoot = [&](const fs::path& root, const char* browserRoot) -> std::string {
        if (root.empty())
            return {};
        std::error_code error;
        const fs::path relative = fs::relative(parent, root.lexically_normal(), error);
        if (error || relative.empty() || IsPathTraversalRelative(relative))
            return {};
        const std::string relativeGeneric = relative.generic_string();
        if (relativeGeneric.empty() || relativeGeneric == ".")
            return browserRoot;
        return std::string(browserRoot) + "/" + relativeGeneric;
    };

    if (std::string folder = relativeToRoot(context.GetContentRoot(), "Content"); !folder.empty()) {
        return folder;
    }

    const fs::path sourceRoot = context.GetContentRoot().parent_path() / "SourceAssets";
    return relativeToRoot(sourceRoot, "SourceAssets");
}

const char* TemplateIDForAssetAction(std::string_view actionID) {
    if (actionID == "asset.createMaterial")
        return "material";
    if (actionID == "asset.createTexture")
        return "texture";
    if (actionID == "asset.createPrefab")
        return "prefab";
    if (actionID == "asset.createAngelScript")
        return "as";
    if (actionID == "asset.createLua")
        return "lua";
    if (actionID == "asset.createShader")
        return "shader";
    if (actionID == "asset.createUI")
        return "ui";
    if (actionID == "asset.createScene")
        return "scene";
    return nullptr;
}

const char* TemplateFallbackFolder(const char* templateID) {
    if (!templateID)
        return "";
    if (std::strcmp(templateID, "material") == 0)
        return "Materials";
    if (std::strcmp(templateID, "texture") == 0)
        return "Textures";
    if (std::strcmp(templateID, "prefab") == 0)
        return "Prefabs";
    return "";
}

int ParseWorkspaceInt(const EditorWorkspace& workspace, const std::string& panelID, const char* key, int fallback) {
    const auto value = workspace.GetPanelStateValue(panelID, key);
    if (!value)
        return fallback;
    try {
        return std::stoi(*value);
    } catch (...) {
        return fallback;
    }
}

bool ParseWorkspaceBool(const EditorWorkspace& workspace, const std::string& panelID, const char* key, bool fallback) {
    const auto value = workspace.GetPanelStateValue(panelID, key);
    if (!value)
        return fallback;
    return *value == "true" || *value == "1";
}

std::string NormalizePanelAssetPath(const std::string& path) {
    return std::filesystem::path(path).lexically_normal().string();
}

size_t CountSceneReferencesForAssets(EditorContext& context, EditorOperators* operators,
                                     const std::vector<std::string>& paths) {
    if (!operators)
        return 0;
    size_t count = 0;
    for (const std::string& path : paths) {
        count += operators->Assets().FindSceneReferences(context, path).size();
    }
    return count;
}

size_t CountProjectSceneReferencesForAssets(EditorContext& context, EditorOperators* operators,
                                            const std::vector<std::string>& paths) {
    if (!operators)
        return 0;
    size_t count = 0;
    for (const std::string& path : paths) {
        count += operators->Assets().FindProjectSceneReferences(context, path).size();
    }
    return count;
}

size_t CountProjectSceneReferencesForFolder(EditorContext& context, EditorOperators* operators,
                                            const std::string& folder) {
    auto* registry = context.GetAssetRegistry();
    if (!operators || !registry || folder.empty())
        return 0;
    size_t count = 0;
    for (const auto& asset : registry->GetAssetsInFolder(
             std::filesystem::path(folder).lexically_normal().generic_string(), true, EditorAssetType::Unknown)) {
        count += operators->Assets().FindProjectSceneReferences(context, asset.relativePath).size();
    }
    return count;
}

std::string AppendRetargetReferenceWarning(std::string message, size_t currentSceneReferenceCount,
                                           size_t projectSceneReferenceCount = 0) {
    if (currentSceneReferenceCount == 0 && projectSceneReferenceCount == 0) {
        return message;
    }
    message += "; warning: ";
    bool needsSeparator = false;
    if (currentSceneReferenceCount > 0) {
        message += std::to_string(currentSceneReferenceCount);
        message += " current scene reference(s)";
        needsSeparator = true;
    }
    if (projectSceneReferenceCount > 0) {
        if (needsSeparator)
            message += " and ";
        message += std::to_string(projectSceneReferenceCount);
        message += " project scene reference(s)";
    }
    message += " still point to old asset path(s)";
    return message;
}
} // namespace

AssetBrowserPanel::AssetBrowserPanel() : EditorPanel("assetBrowser", "Asset Browser") {
}
void AssetBrowserPanel::OnAttach(EditorContext& context) {
    EditorPanel::OnAttach(context);
    LoadWorkspaceState();
    if (auto* operators = context.GetOperators()) {
        operators->Assets().Refresh(context);
    } else {
        EditorAssetOperator assetOperator;
        assetOperator.Refresh(context);
    }
}
void AssetBrowserPanel::OnDetach() {
    SaveWorkspaceState();
    EditorPanel::OnDetach();
}
void AssetBrowserPanel::OnUpdate(float dt) {
    auto* context = GetContext();
    auto* operators = context ? context->GetOperators() : nullptr;
    if (operators)
        operators->Assets().WatchIfDue(*context, dt, m_WatchAccumulator);
}

bool AssetBrowserPanel::HandleEditorAction(EditorContext& context, std::string_view actionID) {
    if (!context.IsEditing())
        return false;
    SyncAssetSelectionFromContext(context);

    if (actionID == "asset.createFolder") {
        EnsureSelectedFolder();
        return RequestCreateFolderInFolder(context, m_SelectedFolder, false);
    }

    if (const char* templateID = TemplateIDForAssetAction(actionID)) {
        EnsureSelectedFolder();
        return RequestCreateAssetFromTemplateInFolder(context, m_SelectedFolder, templateID);
    }

    if (actionID == "asset.open") {
        auto* operators = context.GetOperators();
        if (!operators || !context.GetSelection().HasAsset())
            return false;
        return RequestOpenAsset(context.GetSelection().GetAssetPath());
    }

    if (actionID == "asset.reveal") {
        if (context.GetSelection().HasAsset()) {
            return RequestRevealPath(context.GetSelection().GetAssetPath());
        }
        EnsureSelectedFolder();
        if (m_SelectedFolder.empty())
            return false;
        return RequestRevealPath(m_SelectedFolder);
    }

    if (actionID == "asset.move") {
        EnsureSelectedFolder();
        return MoveSelectedAssetsToFolder(context, CurrentContentDirectory("").string());
    }

    if (actionID == "asset.rename") {
        actionID = "edit.rename";
    }

    if (actionID == "edit.selectAll") {
        EnsureSelectedFolder();
        auto* registry = context.GetAssetRegistry();
        if (!registry)
            return false;

        std::vector<EditorAssetInfo> visibleAssets;
        for (const auto& asset :
             registry->GetAssetsInFolder(m_SelectedFolder, m_RecursiveAssets, AssetTypeFromFilterIndex(m_TypeFilter))) {
            if (!AssetMatchesBrowserFilters(asset, m_Filter, m_TypeFilter, m_ImportStateFilter, m_DiagnosticsOnly)) {
                continue;
            }
            visibleAssets.push_back(asset);
        }
        SelectVisibleAssets(context, visibleAssets);
        return !visibleAssets.empty();
    }

    if (actionID == "edit.delete") {
        if (!ActiveSelectedAssetPaths(context).empty()) {
            RequestDeleteSelectedAssets();
            return true;
        }
        if (IsFocused() && !m_SelectedFolder.empty() && !IsRootFolder(m_SelectedFolder)) {
            RequestDeleteSelectedFolder();
            return true;
        }
        return false;
    }

    if (actionID == "edit.duplicate") {
        if (ActiveSelectedAssetPaths(context).empty())
            return false;
        DuplicateSelectedAssets();
        return true;
    }

    if (actionID == "edit.copy") {
        const auto selectedPaths = ActiveSelectedAssetPaths(context);
        if (selectedPaths.empty())
            return false;
        auto* operators = context.GetOperators();
        const bool copied = operators && operators->Commands().CopyAssets(context, selectedPaths);
        SetOperationMessage(
            copied ? (selectedPaths.size() == 1
                          ? "Copied asset: " + std::filesystem::path(selectedPaths.front()).filename().string()
                          : "Copied " + std::to_string(selectedPaths.size()) + " asset(s)")
                   : "Failed to copy selected asset",
            !copied);
        return copied;
    }

    if (actionID == "edit.paste") {
        EnsureSelectedFolder();
        return RequestPasteAssetsToFolder(context, m_SelectedFolder);
    }

    if (actionID != "edit.rename")
        return false;
    if (context.GetSelection().HasAsset()) {
        return StartRenameSelectedAsset();
    }
    if (IsFocused() && !m_SelectedFolder.empty() && !IsRootFolder(m_SelectedFolder)) {
        StartRenameFolder(m_SelectedFolder);
        return true;
    }
    return false;
}

bool AssetBrowserPanel::CanHandleEditorAction(const EditorContext& context, std::string_view actionID) const {
    if (!context.IsEditing())
        return false;

    if (actionID == "asset.createFolder") {
        return !m_SelectedFolder.empty();
    }

    if (TemplateIDForAssetAction(actionID)) {
        return !m_SelectedFolder.empty();
    }

    if (actionID == "asset.open") {
        return context.GetSelection().HasAsset();
    }

    if (actionID == "asset.reveal") {
        return context.GetSelection().HasAsset() || !m_SelectedFolder.empty();
    }

    if (actionID == "asset.move") {
        const auto selectedPaths = ActiveSelectedAssetPaths(context);
        if (selectedPaths.empty() || m_SelectedFolder.empty()) {
            return false;
        }
        const std::filesystem::path targetFolder = CurrentContentDirectory("").lexically_normal();
        if (targetFolder.empty())
            return false;
        return std::any_of(selectedPaths.begin(), selectedPaths.end(), [&](const std::string& path) {
            return std::filesystem::path(path).lexically_normal().parent_path().lexically_normal() != targetFolder;
        });
    }

    if (actionID == "asset.rename") {
        actionID = "edit.rename";
    }

    if (actionID == "edit.selectAll") {
        auto* registry = context.GetAssetRegistry();
        if (!registry)
            return false;
        for (const auto& asset :
             registry->GetAssetsInFolder(m_SelectedFolder, m_RecursiveAssets, AssetTypeFromFilterIndex(m_TypeFilter))) {
            if (AssetMatchesBrowserFilters(asset, m_Filter, m_TypeFilter, m_ImportStateFilter, m_DiagnosticsOnly)) {
                return true;
            }
        }
        return false;
    }

    if (actionID == "edit.delete") {
        return !ActiveSelectedAssetPaths(context).empty() ||
               (IsFocused() && !m_SelectedFolder.empty() && !IsRootFolder(m_SelectedFolder));
    }

    if (actionID == "edit.duplicate" || actionID == "edit.copy") {
        return !ActiveSelectedAssetPaths(context).empty();
    }

    if (actionID == "edit.paste") {
        auto* operators = context.GetOperators();
        return operators && operators->Commands().HasAssetClipboard() && !m_SelectedFolder.empty();
    }

    if (actionID == "edit.rename") {
        return context.GetSelection().HasAsset() ||
               (IsFocused() && !m_SelectedFolder.empty() && !IsRootFolder(m_SelectedFolder));
    }

    return false;
}

void AssetBrowserPanel::DeleteSelectedAsset() {
    auto* context = GetContext();
    if (!context)
        return;
    const std::string& path = context->GetSelection().GetAssetPath();
    if (path.empty())
        return;
    auto* operators = context->GetOperators();
    if (!operators || !operators->Assets().DeleteAsset(*context, path)) {
        const std::string message = "Failed to delete asset: " + path;
        Logger::Warn("[Editor] ", message);
        SetOperationMessage(message, true);
        return;
    }
    SetOperationMessage("Deleted asset: " + std::filesystem::path(path).filename().string(), false);
    m_PendingDelete = false;
}

void AssetBrowserPanel::DeleteSelectedAssets() {
    auto* context = GetContext();
    if (!context)
        return;
    const auto paths = ActiveSelectedAssetPaths(*context);
    if (paths.empty())
        return;
    auto* operators = context->GetOperators();
    size_t deletedCount = 0;
    for (const std::string& path : paths) {
        if (operators && operators->Assets().DeleteAsset(*context, path)) {
            ++deletedCount;
        }
    }
    m_SelectedAssetPaths.clear();
    m_LastPrimaryAssetPath.clear();
    m_PendingDelete = false;
    SetOperationMessage(deletedCount > 0 ? "Deleted " + std::to_string(deletedCount) + " asset(s)"
                                         : "Failed to delete selected asset(s)",
                        deletedCount == 0);
}

void AssetBrowserPanel::DrawDeleteReferenceWarning(const std::vector<std::string>& paths) {
#if defined(MYENGINE_ENABLE_IMGUI)
    auto* context = GetContext();
    auto* operators = context ? context->GetOperators() : nullptr;
    if (!operators || paths.empty())
        return;

    struct ReferenceRow {
        std::string assetName;
        EditorAssetOperator::SceneReferenceInfo reference;
    };
    std::vector<ReferenceRow> rows;
    size_t totalReferences = 0;
    for (const std::string& path : paths) {
        const auto references = operators->Assets().FindSceneReferences(*context, path);
        const auto projectReferences = operators->Assets().FindProjectSceneReferences(*context, path);
        totalReferences += references.size();
        totalReferences += projectReferences.size();
        for (const auto& reference : references) {
            if (rows.size() >= 8)
                continue;
            rows.push_back({std::filesystem::path(path).filename().string(), reference});
        }
        for (const auto& reference : projectReferences) {
            if (rows.size() >= 8)
                continue;
            rows.push_back({std::filesystem::path(path).filename().string(), reference});
        }
    }
    if (totalReferences == 0)
        return;

    const std::string warning =
        std::to_string(totalReferences) + " current/project scene reference(s) will become unresolved.";
    EditorWidgets::InlineMessage(Editor::UI::EditorNotificationType::Warning, warning.c_str());
    for (const ReferenceRow& row : rows) {
        ImGui::BulletText("%s -> %s%s%s / %s %s", row.assetName.c_str(),
                          row.reference.scenePath.empty() ? "" : row.reference.scenePath.c_str(),
                          row.reference.scenePath.empty() ? "" : " / ",
                          row.reference.actorName.empty() ? "(unnamed actor)" : row.reference.actorName.c_str(),
                          row.reference.componentType.c_str(), row.reference.jsonPath.c_str());
    }
    if (totalReferences > rows.size()) {
        ImGui::TextDisabled("...and %zu more reference(s)", totalReferences - rows.size());
    }
#else
    (void)paths;
#endif
}

std::vector<std::string> AssetBrowserPanel::CollectAssetPathsForFolder(const EditorContext& context,
                                                                       const std::string& folderPath) const {
    std::vector<std::string> paths;
    auto* registry = context.GetAssetRegistry();
    if (!registry || folderPath.empty())
        return paths;
    for (const auto& asset : registry->GetAssetsInFolder(
             std::filesystem::path(folderPath).lexically_normal().generic_string(), true, EditorAssetType::Unknown)) {
        paths.push_back(asset.absolutePath.string());
    }
    return paths;
}

void AssetBrowserPanel::RequestDeleteSelectedAssets() {
    m_PendingDelete = true;
    m_PendingFolderDelete = false;
    m_PendingFolderDeletePath.clear();
    m_PendingRename = false;
    m_PendingFolderRename = false;
    m_PendingFolderRenamePath.clear();
}

void AssetBrowserPanel::RequestDeleteFolder(const std::string& folderPath) {
    if (folderPath.empty() || IsRootFolder(folderPath))
        return;
    m_PendingDelete = false;
    m_PendingFolderDelete = true;
    m_PendingFolderDeletePath = folderPath;
    m_PendingRename = false;
    m_PendingFolderRename = false;
    m_PendingFolderRenamePath.clear();
}

void AssetBrowserPanel::RequestDeleteSelectedFolder() {
    RequestDeleteFolder(m_SelectedFolder);
}

void AssetBrowserPanel::DeleteSelectedFolder() {
    auto* context = GetContext();
    const std::string folderPath = !m_PendingFolderDeletePath.empty() ? m_PendingFolderDeletePath : m_SelectedFolder;
    if (!context || folderPath.empty() || IsRootFolder(folderPath))
        return;
    auto* operators = context->GetOperators();
    if (!operators || !operators->Assets().DeleteFolder(*context, folderPath)) {
        const std::string message = "Failed to delete folder: " + folderPath;
        Logger::Warn("[Editor] ", message);
        SetOperationMessage(message, true);
        return;
    }
    SetOperationMessage("Deleted folder: " + folderPath, false);
    m_PendingFolderDelete = false;
    m_PendingFolderDeletePath.clear();
    m_SelectedFolder = "Content";
    EnsureSelectedFolder();
}

void AssetBrowserPanel::DuplicateSelectedAsset() {
    auto* context = GetContext();
    if (!context)
        return;
    const std::string& path = context->GetSelection().GetAssetPath();
    if (path.empty())
        return;
    auto* operators = context->GetOperators();
    if (!operators || !operators->Assets().DuplicateAsset(*context, path)) {
        const std::string message = "Failed to duplicate asset: " + path;
        Logger::Warn("[Editor] ", message);
        SetOperationMessage(message, true);
        return;
    }
    SetOperationMessage("Duplicated asset: " + std::filesystem::path(path).filename().string(), false);
}

void AssetBrowserPanel::DuplicateSelectedAssets() {
    auto* context = GetContext();
    if (!context)
        return;
    const auto paths = ActiveSelectedAssetPaths(*context);
    if (paths.empty())
        return;
    auto* operators = context->GetOperators();
    size_t duplicatedCount = 0;
    for (const std::string& path : paths) {
        if (operators && operators->Assets().DuplicateAsset(*context, path)) {
            ++duplicatedCount;
        }
    }
    m_SelectedAssetPaths.clear();
    m_LastPrimaryAssetPath = context->GetSelection().HasAsset()
                                 ? NormalizePanelAssetPath(context->GetSelection().GetAssetPath())
                                 : std::string{};
    SetOperationMessage(duplicatedCount > 0 ? "Duplicated " + std::to_string(duplicatedCount) + " asset(s)"
                                            : "Failed to duplicate selected asset(s)",
                        duplicatedCount == 0);
}

void AssetBrowserPanel::ReimportSelectedAssets() {
    auto* context = GetContext();
    if (!context)
        return;
    auto* operators = context->GetOperators();
    auto* registry = context->GetAssetRegistry();
    if (!operators || !registry) {
        SetOperationMessage("Failed to reimport selected asset(s)", true);
        return;
    }

    const auto paths = ActiveSelectedAssetPaths(*context);
    if (paths.empty())
        return;
    size_t reimportedCount = 0;
    size_t skippedCount = 0;
    for (const std::string& path : paths) {
        const EditorAssetInfo* info = registry->GetAssetInfo(path);
        if (!info || !info->imported || info->uuid.empty()) {
            ++skippedCount;
            continue;
        }
        if (operators->Assets().Reimport(*context, info->uuid)) {
            ++reimportedCount;
        }
    }
    EnsureSelectedFolder();
    if (reimportedCount > 0) {
        std::string message = paths.size() == 1 && skippedCount == 0
                                  ? "Reimported asset: " + std::filesystem::path(paths.front()).filename().string()
                                  : "Reimported " + std::to_string(reimportedCount) + " asset(s)";
        if (skippedCount > 0) {
            message += "; skipped " + std::to_string(skippedCount) + " non-imported asset(s)";
        }
        SetOperationMessage(std::move(message), false);
    } else {
        SetOperationMessage(skippedCount > 0 ? "No imported assets selected for reimport"
                                             : "Failed to reimport selected asset(s)",
                            true);
    }
}

bool AssetBrowserPanel::MoveSelectedAssetsToFolder(EditorContext& context, const std::string& targetFolderPath) {
    auto* operators = context.GetOperators();
    const std::filesystem::path rawTargetFolder = std::filesystem::path(targetFolderPath).lexically_normal();
    const std::filesystem::path targetFolder =
        rawTargetFolder.is_absolute() ? rawTargetFolder : FolderPathToAbsolute(targetFolderPath, "").lexically_normal();
    const auto selectedPaths = ActiveSelectedAssetPaths(context);
    if (!operators || selectedPaths.empty() || targetFolder.empty()) {
        return false;
    }

    size_t movedCount = 0;
    size_t skippedCount = 0;
    size_t movedReferenceCount = 0;
    size_t movedProjectReferenceCount = 0;
    std::vector<PendingAssetRetarget> pendingRetargets;
    for (const std::string& path : selectedPaths) {
        const std::filesystem::path selectedPath = std::filesystem::path(path).lexically_normal();
        if (selectedPath.parent_path().lexically_normal() == targetFolder) {
            ++skippedCount;
            continue;
        }
        const std::filesystem::path newPath = (targetFolder / selectedPath.filename()).lexically_normal();
        const size_t referenceCount = CountSceneReferencesForAssets(context, operators, {selectedPath.string()});
        const size_t projectReferenceCount =
            CountProjectSceneReferencesForAssets(context, operators, {selectedPath.string()});
        if (operators->Assets().MoveAsset(context, selectedPath.string(), targetFolder.string())) {
            ++movedCount;
            movedReferenceCount += referenceCount;
            movedProjectReferenceCount += projectReferenceCount;
            if (referenceCount > 0 || projectReferenceCount > 0) {
                pendingRetargets.push_back(
                    {selectedPath.string(), newPath.string(), referenceCount, projectReferenceCount});
            }
        }
    }

    m_SelectedAssetPaths.clear();
    m_LastPrimaryAssetPath = context.GetSelection().HasAsset()
                                 ? NormalizePanelAssetPath(context.GetSelection().GetAssetPath())
                                 : std::string{};
    std::string message = movedCount > 0 ? "Moved " + std::to_string(movedCount) +
                                               " asset(s) into folder: " + targetFolder.filename().string()
                                         : "Failed to move asset(s) into folder: " + targetFolder.string();
    if (movedCount > 0 && skippedCount > 0) {
        message += " (" + std::to_string(skippedCount) + " already in target folder)";
    }
    if (movedCount > 0) {
        message = AppendRetargetReferenceWarning(std::move(message), movedReferenceCount, movedProjectReferenceCount);
    }
    SetOperationMessage(std::move(message), movedCount == 0);
    if (movedCount > 0) {
        SetPendingRetargets(std::move(pendingRetargets));
    }
    return movedCount > 0;
}

void AssetBrowserPanel::RenameSelectedAsset() {
    auto* context = GetContext();
    if (!context)
        return;
    const std::string& path = context->GetSelection().GetAssetPath();
    if (path.empty()) {
        m_PendingRename = false;
        return;
    }
    std::string newName(m_RenameBuffer);
    if (newName.empty()) {
        m_PendingRename = false;
        return;
    }
    namespace fs = std::filesystem;
    fs::path src(path);
    fs::path dst = src.parent_path() / newName;
    if (src == dst) {
        m_PendingRename = false;
        return;
    }
    auto* operators = context->GetOperators();
    const size_t referenceCount = CountSceneReferencesForAssets(*context, operators, {path});
    const size_t projectReferenceCount = CountProjectSceneReferencesForAssets(*context, operators, {path});
    if (!operators || !operators->Assets().RenameAsset(*context, path, dst.string())) {
        const std::string message = "Failed to rename asset: " + path;
        Logger::Warn("[Editor] ", message);
        SetOperationMessage(message, true);
    } else {
        SetOperationMessage(
            AppendRetargetReferenceWarning("Renamed asset to: " + newName, referenceCount, projectReferenceCount),
            false);
        if (referenceCount > 0 || projectReferenceCount > 0) {
            SetPendingRetargets({{src.string(), dst.string(), referenceCount, projectReferenceCount}});
        }
    }
    m_PendingRename = false;
}

void AssetBrowserPanel::RenameSelectedFolder() {
    auto* context = GetContext();
    if (!context)
        return;
    if (m_PendingFolderRenamePath.empty()) {
        m_PendingFolderRename = false;
        return;
    }
    std::string newName(m_FolderRenameBuffer);
    if (newName.empty() || IsRootFolder(m_PendingFolderRenamePath)) {
        m_PendingFolderRename = false;
        return;
    }
    auto* operators = context->GetOperators();
    const std::filesystem::path renamed = std::filesystem::path(m_PendingFolderRenamePath).parent_path() / newName;
    auto pendingRetargets =
        BuildPendingRetargetsForFolder(*context, operators, m_PendingFolderRenamePath, renamed.generic_string());
    size_t referenceCount = 0;
    size_t projectReferenceCount = 0;
    for (const auto& retarget : pendingRetargets) {
        referenceCount += retarget.referenceCount;
        projectReferenceCount += retarget.projectReferenceCount;
    }
    if (!operators || !operators->Assets().RenameFolder(*context, m_PendingFolderRenamePath, newName)) {
        const std::string message = "Failed to rename folder: " + m_PendingFolderRenamePath;
        Logger::Warn("[Editor] ", message);
        SetOperationMessage(message, true);
    } else {
        m_SelectedFolder = renamed.generic_string();
        SetOperationMessage(
            AppendRetargetReferenceWarning("Renamed folder to: " + newName, referenceCount, projectReferenceCount),
            false);
        if (referenceCount > 0 || projectReferenceCount > 0) {
            SetPendingRetargets(std::move(pendingRetargets));
        }
    }
    m_PendingFolderRename = false;
    m_PendingFolderRenamePath.clear();
    EnsureSelectedFolder();
}

bool AssetBrowserPanel::StartRenameSelectedAsset() {
    auto* context = GetContext();
    if (!context)
        return false;
    const std::string& path = context->GetSelection().GetAssetPath();
    if (path.empty())
        return false;
    if (const std::string assetFolder = AssetParentFolderForBrowser(*context, path); !assetFolder.empty()) {
        m_SelectedFolder = assetFolder;
        m_RecursiveAssets = true;
        m_Filter[0] = '\0';
        m_TypeFilter = 0;
        m_ImportStateFilter = 0;
        m_DiagnosticsOnly = false;
        SaveWorkspaceState();
    }
    m_PendingRename = true;
    m_PendingFolderRename = false;
    m_PendingDelete = false;
    m_PendingFolderDelete = false;
    m_PendingFolderDeletePath.clear();
    const std::string filename = std::filesystem::path(path).filename().string();
    std::strncpy(m_RenameBuffer, filename.c_str(), sizeof(m_RenameBuffer) - 1);
    m_RenameBuffer[sizeof(m_RenameBuffer) - 1] = '\0';
    return true;
}

void AssetBrowserPanel::StartRenameFolder(const std::string& folderPath) {
    if (folderPath.empty() || IsRootFolder(folderPath))
        return;
    m_PendingFolderRename = true;
    m_PendingRename = false;
    m_PendingDelete = false;
    m_PendingFolderDelete = false;
    m_PendingFolderDeletePath.clear();
    m_PendingFolderRenamePath = folderPath;
    const std::string filename = std::filesystem::path(folderPath).filename().string();
    std::strncpy(m_FolderRenameBuffer, filename.c_str(), sizeof(m_FolderRenameBuffer) - 1);
    m_FolderRenameBuffer[sizeof(m_FolderRenameBuffer) - 1] = '\0';
}

void AssetBrowserPanel::SelectAssetRow(EditorContext& context, const std::vector<EditorAssetInfo>& visibleAssets,
                                       size_t index, bool toggle, bool range) {
    if (index >= visibleAssets.size())
        return;
    const std::string path = NormalizePanelAssetPath(visibleAssets[index].absolutePath.string());
    auto* operators = context.GetOperators();

    if (range && !m_LastPrimaryAssetPath.empty()) {
        auto findVisible = [&](const std::string& value) {
            return std::find_if(visibleAssets.begin(), visibleAssets.end(), [&](const EditorAssetInfo& asset) {
                return NormalizePanelAssetPath(asset.absolutePath.string()) == value;
            });
        };
        auto anchor = findVisible(m_LastPrimaryAssetPath);
        if (anchor != visibleAssets.end()) {
            const size_t anchorIndex = static_cast<size_t>(std::distance(visibleAssets.begin(), anchor));
            const size_t first = std::min(anchorIndex, index);
            const size_t last = std::max(anchorIndex, index);
            m_SelectedAssetPaths.clear();
            for (size_t row = first; row <= last; ++row) {
                m_SelectedAssetPaths.push_back(NormalizePanelAssetPath(visibleAssets[row].absolutePath.string()));
            }
            if (operators)
                operators->Selection().SelectAsset(context, path);
            else
                context.GetSelection().SelectAssetPath(path);
            m_LastPrimaryAssetPath = path;
            return;
        }
    }

    if (toggle) {
        auto selected = std::find(m_SelectedAssetPaths.begin(), m_SelectedAssetPaths.end(), path);
        if (selected != m_SelectedAssetPaths.end()) {
            m_SelectedAssetPaths.erase(selected);
            if (m_SelectedAssetPaths.empty()) {
                if (operators)
                    operators->Selection().Clear(context);
                else
                    context.GetSelection().Clear();
                m_LastPrimaryAssetPath.clear();
                return;
            }
            const std::string next = m_SelectedAssetPaths.back();
            if (operators)
                operators->Selection().SelectAsset(context, next);
            else
                context.GetSelection().SelectAssetPath(next);
            m_LastPrimaryAssetPath = next;
            return;
        }
        m_SelectedAssetPaths.push_back(path);
        if (operators)
            operators->Selection().SelectAsset(context, path);
        else
            context.GetSelection().SelectAssetPath(path);
        m_LastPrimaryAssetPath = path;
        return;
    }

    m_SelectedAssetPaths.clear();
    m_SelectedAssetPaths.push_back(path);
    if (operators)
        operators->Selection().SelectAsset(context, path);
    else
        context.GetSelection().SelectAssetPath(path);
    m_LastPrimaryAssetPath = path;
}

void AssetBrowserPanel::SelectVisibleAssets(EditorContext& context, const std::vector<EditorAssetInfo>& visibleAssets) {
    m_SelectedAssetPaths.clear();
    if (visibleAssets.empty())
        return;
    m_SelectedAssetPaths.reserve(visibleAssets.size());
    for (const EditorAssetInfo& asset : visibleAssets) {
        m_SelectedAssetPaths.push_back(NormalizePanelAssetPath(asset.absolutePath.string()));
    }
    const std::string primary = m_SelectedAssetPaths.front();
    if (auto* operators = context.GetOperators()) {
        operators->Selection().SelectAsset(context, primary);
    } else {
        context.GetSelection().SelectAssetPath(primary);
    }
    m_LastPrimaryAssetPath = primary;
}

bool AssetBrowserPanel::IsAssetSelected(const std::string& path) const {
    const std::string normalized = NormalizePanelAssetPath(path);
    return std::find(m_SelectedAssetPaths.begin(), m_SelectedAssetPaths.end(), normalized) !=
           m_SelectedAssetPaths.end();
}

std::vector<std::string> AssetBrowserPanel::ActiveSelectedAssetPaths(const EditorContext& context) const {
    if (!context.GetSelection().HasAsset())
        return {};
    const std::string primary = NormalizePanelAssetPath(context.GetSelection().GetAssetPath());
    if (primary.empty())
        return {};
    if (m_SelectedAssetPaths.empty())
        return {primary};
    if (std::find(m_SelectedAssetPaths.begin(), m_SelectedAssetPaths.end(), primary) == m_SelectedAssetPaths.end()) {
        return {primary};
    }
    return m_SelectedAssetPaths;
}

void AssetBrowserPanel::SyncAssetSelectionFromContext(const EditorContext& context) {
    if (!context.GetSelection().HasAsset()) {
        m_SelectedAssetPaths.clear();
        m_LastPrimaryAssetPath.clear();
        return;
    }
    const std::string primary = NormalizePanelAssetPath(context.GetSelection().GetAssetPath());
    if (m_LastPrimaryAssetPath.empty()) {
        m_LastPrimaryAssetPath = primary;
        if (m_SelectedAssetPaths.empty())
            m_SelectedAssetPaths.push_back(primary);
        return;
    }
    if (primary != m_LastPrimaryAssetPath) {
        m_SelectedAssetPaths.clear();
        m_SelectedAssetPaths.push_back(primary);
        m_LastPrimaryAssetPath = primary;
    }
}

bool AssetBrowserPanel::RequestCreateFolderInFolder(EditorContext& context, const std::string& parentFolder,
                                                    bool selectCreated) {
    if (parentFolder.empty())
        return false;
    const std::filesystem::path folderPath = FolderPathToAbsolute(parentFolder, "") / "New Folder";
    auto* operators = context.GetOperators();
    const bool created = operators && operators->Assets().CreateFolder(context, folderPath.string());
    if (created && selectCreated) {
        m_SelectedFolder =
            (std::filesystem::path(parentFolder) / folderPath.filename()).lexically_normal().generic_string();
        EnsureSelectedFolder();
        SaveWorkspaceState();
    } else if (created) {
        EnsureSelectedFolder();
    } else {
        EnsureSelectedFolder();
    }
    SetOperationMessage(created ? "Created folder: " + folderPath.filename().string()
                                : "Failed to create folder: " + folderPath.string(),
                        !created);
    return created;
}

bool AssetBrowserPanel::RequestCreateAssetFromTemplateInFolder(EditorContext& context, const std::string& folderPath,
                                                               const char* templateID) {
    if (folderPath.empty() || !templateID || templateID[0] == '\0')
        return false;
    auto* operators = context.GetOperators();
    const std::filesystem::path targetPath = TemplateTargetDirectoryInFolder(folderPath, templateID);
    const bool created =
        operators && operators->Assets().CreateAssetFromTemplate(context, targetPath.string(), templateID);
    EnsureSelectedFolder();
    SetOperationMessage(created ? "Created asset from template: " + targetPath.filename().string()
                                : "Failed to create asset from template: " + targetPath.string(),
                        !created);
    return created;
}

bool AssetBrowserPanel::RequestPasteAssetsToFolder(EditorContext& context, const std::string& targetFolder) {
    if (targetFolder.empty())
        return false;
    auto* operators = context.GetOperators();
    const std::filesystem::path rawTargetFolder = std::filesystem::path(targetFolder).lexically_normal();
    const std::filesystem::path resolvedTargetFolder =
        rawTargetFolder.is_absolute() ? rawTargetFolder : FolderPathToAbsolute(targetFolder, "").lexically_normal();
    const bool pasted = operators && operators->Commands().PasteAssetToFolder(context, resolvedTargetFolder.string());
    SetOperationMessage(pasted ? "Pasted asset into folder: " + resolvedTargetFolder.string()
                               : "Failed to paste asset into folder: " + resolvedTargetFolder.string(),
                        !pasted);
    return pasted;
}

bool AssetBrowserPanel::RequestOpenAsset(const std::string& path) {
    auto* context = GetContext();
    if (!context || path.empty())
        return false;
    if (EditorAssetRegistry::Classify(path) == EditorAssetType::Scene) {
        if (SceneRenderLayer* layer = context->GetSceneLayer(); layer && layer->IsDirty()) {
            m_PendingSceneOpenPath = path;
            m_PendingSceneOpenPopup = true;
            return true;
        }
    }
    auto* operators = context->GetOperators();
    const bool opened = operators && operators->Assets().OpenAsset(*context, path);
    SetOperationMessage(opened ? "Opened asset: " + std::filesystem::path(path).filename().string()
                               : "Failed to open asset: " + path,
                        !opened);
    return opened;
}

bool AssetBrowserPanel::RequestOpenFolder(const std::string& folderPath) {
    if (folderPath.empty())
        return false;
    m_SelectedFolder = folderPath;
    EnsureSelectedFolder();
    SaveWorkspaceState();
    SetOperationMessage("Opened folder: " + m_SelectedFolder, false);
    return true;
}

bool AssetBrowserPanel::RequestRevealPath(const std::string& path) {
    auto* context = GetContext();
    if (!context || path.empty())
        return false;
    auto* operators = context->GetOperators();
    const bool revealed = operators && operators->Assets().RevealAsset(*context, path);
    SetOperationMessage(revealed ? "Revealed in Explorer: " + std::filesystem::path(path).filename().string()
                                 : "Failed to reveal in Explorer: " + path,
                        !revealed);
    return revealed;
}

bool AssetBrowserPanel::RequestValidateAssets() {
    auto* context = GetContext();
    if (!context)
        return false;
    auto* operators = context->GetOperators();
    const bool requested = operators && operators->Commands().ExecuteAction(*context, "asset.validate");
    EnsureSelectedFolder();
    std::string message = "Failed to validate assets.";
    if (requested) {
        if (auto* imports = context->GetService<EditorImportService>()) {
            SetValidationSummary(imports->GetValidationSummaryText(), imports->HasValidationIssues());
            message = "Validated assets; diagnostics are refreshed.";
        } else {
            ClearValidationSummary();
            message = "Validated assets; diagnostics are refreshed.";
        }
    } else {
        ClearValidationSummary();
    }
    SetOperationMessage(message, !requested);
    return requested;
}

bool AssetBrowserPanel::OpenPendingSceneAsset(bool discardUnsavedChanges) {
    auto* context = GetContext();
    if (!context || m_PendingSceneOpenPath.empty())
        return false;
    SceneRenderLayer* layer = context->GetSceneLayer();
    if (!layer)
        return false;
    if (discardUnsavedChanges)
        layer->ClearDirty();
    auto* operators = context->GetOperators();
    const bool opened = operators && operators->Assets().OpenAsset(*context, m_PendingSceneOpenPath);
    if (!opened && discardUnsavedChanges)
        layer->MarkDirty();
    if (opened) {
        SetOperationMessage("Opened asset: " + std::filesystem::path(m_PendingSceneOpenPath).filename().string(),
                            false);
        m_PendingSceneOpenPath.clear();
        m_PendingSceneOpenPopup = false;
    } else {
        SetOperationMessage("Failed to open asset: " + m_PendingSceneOpenPath, true);
    }
    return opened;
}

void AssetBrowserPanel::DrawPendingSceneOpenModal() {
#if defined(MYENGINE_ENABLE_IMGUI)
    auto* context = GetContext();
    if (!context || m_PendingSceneOpenPath.empty())
        return;
    SceneRenderLayer* layer = context->GetSceneLayer();
    if (!layer)
        return;

    if (!layer->IsDirty()) {
        OpenPendingSceneAsset(false);
        return;
    }

    if (m_PendingSceneOpenPopup) {
        ImGui::OpenPopup("Unsaved Scene Changes###asset_open_dirty_scene");
        m_PendingSceneOpenPopup = false;
    }

    Editor::UI::EditorViewportPolicy::BindNextModalToMainViewport();
    if (!ImGui::BeginPopupModal("Unsaved Scene Changes###asset_open_dirty_scene", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextUnformatted("The current scene has unsaved changes.");
    ImGui::TextWrapped("Save or discard those changes before opening:");
    ImGui::TextDisabled("%s", m_PendingSceneOpenPath.c_str());
    ImGui::Separator();

    if (ImGui::Button("Save")) {
        if (layer->HasFilePath()) {
            if (layer->SaveScene())
                OpenPendingSceneAsset(false);
        } else if (auto* dialogs = context->GetService<EditorDialogService>()) {
            dialogs->RequestSaveScene(context->GetWindow());
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard")) {
        OpenPendingSceneAsset(true);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        m_PendingSceneOpenPath.clear();
        m_PendingSceneOpenPopup = false;
        ImGui::CloseCurrentPopup();
    }

    if (!layer->HasFilePath() && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Cancel the save dialog to keep the scene open.");
    }

    ImGui::EndPopup();
#endif
}

void AssetBrowserPanel::SetOperationMessage(std::string message, bool error) {
    m_OperationMessage = std::move(message);
    m_OperationMessageIsError = error;
    ClearPendingRetargets();
    m_PendingDelete = false;
    m_PendingFolderDelete = false;
    m_PendingFolderDeletePath.clear();
}

void AssetBrowserPanel::ClearOperationMessage() {
    m_OperationMessage.clear();
    m_OperationMessageIsError = false;
    ClearPendingRetargets();
    m_PendingDelete = false;
    m_PendingFolderDelete = false;
    m_PendingFolderDeletePath.clear();
}

std::vector<AssetBrowserPanel::PendingAssetRetarget>
AssetBrowserPanel::BuildPendingRetargetsForFolder(EditorContext& context, EditorOperators* operators,
                                                  const std::string& oldFolder, const std::string& newFolder) const {
    std::vector<PendingAssetRetarget> retargets;
    auto* registry = context.GetAssetRegistry();
    if (!operators || !registry || oldFolder.empty() || newFolder.empty() || oldFolder == newFolder) {
        return retargets;
    }

    const std::filesystem::path oldRoot = std::filesystem::path(oldFolder).lexically_normal();
    const std::filesystem::path newRoot = std::filesystem::path(newFolder).lexically_normal();
    for (const auto& asset : registry->GetAssetsInFolder(oldRoot.generic_string(), true, EditorAssetType::Unknown)) {
        const std::filesystem::path oldPath = std::filesystem::path(asset.relativePath).lexically_normal();
        std::filesystem::path suffix = oldPath.lexically_relative(oldRoot);
        if (suffix.empty() || suffix.generic_string().find("..") != std::string::npos) {
            suffix = oldPath.filename();
        }
        const std::filesystem::path newPath = (newRoot / suffix).lexically_normal();
        const size_t referenceCount = operators->Assets().FindSceneReferences(context, oldPath.generic_string()).size();
        const size_t projectReferenceCount =
            operators->Assets().FindProjectSceneReferences(context, oldPath.generic_string()).size();
        if (referenceCount > 0 || projectReferenceCount > 0) {
            retargets.push_back(
                {oldPath.generic_string(), newPath.generic_string(), referenceCount, projectReferenceCount});
        }
    }
    return retargets;
}

void AssetBrowserPanel::SetPendingRetargets(std::vector<PendingAssetRetarget> retargets) {
    m_PendingAssetRetargets.clear();
    for (auto& retarget : retargets) {
        if (retarget.oldPath.empty() || retarget.newPath.empty() || retarget.oldPath == retarget.newPath ||
            (retarget.referenceCount == 0 && retarget.projectReferenceCount == 0)) {
            continue;
        }
        m_PendingAssetRetargets.push_back(std::move(retarget));
    }
}

void AssetBrowserPanel::ClearPendingRetargets() {
    m_PendingAssetRetargets.clear();
}

bool AssetBrowserPanel::ExecutePendingRetargets() {
    auto* context = GetContext();
    auto* operators = context ? context->GetOperators() : nullptr;
    if (!context || !operators || m_PendingAssetRetargets.empty())
        return false;

    const auto retargets = m_PendingAssetRetargets;
    size_t currentSceneChangedCount = 0;
    size_t projectSceneChangedCount = 0;
    size_t requestedCount = 0;
    for (const auto& retarget : retargets) {
        requestedCount += retarget.referenceCount + retarget.projectReferenceCount;
        if (retarget.referenceCount > 0) {
            currentSceneChangedCount +=
                operators->Assets().RetargetSceneReferences(*context, retarget.oldPath, retarget.newPath);
        }
        if (retarget.projectReferenceCount > 0) {
            projectSceneChangedCount +=
                operators->Assets().RetargetProjectSceneReferences(*context, retarget.oldPath, retarget.newPath);
        }
    }
    const size_t changedCount = currentSceneChangedCount + projectSceneChangedCount;

    SetOperationMessage(changedCount > 0 ? "Retargeted " + std::to_string(currentSceneChangedCount) +
                                               " current scene reference(s) and " +
                                               std::to_string(projectSceneChangedCount) + " project scene reference(s)"
                                         : "No scene references were retargeted",
                        changedCount == 0 && requestedCount > 0);
    ClearPendingRetargets();
    return changedCount > 0;
}

void AssetBrowserPanel::SetValidationSummary(std::string message, bool error) {
    m_ValidationSummaryMessage = std::move(message);
    m_ValidationSummaryIsError = error;
}

void AssetBrowserPanel::ClearValidationSummary() {
    m_ValidationSummaryMessage.clear();
    m_ValidationSummaryIsError = false;
}

void AssetBrowserPanel::EnsureSelectedFolder() {
    auto* context = GetContext();
    auto* registry = context ? context->GetAssetRegistry() : nullptr;
    if (!registry)
        return;
    for (const auto& folder : registry->GetFolders()) {
        if (folder.relativePath == m_SelectedFolder)
            return;
    }
    m_SelectedFolder = "Content";
}

void AssetBrowserPanel::LoadWorkspaceState() {
    auto* context = GetContext();
    auto* workspace = context ? context->GetWorkspace() : nullptr;
    if (!workspace)
        return;
    constexpr size_t kFilterLimit = sizeof(m_Filter) - 1;
    if (const auto value = workspace->GetPanelStateValue(GetID(), "filter")) {
        std::strncpy(m_Filter, value->c_str(), kFilterLimit);
        m_Filter[kFilterLimit] = '\0';
    }
    if (const auto value = workspace->GetPanelStateValue(GetID(), "selectedFolder")) {
        m_SelectedFolder = *value;
    }
    m_TypeFilter = std::clamp(ParseWorkspaceInt(*workspace, GetID(), "typeFilter", m_TypeFilter), 0, 11);
    m_ImportStateFilter =
        std::clamp(ParseWorkspaceInt(*workspace, GetID(), "importStateFilter", m_ImportStateFilter), 0, 7);
    m_RecursiveAssets = ParseWorkspaceBool(*workspace, GetID(), "recursive", m_RecursiveAssets);
    m_DiagnosticsOnly = ParseWorkspaceBool(*workspace, GetID(), "diagnosticsOnly", m_DiagnosticsOnly);
}

void AssetBrowserPanel::SaveWorkspaceState() const {
    auto* context = GetContext();
    auto* workspace = context ? context->GetWorkspace() : nullptr;
    if (!workspace)
        return;
    workspace->SetPanelStateValue(GetID(), "filter", m_Filter);
    workspace->SetPanelStateValue(GetID(), "selectedFolder", m_SelectedFolder);
    workspace->SetPanelStateValue(GetID(), "typeFilter", std::to_string(m_TypeFilter));
    workspace->SetPanelStateValue(GetID(), "importStateFilter", std::to_string(m_ImportStateFilter));
    workspace->SetPanelStateValue(GetID(), "recursive", m_RecursiveAssets ? "true" : "false");
    workspace->SetPanelStateValue(GetID(), "diagnosticsOnly", m_DiagnosticsOnly ? "true" : "false");
}

std::filesystem::path AssetBrowserPanel::FolderPathToAbsolute(const std::string& folderPath,
                                                              const char* fallback) const {
    auto* context = GetContext();
    if (!context)
        return {};
    const auto projectRoot = context->GetContentRoot().parent_path();
    if (StartsWithPath(folderPath, "Content")) {
        const std::string suffix =
            folderPath == "Content" ? std::string{} : folderPath.substr(std::string("Content/").size());
        return suffix.empty() ? context->GetContentRoot() : context->GetContentRoot() / std::filesystem::path(suffix);
    }
    if (StartsWithPath(folderPath, "SourceAssets")) {
        const std::string suffix =
            folderPath == "SourceAssets" ? std::string{} : folderPath.substr(std::string("SourceAssets/").size());
        return suffix.empty() ? projectRoot / "SourceAssets"
                              : projectRoot / "SourceAssets" / std::filesystem::path(suffix);
    }
    return context->GetContentRoot() / fallback;
}

std::filesystem::path AssetBrowserPanel::CurrentContentDirectory(const char* fallback) const {
    return FolderPathToAbsolute(m_SelectedFolder, fallback);
}

std::filesystem::path AssetBrowserPanel::TemplateTargetDirectoryInFolder(const std::string& folderPath,
                                                                         const char* templateID) const {
    auto* context = GetContext();
    if (!context)
        return {};

    const char* fallback = TemplateFallbackFolder(templateID);
    if (fallback && fallback[0] && folderPath == "Content") {
        return context->GetContentRoot() / fallback;
    }
    return FolderPathToAbsolute(folderPath, fallback);
}

std::filesystem::path AssetBrowserPanel::TemplateTargetDirectory(const char* templateID) const {
    return TemplateTargetDirectoryInFolder(m_SelectedFolder, templateID);
}

void AssetBrowserPanel::DrawContent() {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (TryDrawScriptedBody("assetBrowser"))
        return;

    using namespace EditorPanelHelpers;
    auto* context = GetContext();
    auto* registry = context ? context->GetAssetRegistry() : nullptr;
    if (!registry)
        return;
    const uint32_t assetBrowserViewportID = Editor::UI::EditorViewportPolicy::GetCurrentViewportID();
    EnsureSelectedFolder();

    // Toolbar: Refresh | Import | Create Material / Texture / Script
    auto* operators = context->GetOperators();
    auto refreshAssets = [&]() {
        if (operators)
            return operators->Assets().Refresh(*context);
        EditorAssetOperator assetOperator;
        return assetOperator.Refresh(*context);
    };
    if (EditorWidgets::IconButton(*context, "RefreshAssets", EditorIcons::Refresh, "Refresh")) {
        refreshAssets();
        EnsureSelectedFolder();
        SetOperationMessage("Refreshed asset registry", false);
    }
    ImGui::SameLine();
    if (EditorWidgets::IconButton(*context, "ImportAsset", EditorIcons::Asset, "Import")) {
        if (auto* dialogs = context->GetService<EditorDialogService>())
            dialogs->RequestImportAsset(context->GetWindow());
    }
    ImGui::SameLine();
    if (EditorWidgets::IconButton(*context, "ReimportAllAssets", EditorIcons::Refresh, "Reimport All")) {
        std::vector<std::string> failures;
        const bool reimported = operators && operators->Assets().ReimportAll(*context, &failures);
        if (!failures.empty()) {
            Logger::Warn("[Editor] Reimport failures: ", failures.front());
            SetOperationMessage("Reimport completed with failures: " + failures.front(), true);
        } else {
            SetOperationMessage(reimported ? "Reimported all assets" : "Failed to reimport assets", !reimported);
        }
        EnsureSelectedFolder();
    }
    ImGui::SameLine();
    if (EditorWidgets::IconButton(*context, "ValidateAssets", EditorIcons::Asset, "Validate Assets")) {
        RequestValidateAssets();
    }
    ImGui::SameLine();

    Editor::UI::EditorViewportPolicy::BindNextPopupToViewport(assetBrowserViewportID);
    if (ImGui::BeginCombo("##CreateAsset", "Create...")) {
        auto createFolderFromToolbar = [&]() { RequestCreateFolderInFolder(*context, m_SelectedFolder, false); };
        auto createTemplateFromToolbar = [&](const char* templateID) {
            RequestCreateAssetFromTemplateInFolder(*context, m_SelectedFolder, templateID);
        };
        if (ImGui::Selectable("Folder")) {
            createFolderFromToolbar();
        }
        ImGui::Separator();
        if (ImGui::Selectable("Material")) {
            createTemplateFromToolbar("material");
        }
        if (ImGui::Selectable("Default Texture")) {
            createTemplateFromToolbar("texture");
        }
        if (ImGui::Selectable("Prefab")) {
            createTemplateFromToolbar("prefab");
        }
        if (ImGui::Selectable("AngelScript")) {
            createTemplateFromToolbar("as");
        }
        if (ImGui::Selectable("Lua Script")) {
            createTemplateFromToolbar("lua");
        }
        if (ImGui::Selectable("Shader")) {
            createTemplateFromToolbar("shader");
        }
        if (ImGui::Selectable("UI Document")) {
            createTemplateFromToolbar("ui");
        }
        if (ImGui::Selectable("Scene")) {
            createTemplateFromToolbar("scene");
        }
        ImGui::EndCombo();
    }

    if (!m_OperationMessage.empty()) {
        EditorWidgets::InlineMessage(m_OperationMessageIsError ? Editor::UI::EditorNotificationType::Error
                                                               : Editor::UI::EditorNotificationType::Info,
                                     m_OperationMessage.c_str());
        if (!m_PendingAssetRetargets.empty()) {
            size_t referenceCount = 0;
            size_t projectReferenceCount = 0;
            for (const auto& retarget : m_PendingAssetRetargets) {
                referenceCount += retarget.referenceCount;
                projectReferenceCount += retarget.projectReferenceCount;
            }
            ImGui::BeginDisabled(!operators);
            if (ImGui::SmallButton("Retarget References")) {
                ExecutePendingRetargets();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("%zu current / %zu project scene reference(s)", referenceCount, projectReferenceCount);
            ImGui::SameLine();
            if (ImGui::SmallButton("Dismiss Retarget")) {
                ClearPendingRetargets();
            }
        }
    }
    if (m_PendingDelete) {
        const auto selectedForDelete = ActiveSelectedAssetPaths(*context);
        if (selectedForDelete.empty()) {
            m_PendingDelete = false;
        } else {
            const std::string deleteMessage =
                "Delete " + std::to_string(selectedForDelete.size()) + " selected asset(s)?";
            EditorWidgets::InlineMessage(Editor::UI::EditorNotificationType::Warning, deleteMessage.c_str());
            DrawDeleteReferenceWarning(selectedForDelete);
            ImGui::BeginDisabled(!operators);
            if (ImGui::SmallButton("Delete Assets")) {
                DeleteSelectedAssets();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::SmallButton("Cancel Asset Delete")) {
                m_PendingDelete = false;
            }
        }
    }
    if (m_PendingFolderDelete && !m_PendingFolderDeletePath.empty()) {
        const std::string folderDeleteMessage =
            "Delete folder and all contained assets: " + m_PendingFolderDeletePath + "?";
        EditorWidgets::InlineMessage(Editor::UI::EditorNotificationType::Warning, folderDeleteMessage.c_str());
        const auto folderDeletePaths = CollectAssetPathsForFolder(*context, m_PendingFolderDeletePath);
        DrawDeleteReferenceWarning(folderDeletePaths);
        ImGui::BeginDisabled(!operators || IsRootFolder(m_PendingFolderDeletePath));
        if (ImGui::SmallButton("Delete Folder")) {
            m_SelectedFolder = m_PendingFolderDeletePath;
            DeleteSelectedFolder();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::SmallButton("Cancel Folder Delete")) {
            m_PendingFolderDelete = false;
            m_PendingFolderDeletePath.clear();
        }
    }
    if (!m_ValidationSummaryMessage.empty()) {
        EditorWidgets::InlineMessage(m_ValidationSummaryIsError ? Editor::UI::EditorNotificationType::Warning
                                                                : Editor::UI::EditorNotificationType::Info,
                                     m_ValidationSummaryMessage.c_str());
    }

    EditorWidgets::SvgIcon(*context, EditorIcons::Search, 14.0f);
    ImGui::SameLine();
    ImGui::InputTextWithHint("##Filter", "Filter...", m_Filter, sizeof(m_Filter));
    ImGui::SameLine();
    const char* typeItems[] = {"All Types", "Model",  "Texture", "Material", "Scene",    "Prefab",
                               "Script",    "Shader", "Audio",   "UI",       "Particle", "Navigation"};
    ImGui::SetNextItemWidth(120.0f);
    ImGui::Combo("##AssetTypeFilter", &m_TypeFilter, typeItems, static_cast<int>(std::size(typeItems)));
    ImGui::SameLine();
    const char* importItems[] = {"All States", "Ready",          "Importing", "Failed",
                                 "Stale",      "Missing Source", "Imported",  "Not Imported"};
    ImGui::SetNextItemWidth(140.0f);
    ImGui::Combo("##ImportStateFilter", &m_ImportStateFilter, importItems, static_cast<int>(std::size(importItems)));
    ImGui::SameLine();
    ImGui::Checkbox("Recursive", &m_RecursiveAssets);
    ImGui::SameLine();
    ImGui::Checkbox("Diagnostics", &m_DiagnosticsOnly);
    ImGui::Separator();

    if (ImGui::BeginChild("##AssetFolderTree", ImVec2(220.0f, 0), true)) {
        const auto& folders = registry->GetFolders();
        std::function<void(const EditorAssetFolderInfo&)> drawFolder = [&](const EditorAssetFolderInfo& folder) {
            bool hasChildren = false;
            for (const auto& child : folders) {
                if (IsDirectChildFolder(folder.relativePath, child.relativePath)) {
                    hasChildren = true;
                    break;
                }
            }
            ImGui::PushID(folder.relativePath.c_str());
            EditorWidgets::SvgIcon(*context, EditorIcons::Folder, 14.0f);
            ImGui::SameLine();
            if (m_PendingFolderRename && m_PendingFolderRenamePath == folder.relativePath) {
                ImGui::SetKeyboardFocusHere();
                if (ImGui::InputText("##folderRename", m_FolderRenameBuffer, sizeof(m_FolderRenameBuffer),
                                     ImGuiInputTextFlags_EnterReturnsTrue)) {
                    RenameSelectedFolder();
                }
                if (!ImGui::IsItemActive() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    m_PendingFolderRename = false;
                    m_PendingFolderRenamePath.clear();
                }
                ImGui::PopID();
                return;
            }
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (!hasChildren)
                flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            if (folder.relativePath == m_SelectedFolder)
                flags |= ImGuiTreeNodeFlags_Selected;
            const std::string label = folder.displayName + " (" + std::to_string(folder.assetCount) + ")";
            const bool open = ImGui::TreeNodeEx("##folder", flags, "%s", label.c_str());
            if (ImGui::IsItemClicked())
                m_SelectedFolder = folder.relativePath;
            if (!IsRootFolder(folder.relativePath)) {
                AssetDragDropSource(kFolderPayload, folder.relativePath, folder.displayName).Draw();
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetPayload)) {
                    if (operators) {
                        const std::string source = static_cast<const char*>(payload->Data);
                        const std::filesystem::path targetFolder =
                            FolderPathToAbsolute(folder.relativePath, "").lexically_normal();
                        const size_t referenceCount = CountSceneReferencesForAssets(*context, operators, {source});
                        const size_t projectReferenceCount =
                            CountProjectSceneReferencesForAssets(*context, operators, {source});
                        const std::filesystem::path newPath =
                            (targetFolder / std::filesystem::path(source).filename()).lexically_normal();
                        if (!operators->Assets().MoveAsset(*context, source, targetFolder.string())) {
                            SetOperationMessage("Failed to move asset into folder: " + folder.relativePath, true);
                        } else {
                            SetOperationMessage(
                                AppendRetargetReferenceWarning("Moved asset into folder: " + folder.relativePath,
                                                               referenceCount, projectReferenceCount),
                                false);
                            if (referenceCount > 0 || projectReferenceCount > 0) {
                                SetPendingRetargets(
                                    {{source, newPath.generic_string(), referenceCount, projectReferenceCount}});
                            }
                        }
                        EnsureSelectedFolder();
                    }
                }
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kFolderPayload)) {
                    const std::string source = static_cast<const char*>(payload->Data);
                    if (operators && source != folder.relativePath && !StartsWithPath(folder.relativePath, source)) {
                        const std::filesystem::path newFolder =
                            (std::filesystem::path(folder.relativePath) / std::filesystem::path(source).filename())
                                .lexically_normal();
                        auto pendingRetargets =
                            BuildPendingRetargetsForFolder(*context, operators, source, newFolder.generic_string());
                        size_t referenceCount = 0;
                        size_t projectReferenceCount = 0;
                        for (const auto& retarget : pendingRetargets) {
                            referenceCount += retarget.referenceCount;
                            projectReferenceCount += retarget.projectReferenceCount;
                        }
                        if (!operators->Assets().MoveFolder(*context, source, folder.relativePath)) {
                            SetOperationMessage("Failed to move folder into folder: " + folder.relativePath, true);
                        } else {
                            SetOperationMessage(
                                AppendRetargetReferenceWarning("Moved folder into folder: " + folder.relativePath,
                                                               referenceCount, projectReferenceCount),
                                false);
                            if (referenceCount > 0 || projectReferenceCount > 0) {
                                SetPendingRetargets(std::move(pendingRetargets));
                            }
                        }
                        EnsureSelectedFolder();
                    }
                }
                ImGui::EndDragDropTarget();
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                m_SelectedFolder = folder.relativePath;
                ImGui::OpenPopup("##FolderCtx");
            }
            Editor::UI::EditorViewportPolicy::BindNextPopupToViewport(assetBrowserViewportID);
            if (ImGui::BeginPopup("##FolderCtx")) {
                if (ImGui::Selectable("Refresh")) {
                    refreshAssets();
                    EnsureSelectedFolder();
                    SetOperationMessage("Refreshed asset registry", false);
                }
                if (ImGui::Selectable("Open")) {
                    RequestOpenFolder(folder.relativePath);
                }
                if (ImGui::Selectable("Reveal in Explorer")) {
                    RequestRevealPath(folder.relativePath);
                }
                ImGui::Separator();
                const auto selectedForMove = ActiveSelectedAssetPaths(*context);
                ImGui::BeginDisabled(selectedForMove.empty());
                if (ImGui::Selectable("Move Selected Assets Here")) {
                    MoveSelectedAssetsToFolder(*context, folder.relativePath);
                }
                ImGui::EndDisabled();
                const bool canPasteIntoFolder = operators && operators->Commands().HasAssetClipboard();
                ImGui::BeginDisabled(!canPasteIntoFolder);
                if (ImGui::Selectable("Paste Into Folder")) {
                    RequestPasteAssetsToFolder(*context, folder.relativePath);
                }
                ImGui::EndDisabled();
                if (ImGui::Selectable("New Folder") && operators) {
                    RequestCreateFolderInFolder(*context, folder.relativePath, true);
                }
                Editor::UI::EditorViewportPolicy::BindNextPopupToViewport(assetBrowserViewportID);
                if (ImGui::BeginMenu("Create")) {
                    if (ImGui::MenuItem("Material")) {
                        RequestCreateAssetFromTemplateInFolder(*context, folder.relativePath, "material");
                    }
                    if (ImGui::MenuItem("Default Texture")) {
                        RequestCreateAssetFromTemplateInFolder(*context, folder.relativePath, "texture");
                    }
                    if (ImGui::MenuItem("Prefab")) {
                        RequestCreateAssetFromTemplateInFolder(*context, folder.relativePath, "prefab");
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("AngelScript")) {
                        RequestCreateAssetFromTemplateInFolder(*context, folder.relativePath, "as");
                    }
                    if (ImGui::MenuItem("Lua Script")) {
                        RequestCreateAssetFromTemplateInFolder(*context, folder.relativePath, "lua");
                    }
                    if (ImGui::MenuItem("Shader")) {
                        RequestCreateAssetFromTemplateInFolder(*context, folder.relativePath, "shader");
                    }
                    if (ImGui::MenuItem("UI Document")) {
                        RequestCreateAssetFromTemplateInFolder(*context, folder.relativePath, "ui");
                    }
                    if (ImGui::MenuItem("Scene")) {
                        RequestCreateAssetFromTemplateInFolder(*context, folder.relativePath, "scene");
                    }
                    ImGui::EndMenu();
                }
                const bool rootFolder = IsRootFolder(folder.relativePath);
                ImGui::BeginDisabled(rootFolder);
                if (ImGui::Selectable("Rename Folder")) {
                    StartRenameFolder(folder.relativePath);
                }
                if (ImGui::Selectable("Delete Folder")) {
                    RequestDeleteFolder(folder.relativePath);
                }
                ImGui::EndDisabled();
                ImGui::EndPopup();
            }
            if (hasChildren && open) {
                for (const auto& child : folders) {
                    if (IsDirectChildFolder(folder.relativePath, child.relativePath))
                        drawFolder(child);
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        };
        for (const auto& folder : folders) {
            if (folder.relativePath == "Content" || folder.relativePath == "SourceAssets")
                drawFolder(folder);
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginGroup();

    // Context menu detection for empty area
    bool emptyRightClick = false;
    if (EditorContextMenu::DetectWindow("##AssetCtxMenu"))
        emptyRightClick = true;

    std::vector<EditorAssetInfo> visibleAssets;
    const auto folderAssets =
        registry->GetAssetsInFolder(m_SelectedFolder, m_RecursiveAssets, AssetTypeFromFilterIndex(m_TypeFilter));
    visibleAssets.reserve(folderAssets.size());
    size_t visibleDiagnosticCount = 0;
    for (const auto& asset : folderAssets) {
        if (!AssetMatchesBrowserFilters(asset, m_Filter, m_TypeFilter, m_ImportStateFilter, m_DiagnosticsOnly)) {
            continue;
        }
        visibleDiagnosticCount += asset.diagnostics.size();
        visibleAssets.push_back(asset);
    }
    SyncAssetSelectionFromContext(*context);

    const auto activeAssetSelection = ActiveSelectedAssetPaths(*context);
    if (activeAssetSelection.size() > 1) {
        ImGui::TextDisabled("%zu assets selected", activeAssetSelection.size());
    }

    if (visibleDiagnosticCount > 0) {
        const std::string diagnosticsHeader = "Diagnostics Issues (" + std::to_string(visibleDiagnosticCount) + ")";
        if (ImGui::CollapsingHeader(diagnosticsHeader.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            const float issueListHeight = std::min(160.0f, 26.0f * static_cast<float>(visibleDiagnosticCount + 1));
            if (ImGui::BeginChild("##AssetDiagnosticsIssues", ImVec2(0.0f, issueListHeight), true)) {
                for (const auto& asset : visibleAssets) {
                    if (asset.diagnostics.empty())
                        continue;
                    ImGui::PushID(asset.relativePath.c_str());
                    const bool open = ImGui::TreeNodeEx("AssetDiagnosticsGroup", ImGuiTreeNodeFlags_DefaultOpen, "%s",
                                                        asset.relativePath.c_str());
                    if (open) {
                        for (size_t diagnosticIndex = 0; diagnosticIndex < asset.diagnostics.size();
                             ++diagnosticIndex) {
                            const auto& diagnostic = asset.diagnostics[diagnosticIndex];
                            ImGui::PushID(static_cast<int>(diagnosticIndex));
                            const std::string issueLabel = diagnostic.severity + ": " + diagnostic.message;
                            if (ImGui::Selectable(issueLabel.c_str(), false)) {
                                if (operators) {
                                    operators->Selection().SelectAsset(*context, asset.absolutePath.string());
                                }
                                context->RequestPanelFocus("inspector");
                            }
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("%s", asset.relativePath.c_str());
                            }
                            ImGui::PopID();
                        }
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
            ImGui::Separator();
        }
    }

    for (size_t assetIndex = 0; assetIndex < visibleAssets.size(); ++assetIndex) {
        const auto& asset = visibleAssets[assetIndex];
        const std::string assetPath = NormalizePanelAssetPath(asset.absolutePath.string());
        const bool selected = IsAssetSelected(assetPath);
        const bool primarySelected = context->GetSelection().HasAsset() &&
                                     NormalizePanelAssetPath(context->GetSelection().GetAssetPath()) == assetPath;

        // Rename inline editing
        if (m_PendingRename && primarySelected) {
            ImGui::SetKeyboardFocusHere();
            if (ImGui::InputText("##rename", m_RenameBuffer, sizeof(m_RenameBuffer),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                RenameSelectedAsset();
            }
            if (!ImGui::IsItemActive() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                m_PendingRename = false;
            }
            continue;
        }

        EditorWidgets::SvgIcon(*context, IconForAssetType(asset.type), 14.0f);
        ImGui::SameLine();
        std::string label = asset.relativePath;
        if (asset.imported)
            label += " [" + std::string(ImportStateText(asset.importState)) + "]";
        if (!asset.diagnostics.empty())
            label += " !";
        if (ImGui::Selectable(label.c_str(), selected)) {
            const ImGuiIO& io = ImGui::GetIO();
            SelectAssetRow(*context, visibleAssets, assetIndex, io.KeyCtrl, io.KeyShift);
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            RequestOpenAsset(asset.absolutePath.string());
        }
        if (ImGui::IsItemHovered() && (asset.imported || !asset.diagnostics.empty())) {
            ImGui::BeginTooltip();
            if (asset.imported) {
                ImGui::Text("UUID: %s", asset.uuid.c_str());
                ImGui::Text("State: %s", ImportStateText(asset.importState));
                if (!asset.artifactPath.empty())
                    ImGui::Text("Artifact: %s", asset.artifactPath.string().c_str());
            }
            for (const auto& diagnostic : asset.diagnostics) {
                ImGui::Text("%s: %s", diagnostic.severity.c_str(), diagnostic.message.c_str());
            }
            ImGui::EndTooltip();
        }

        // Right-click context menu per asset
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            if (!IsAssetSelected(assetPath)) {
                SelectAssetRow(*context, visibleAssets, assetIndex, false, false);
            }
            ImGui::OpenPopup("##AssetItemCtx");
        }
        Editor::UI::EditorViewportPolicy::BindNextPopupToViewport(assetBrowserViewportID);
        if (ImGui::BeginPopup("##AssetItemCtx")) {
            if (ImGui::Selectable("Open")) {
                RequestOpenAsset(asset.absolutePath.string());
            }
            if (ImGui::Selectable("Reveal in Explorer")) {
                RequestRevealPath(asset.absolutePath.string());
            }
            ImGui::Separator();
            const auto contextSelection = ActiveSelectedAssetPaths(*context);
            const bool hasMultiSelection = contextSelection.size() > 1;
            const std::string assetFolder =
                std::filesystem::path(asset.relativePath).parent_path().lexically_normal().generic_string();
            if (asset.imported && ImGui::Selectable(hasMultiSelection ? "Reimport Selected" : "Reimport")) {
                ReimportSelectedAssets();
            }
            if (ImGui::Selectable("Copy")) {
                if (operators) {
                    const auto selectedPaths = ActiveSelectedAssetPaths(*context);
                    const bool copied = operators->Commands().CopyAssets(*context, selectedPaths);
                    SetOperationMessage(copied ? (selectedPaths.size() == 1
                                                      ? "Copied asset: " + asset.absolutePath.filename().string()
                                                      : "Copied " + std::to_string(selectedPaths.size()) + " asset(s)")
                                               : "Failed to copy selected asset",
                                        !copied);
                }
            }
            ImGui::BeginDisabled(contextSelection.empty());
            if (ImGui::Selectable("Move Selected Here")) {
                MoveSelectedAssetsToFolder(*context, assetFolder.empty() ? m_SelectedFolder : assetFolder);
            }
            ImGui::EndDisabled();
            if (ImGui::Selectable("Paste Into Folder")) {
                RequestPasteAssetsToFolder(*context, assetFolder.empty() ? m_SelectedFolder : assetFolder);
            }
            if (ImGui::Selectable("Delete")) {
                RequestDeleteSelectedAssets();
            }
            if (ImGui::Selectable("Duplicate")) {
                DuplicateSelectedAssets();
            }
            if (ImGui::Selectable("Rename")) {
                StartRenameSelectedAsset();
            }
            DrawScriptAssetContextMenu(*context, asset.type);
            ImGui::EndPopup();
        }

        // Drag source for supported types
        AssetDragDropSource(kAssetPayload, asset.absolutePath.string(), asset.relativePath).Draw();
        if (asset.type == EditorAssetType::Model) {
            AssetDragDropSource(kModelPayload, asset.absolutePath.string(), asset.relativePath).Draw();
        }
        if (asset.type == EditorAssetType::Texture) {
            AssetDragDropSource(kTexturePayload, asset.absolutePath.string(), asset.relativePath).Draw();
        }
        if (asset.type == EditorAssetType::Prefab) {
            AssetDragDropSource(kPrefabPayload, asset.absolutePath.string(), asset.relativePath).Draw();
        }
    }

    // Empty-area context menu
    if (emptyRightClick) {
        ImGui::OpenPopup("##AssetCtxMenu");
    }
    Editor::UI::EditorViewportPolicy::BindNextPopupToViewport(assetBrowserViewportID);
    if (ImGui::BeginPopup("##AssetCtxMenu")) {
        if (ImGui::Selectable("Refresh")) {
            refreshAssets();
            EnsureSelectedFolder();
            SetOperationMessage("Refreshed asset registry", false);
        }
        if (ImGui::Selectable("Validate Assets")) {
            RequestValidateAssets();
        }
        if (ImGui::Selectable("Import...")) {
            if (auto* dialogs = context->GetService<EditorDialogService>())
                dialogs->RequestImportAsset(context->GetWindow());
        }
        const auto emptyContextSelection = ActiveSelectedAssetPaths(*context);
        ImGui::BeginDisabled(emptyContextSelection.empty());
        if (ImGui::Selectable("Open Selected")) {
            RequestOpenAsset(emptyContextSelection.front());
        }
        if (ImGui::Selectable("Reveal Selected in Explorer")) {
            RequestRevealPath(emptyContextSelection.front());
        }
        if (ImGui::Selectable("Reimport Selected")) {
            ReimportSelectedAssets();
        }
        if (ImGui::Selectable("Copy Selected")) {
            const bool copied = operators && operators->Commands().CopyAssets(*context, emptyContextSelection);
            SetOperationMessage(copied ? "Copied " + std::to_string(emptyContextSelection.size()) + " asset(s)"
                                       : "Failed to copy selected asset(s)",
                                !copied);
        }
        if (ImGui::Selectable("Move Selected Here")) {
            MoveSelectedAssetsToFolder(*context, m_SelectedFolder);
        }
        if (ImGui::Selectable("Duplicate Selected")) {
            DuplicateSelectedAssets();
        }
        if (ImGui::Selectable("Delete Selected")) {
            RequestDeleteSelectedAssets();
        }
        ImGui::EndDisabled();
        ImGui::Separator();
        if (ImGui::Selectable("Reveal Folder in Explorer")) {
            RequestRevealPath(m_SelectedFolder);
        }
        if (ImGui::Selectable("Create Folder")) {
            RequestCreateFolderInFolder(*context, m_SelectedFolder, false);
        }
        if (ImGui::Selectable("Paste")) {
            RequestPasteAssetsToFolder(*context, m_SelectedFolder);
            EnsureSelectedFolder();
        }
        ImGui::BeginDisabled(IsRootFolder(m_SelectedFolder));
        if (ImGui::Selectable("Rename Folder")) {
            StartRenameFolder(m_SelectedFolder);
        }
        if (ImGui::Selectable("Delete Folder")) {
            RequestDeleteSelectedFolder();
        }
        ImGui::EndDisabled();
        Editor::UI::EditorViewportPolicy::BindNextPopupToViewport(assetBrowserViewportID);
        if (ImGui::BeginMenu("Create")) {
            if (ImGui::MenuItem("Material")) {
                RequestCreateAssetFromTemplateInFolder(*context, m_SelectedFolder, "material");
            }
            if (ImGui::MenuItem("Default Texture")) {
                RequestCreateAssetFromTemplateInFolder(*context, m_SelectedFolder, "texture");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("AngelScript")) {
                RequestCreateAssetFromTemplateInFolder(*context, m_SelectedFolder, "as");
            }
            if (ImGui::MenuItem("Lua Script")) {
                RequestCreateAssetFromTemplateInFolder(*context, m_SelectedFolder, "lua");
            }
            if (ImGui::MenuItem("Shader")) {
                RequestCreateAssetFromTemplateInFolder(*context, m_SelectedFolder, "shader");
            }
            if (ImGui::MenuItem("Prefab")) {
                RequestCreateAssetFromTemplateInFolder(*context, m_SelectedFolder, "prefab");
            }
            if (ImGui::MenuItem("UI Document")) {
                RequestCreateAssetFromTemplateInFolder(*context, m_SelectedFolder, "ui");
            }
            if (ImGui::MenuItem("Scene")) {
                RequestCreateAssetFromTemplateInFolder(*context, m_SelectedFolder, "scene");
            }
            ImGui::EndMenu();
            EnsureSelectedFolder();
        }
        DrawScriptAssetContextMenu(*context, EditorAssetType::Unknown);
        ImGui::EndPopup();
    }
    ImGui::EndGroup();
    DrawPendingSceneOpenModal();
    SaveWorkspaceState();

#endif
}
