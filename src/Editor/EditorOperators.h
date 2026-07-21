#pragma once

#include "Editor/EditorClipboardService.h"
#include "Editor/EditorSelection.h"
#include "Editor/EditorUndoUtil.h"
#include "Core/EngineMath.h"

#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

class Actor;
class Component;
class EditorContext;
enum class SceneViewDirection;

enum class EditorSelectionIntentMode : uint8_t {
    Replace,
    Add,
    Toggle,
};

struct EditorSelectionSnapshot {
    uint64_t actorID = 0;
    std::string assetPath;
    bool hasActor = false;
    bool hasAsset = false;
};

struct EditorRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

class EditorSelectionOperator {
public:
    bool SelectActor(EditorContext& context, uint64_t actorID,
                     EditorSelectionIntentMode mode = EditorSelectionIntentMode::Replace) const;
    bool SelectActorRange(EditorContext& context, uint64_t anchorActorID, uint64_t targetActorID,
                          const std::vector<uint64_t>& orderedActorIDs = {}) const;
    bool SelectActorSubtree(EditorContext& context, uint64_t actorID, bool includeRoot = true) const;
    bool SelectAsset(EditorContext& context, const std::string& path) const;
    void Clear(EditorContext& context) const;
    EditorSelectionSnapshot GetSelectionSnapshot(const EditorContext& context) const;
};

class EditorCommandOperator {
public:
    explicit EditorCommandOperator(EditorClipboardService* clipboard = nullptr) : m_Clipboard(clipboard) {}

    bool ExecuteAction(EditorContext& context, const std::string& actionID) const;
    uint64_t CreateActor(EditorContext& context, const std::string& name) const;
    uint64_t CreateChildActor(EditorContext& context, const std::string& name, uint64_t parentActorID) const;
    uint64_t CreateUIActor(EditorContext& context, const std::string& presetID, uint64_t parentActorID = 0) const;
    uint64_t CreateEmptyParent(EditorContext& context, uint64_t actorID) const;
    bool DuplicateActorSubtree(EditorContext& context, uint64_t actorID) const;
    bool DuplicateSelection(EditorContext& context) const;
    bool RenameActor(EditorContext& context, uint64_t actorID, const std::string& name) const;
    bool DeleteActor(EditorContext& context, uint64_t actorID) const;
    bool DeleteSelection(EditorContext& context) const;
    bool RenameSelection(EditorContext& context, const std::string& name) const;
    bool CopySelection(EditorContext& context) const;
    bool CopyAssets(EditorContext& context, const std::vector<std::string>& paths) const;
    bool HasActorClipboard() const;
    bool HasAssetClipboard() const;
    bool PasteSelection(EditorContext& context) const;
    bool PasteAssetToFolder(EditorContext& context, const std::string& targetFolder) const;
    bool SetActorActive(EditorContext& context, uint64_t actorID, bool active) const;
    bool SetActorTag(EditorContext& context, uint64_t actorID, const std::string& tag) const;
    bool SetActorLayer(EditorContext& context, uint64_t actorID, uint32_t layer) const;
    bool SetActorEditorFlags(EditorContext& context, uint64_t actorID, uint32_t flags) const;
    bool SetActorStatic(EditorContext& context, uint64_t actorID, bool isStatic) const;
    bool SetSceneName(EditorContext& context, const std::string& name) const;
    bool SetSceneGravity(EditorContext& context, const Vec3& gravity) const;
    bool SetSceneMainCameraHint(EditorContext& context, uint64_t actorID) const;
    bool SetSceneAmbientIntensity(EditorContext& context, float intensity) const;
    bool SetActorsActive(EditorContext& context, const std::vector<uint64_t>& actorIDs, bool active) const;
    bool SetActorsTag(EditorContext& context, const std::vector<uint64_t>& actorIDs, const std::string& tag) const;
    bool SetActorsLayer(EditorContext& context, const std::vector<uint64_t>& actorIDs, uint32_t layer) const;
    bool SetActorsEditorFlags(EditorContext& context, const std::vector<uint64_t>& actorIDs, uint32_t flags) const;
    bool SetActorsStatic(EditorContext& context, const std::vector<uint64_t>& actorIDs, bool isStatic) const;
    bool SetActorsPosition(EditorContext& context, const std::vector<uint64_t>& actorIDs, const Vec3& position) const;
    bool SetActorsRotation(EditorContext& context, const std::vector<uint64_t>& actorIDs, const Vec3& rotation) const;
    bool SetActorsScale(EditorContext& context, const std::vector<uint64_t>& actorIDs, const Vec3& scale) const;
    bool MoveActor(EditorContext& context, uint64_t actorID, uint64_t afterParentID, uint64_t afterNextSiblingID) const;
    bool UnparentActor(EditorContext& context, uint64_t actorID) const;
    bool MoveActorUp(EditorContext& context, uint64_t actorID) const;
    bool MoveActorDown(EditorContext& context, uint64_t actorID) const;

private:
    EditorClipboardService* m_Clipboard = nullptr;
};

