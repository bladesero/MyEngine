#pragma once

#include "Editor/EditorPanel.h"
#include "Editor/EditorInspectorSection.h"
#include "Editor/EditorSelection.h"
#include "Editor/EditorUndoUtil.h"
#include "Editor/EditorUI/EditorScriptRegistry.h"
#include "Scene/ActorHandle.h"
#include "Editor/EditorViewportControllers.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Actor;
class EditorOperators;
class SceneViewport;
struct EditorAssetInfo;
class ToolbarPanel final : public EditorPanel {
public:
    ToolbarPanel();

protected:
    void DrawContent() override;

private:
    int GetWindowFlags() const override;
};
class SceneHierarchyPanel final : public EditorPanel {
public:
    SceneHierarchyPanel();
    bool HandleEditorAction(EditorContext& context, std::string_view actionID) override;
    bool CanHandleEditorAction(const EditorContext& context, std::string_view actionID) const override;

protected:
    void DrawContent() override;

private:
    void DrawToolbar();
    void DrawActor(Actor* actor);
    void HandleDragDropTarget(Actor* targetParent);
    bool RebuildSearchCache(Actor* actor);
    bool HasHierarchyFilters() const;
    bool ActorMatchesOwnFilters(const Actor& actor) const;
    bool ActorMatchesSearch(const Actor& actor) const;
    void CollectVisibleActorOrder(Actor* actor, std::vector<uint64_t>& order) const;
    ActorHandle m_DraggedActor;
    bool m_ActorRightClicked = false;
    char m_SearchFilter[128] = {};
    char m_TagFilter[128] = {};
    char m_ComponentFilter[128] = {};
    uint32_t m_LayerFilter = 0;
    bool m_LayerFilterEnabled = false;
    char m_RenameBuffer[256] = {};
    uint64_t m_PendingRenameID = 0;
    uint64_t m_LastClickedActorID = 0;
    int m_OpenRequest = 0;
    std::unordered_map<uint64_t, bool> m_SearchMatches;
};
class SceneViewportPanel final : public EditorPanel {
public:
    explicit SceneViewportPanel(std::shared_ptr<EditorGizmoState> state);

protected:
    void DrawContent() override;

private:
    int GetWindowFlags() const override;
    void BeforeBegin() override;
    void AfterEnd() override;
    bool DrawSceneViewOverlay(EditorContext& context, SceneViewport& viewport, EditorGizmoState& state,
                              const EditorPanelRect& rect);
    void DropModel(const std::string& path, float x, float y);
    void DropPrefab(const std::string& path, float x, float y);
    std::shared_ptr<EditorGizmoState> m_State;
    EditorPickingController m_PickingController;
    EditorGizmoController m_GizmoController;
};
class GameViewportPanel final : public EditorPanel {
public:
    GameViewportPanel();

protected:
    void DrawContent() override;

private:
    int GetWindowFlags() const override;
    void BeforeBegin() override;
    void AfterEnd() override;
};
class InspectorPanel final : public EditorPanel {
public:
    explicit InspectorPanel(std::shared_ptr<EditorGizmoState> state);
    ~InspectorPanel() override;
    void OnAttach(EditorContext& context) override;
    void OnDetach() override;

protected:
    void DrawContent() override;

private:
    void OnSelectionChanged(const EditorSelectionChangedEvent& event);
    std::shared_ptr<EditorGizmoState> m_State;
    EditorInspectorRegistry m_SectionRegistry;
    EditorSceneTransaction m_Transaction;
    EditorSelectObject m_SelectedObject;
    EditorSelection::ListenerID m_SelectionListenerID = 0;
    char m_ActorNameBuffer[256] = {};
    uint64_t m_ActorNameEditID = 0;
    bool m_ActorNameDirty = false;
    char m_ActorTagBuffer[128] = {};
    uint64_t m_ActorTagEditID = 0;
    bool m_ActorTagDirty = false;
    char m_MultiActorTagBuffer[128] = {};
    bool m_MultiActorTagDirty = false;
    char m_MultiAddComponentSearch[128] = {};
    char m_PrefabOverrideFilter[128] = {};
    bool m_PrefabOverrideDiagnosticsOnly = false;
    std::unordered_map<std::string, bool> m_PrefabOverrideCategoryOpen;
    bool m_PrefabOperationMessageIsError = false;
    std::string m_PrefabOperationMessage;
};
class AssetBrowserPanel final : public EditorPanel {
public:
    AssetBrowserPanel();
    void OnAttach(EditorContext& context) override;
    void OnDetach() override;
    void OnUpdate(float dt) override;
    bool HandleEditorAction(EditorContext& context, std::string_view actionID) override;
    bool CanHandleEditorAction(const EditorContext& context, std::string_view actionID) const override;

protected:
    void DrawContent() override;

private:
    struct PendingAssetRetarget {
        std::string oldPath;
        std::string newPath;
        size_t referenceCount = 0;
        size_t projectReferenceCount = 0;
    };
    void DeleteSelectedAsset();
    void DeleteSelectedAssets();
    void DrawDeleteReferenceWarning(const std::vector<std::string>& paths);
    std::vector<std::string> CollectAssetPathsForFolder(const EditorContext& context,
                                                        const std::string& folderPath) const;
    void RequestDeleteSelectedAssets();
    void RequestDeleteFolder(const std::string& folderPath);
    void RequestDeleteSelectedFolder();
    void DeleteSelectedFolder();
    void DuplicateSelectedAsset();
    void DuplicateSelectedAssets();
    void ReimportSelectedAssets();
    bool MoveSelectedAssetsToFolder(EditorContext& context, const std::string& targetFolder);
    void RenameSelectedAsset();
    void RenameSelectedFolder();
    bool StartRenameSelectedAsset();
    void StartRenameFolder(const std::string& folderPath);
    void SelectAssetRow(EditorContext& context, const std::vector<EditorAssetInfo>& visibleAssets, size_t index,
                        bool toggle, bool range);
    void SelectVisibleAssets(EditorContext& context, const std::vector<EditorAssetInfo>& visibleAssets);
    bool IsAssetSelected(const std::string& path) const;
    std::vector<std::string> ActiveSelectedAssetPaths(const EditorContext& context) const;
    void SyncAssetSelectionFromContext(const EditorContext& context);
    bool RequestCreateFolderInFolder(EditorContext& context, const std::string& parentFolder, bool selectCreated);
    bool RequestCreateAssetFromTemplateInFolder(EditorContext& context, const std::string& folderPath,
                                                const char* templateID);
    bool RequestPasteAssetsToFolder(EditorContext& context, const std::string& targetFolder);
    bool RequestOpenAsset(const std::string& path);
    bool RequestOpenFolder(const std::string& folderPath);
    bool RequestRevealPath(const std::string& path);
    bool RequestValidateAssets();
    bool OpenPendingSceneAsset(bool discardUnsavedChanges);
    void DrawPendingSceneOpenModal();
    void SetOperationMessage(std::string message, bool error);
    void ClearOperationMessage();
    std::vector<PendingAssetRetarget> BuildPendingRetargetsForFolder(EditorContext& context, EditorOperators* operators,
                                                                     const std::string& oldFolder,
                                                                     const std::string& newFolder) const;
    void SetPendingRetargets(std::vector<PendingAssetRetarget> retargets);
    void ClearPendingRetargets();
    bool ExecutePendingRetargets();
    void SetValidationSummary(std::string message, bool error);
    void ClearValidationSummary();
    void EnsureSelectedFolder();
    void LoadWorkspaceState();
    void SaveWorkspaceState() const;
    std::filesystem::path FolderPathToAbsolute(const std::string& folderPath, const char* fallback) const;
    std::filesystem::path CurrentContentDirectory(const char* fallback) const;
    std::filesystem::path TemplateTargetDirectoryInFolder(const std::string& folderPath, const char* templateID) const;
    std::filesystem::path TemplateTargetDirectory(const char* templateID) const;
    char m_Filter[128] = {};
    char m_RenameBuffer[256] = {};
    char m_FolderRenameBuffer[256] = {};
    std::string m_SelectedFolder = "Content";
    std::string m_PendingFolderRenamePath;
    std::string m_LastPrimaryAssetPath;
    std::vector<std::string> m_SelectedAssetPaths;
    int m_TypeFilter = 0;
    int m_ImportStateFilter = 0;
    bool m_RecursiveAssets = true;
    bool m_DiagnosticsOnly = false;
    bool m_PendingRename = false;
    bool m_PendingFolderRename = false;
    bool m_PendingDelete = false;
    bool m_PendingFolderDelete = false;
    bool m_PendingSceneOpenPopup = false;
    bool m_OperationMessageIsError = false;
    bool m_ValidationSummaryIsError = false;
    std::string m_PendingSceneOpenPath;
    std::string m_PendingFolderDeletePath;
    std::string m_OperationMessage;
    std::string m_ValidationSummaryMessage;
    std::vector<PendingAssetRetarget> m_PendingAssetRetargets;
    float m_WatchAccumulator = 0.0f;
};
class LogPanel final : public EditorPanel {
public:
    LogPanel();

protected:
    void DrawContent() override;
};
class ProfilerPanel final : public EditorPanel {
public:
    ProfilerPanel();

protected:
    void DrawContent() override;

private:
    char m_CategoryFilter[64] = {};
    float m_MinDurationMs = 0.0f;
};
class ScriptedToolPanel final : public EditorPanel {
public:
    explicit ScriptedToolPanel(EditorScriptPanelSpec spec);
    std::string GetDefaultDockArea() const override;

protected:
    void DrawContent() override;

private:
    EditorScriptPanelSpec m_Spec;
};