class EditorComponentOperator {
public:
    bool AddComponent(EditorContext& context, uint64_t actorID, const std::string& typeName,
                      const nlohmann::json& initialData = nlohmann::json::object()) const;
    bool AddComponents(EditorContext& context, const std::vector<uint64_t>& actorIDs, const std::string& typeName,
                       const nlohmann::json& initialData = nlohmann::json::object()) const;
    bool RemoveComponent(EditorContext& context, uint64_t actorID, const std::string& typeName) const;
    bool RemoveComponent(EditorContext& context, Actor& actor, const Component& component) const;
    bool RemoveComponents(EditorContext& context, const std::vector<uint64_t>& actorIDs,
                          const std::string& typeName) const;
    bool SetComponentPropertyForActors(EditorContext& context, const std::vector<uint64_t>& actorIDs,
                                       const std::string& typeName, const std::string& propertyName,
                                       const nlohmann::json& value) const;
    bool SetEnabled(EditorContext& context, uint64_t actorID, const std::string& typeName, bool enabled) const;
    bool SetJson(EditorContext& context, uint64_t actorID, const std::string& typeName,
                 const nlohmann::json& beforeJson, const nlohmann::json& afterJson,
                 const std::string& label = {}) const;
    bool SetProperty(EditorContext& context, Actor& actor, const std::string& componentType,
                     const std::string& propertyName, const nlohmann::json& beforeJson,
                     const nlohmann::json& afterJson) const;
};

class EditorDragDropOperator {
public:
    bool ApplyActorDrop(EditorContext& context, uint64_t actorID, uint64_t afterParentID,
                        uint64_t afterNextSiblingID) const;
    bool ApplyAssetDrop(EditorContext& context, const std::string& assetPath, const std::string& targetPath) const;
};

class EditorTransactionOperator {
public:
    void BeginSnapshot(EditorSceneTransaction& transaction, const char* label, const std::string& beforeJson,
                       uint64_t selection) const;
    bool CommitIfChanged(EditorContext& context, EditorSceneTransaction& transaction) const;
    bool CommitSceneSnapshot(EditorContext& context, const char* label, const std::string& beforeJson,
                             const std::string& afterJson, uint64_t beforeSelection, uint64_t afterSelection) const;
    bool CommitComponentProperty(EditorContext& context, Actor& actor, const std::string& componentType,
                                 const std::string& propertyName, const nlohmann::json& beforeJson,
                                 const nlohmann::json& afterJson) const;
    void Cancel(EditorSceneTransaction& transaction) const;
};

class EditorAssetOperator {
public:
    struct SceneReferenceInfo {
        std::string scenePath;
        uint64_t actorID = 0;
        std::string actorName;
        std::string componentType;
        std::string jsonPath;
        std::string valuePreview;
    };

    bool Refresh(EditorContext& context) const;
    bool WatchIfDue(EditorContext& context, float deltaSeconds, float& accumulator, float intervalSeconds = 1.0f) const;
    bool CreateFolder(EditorContext& context, const std::string& folderPath) const;
    bool RenameFolder(EditorContext& context, const std::string& folderPath, const std::string& newNameOrPath) const;
    bool DeleteFolder(EditorContext& context, const std::string& folderPath) const;
    bool DeleteAsset(EditorContext& context, const std::string& path) const;
    bool RenameAsset(EditorContext& context, const std::string& path, const std::string& newNameOrPath) const;
    bool MoveAsset(EditorContext& context, const std::string& path, const std::string& targetFolder) const;
    bool MoveFolder(EditorContext& context, const std::string& folderPath, const std::string& targetFolder) const;
    bool CopyAssetToFolder(EditorContext& context, const std::string& path, const std::string& targetFolder) const;
    bool DuplicateAsset(EditorContext& context, const std::string& path) const;
    bool CreateAssetFromTemplate(EditorContext& context, const std::string& folderPath,
                                 const std::string& templateID) const;
    bool OpenAsset(EditorContext& context, const std::string& path) const;
    bool RevealAsset(EditorContext& context, const std::string& path) const;
    std::vector<SceneReferenceInfo> FindSceneReferences(EditorContext& context, const std::string& path) const;
    std::vector<SceneReferenceInfo> FindProjectSceneReferences(EditorContext& context, const std::string& path) const;
    size_t RetargetSceneReferences(EditorContext& context, const std::string& oldPath,
                                   const std::string& newPath) const;
    size_t RetargetProjectSceneReferences(EditorContext& context, const std::string& oldPath,
                                          const std::string& newPath) const;
    bool Reimport(EditorContext& context, const std::string& uuid) const;
    bool ReimportAll(EditorContext& context, std::vector<std::string>* failures = nullptr) const;
    bool ReimportWithSettings(EditorContext& context, const std::string& uuid, const std::string& settingsJson) const;
};

class EditorPrefabOperator {
public:
    struct OverrideInfo {
        size_t index = 0;
        std::string kind;
        std::string localId;
        std::string componentType;
        std::string path;
        std::string category;
        std::string target;
        std::string property;
        std::string label;
        std::string valuePreview;
        std::string diagnostic;
        bool canApply = false;
        bool canRevert = false;
    };

    std::vector<OverrideInfo> GetOverrides(EditorContext& context, uint64_t actorID) const;
    bool ApplyAll(EditorContext& context, uint64_t actorID) const;
    bool RevertAll(EditorContext& context, uint64_t actorID) const;
    bool Unpack(EditorContext& context, uint64_t actorID) const;
    bool CreatePrefabFromActor(EditorContext& context, uint64_t actorID) const;
    uint64_t InstantiatePrefab(EditorContext& context, const std::string& path, uint64_t parentActorID = 0,
                               const std::optional<Transform>& rootTransform = std::nullopt,
                               const char* commandName = "Instantiate Prefab") const;
    size_t SelectInstances(EditorContext& context, const std::string& path) const;
    bool ApplyOverride(EditorContext& context, uint64_t actorID, size_t overrideIndex) const;
    bool RevertOverride(EditorContext& context, uint64_t actorID, size_t overrideIndex) const;
    bool ApplyOverride(EditorContext& context, uint64_t actorID, const std::string& overridePath) const;
    bool RevertOverride(EditorContext& context, uint64_t actorID, const std::string& overridePath) const;
};

class EditorViewportOperator {
public:
    bool SetSceneViewportRect(EditorContext& context, const EditorRect& rect, bool hovered) const;
    bool SetGameViewportRect(EditorContext& context, const EditorRect& rect) const;
    bool FrameSelected(EditorContext& context) const;
    bool FrameTarget(EditorContext& context, const Vec3& target, float radius = 1.0f) const;
    bool FrameDirection(EditorContext& context, SceneViewDirection direction) const;
    bool OrbitAroundSelection(EditorContext& context, float yawDegrees, float pitchDegrees) const;
    bool ToggleSceneProjection(EditorContext& context) const;
    bool DropModel(EditorContext& context, const std::string& path, float screenX, float screenY) const;
};

class EditorOperators {
public:
    EditorOperators();

    EditorSelectionOperator& Selection() { return m_Selection; }
    const EditorSelectionOperator& Selection() const { return m_Selection; }
    EditorCommandOperator& Commands() { return m_Commands; }
    const EditorCommandOperator& Commands() const { return m_Commands; }
    EditorDragDropOperator& DragDrop() { return m_DragDrop; }
    const EditorDragDropOperator& DragDrop() const { return m_DragDrop; }
    EditorComponentOperator& Components() { return m_Components; }
    const EditorComponentOperator& Components() const { return m_Components; }
    EditorTransactionOperator& Transactions() { return m_Transactions; }
    const EditorTransactionOperator& Transactions() const { return m_Transactions; }
    EditorAssetOperator& Assets() { return m_Assets; }
    const EditorAssetOperator& Assets() const { return m_Assets; }
    EditorPrefabOperator& Prefabs() { return m_Prefabs; }
    const EditorPrefabOperator& Prefabs() const { return m_Prefabs; }
    EditorViewportOperator& Viewport() { return m_Viewport; }
    const EditorViewportOperator& Viewport() const { return m_Viewport; }

private:
    EditorClipboardService m_Clipboard;
    EditorSelectionOperator m_Selection;
    EditorCommandOperator m_Commands;
    EditorDragDropOperator m_DragDrop;
    EditorComponentOperator m_Components;
    EditorTransactionOperator m_Transactions;
    EditorAssetOperator m_Assets;
    EditorPrefabOperator m_Prefabs;
    EditorViewportOperator m_Viewport;
};
