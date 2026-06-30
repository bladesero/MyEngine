#pragma once

#include "Editor/EditorSelection.h"
#include "Editor/EditorUndoUtil.h"

#include <cstdint>
#include <filesystem>
#include <string>

class Actor;
class EditorContext;

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
    bool SelectAsset(EditorContext& context, const std::string& path) const;
    void Clear(EditorContext& context) const;
    EditorSelectionSnapshot GetSelectionSnapshot(const EditorContext& context) const;
};

class EditorCommandOperator {
public:
    bool ExecuteAction(EditorContext& context, const std::string& actionID) const;
    uint64_t CreateActor(EditorContext& context, const std::string& name) const;
    bool RenameActor(EditorContext& context, uint64_t actorID, const std::string& name) const;
    bool DeleteActor(EditorContext& context, uint64_t actorID) const;
    bool SetActorActive(EditorContext& context, uint64_t actorID, bool active) const;
    bool MoveActor(EditorContext& context, uint64_t actorID,
                   uint64_t afterParentID, uint64_t afterNextSiblingID) const;
};

class EditorDragDropOperator {
public:
    bool ApplyActorDrop(EditorContext& context, uint64_t actorID,
                        uint64_t afterParentID, uint64_t afterNextSiblingID) const;
    bool ApplyAssetDrop(EditorContext& context, const std::string& assetPath,
                        const std::string& targetPath) const;
};

class EditorTransactionOperator {
public:
    void BeginSnapshot(EditorSceneTransaction& transaction, const char* label,
                       const std::string& beforeJson, uint64_t selection) const;
    bool CommitIfChanged(EditorContext& context, EditorSceneTransaction& transaction) const;
    bool CommitSceneSnapshot(EditorContext& context, const char* label,
                             const std::string& beforeJson, const std::string& afterJson,
                             uint64_t beforeSelection, uint64_t afterSelection) const;
    void Cancel(EditorSceneTransaction& transaction) const;
};

class EditorAssetOperator {
public:
    bool Refresh(EditorContext& context) const;
    bool WatchIfDue(EditorContext& context, float deltaSeconds, float& accumulator,
                    float intervalSeconds = 1.0f) const;
    bool DeleteAsset(EditorContext& context, const std::string& path) const;
    bool RenameAsset(EditorContext& context, const std::string& path,
                     const std::string& newNameOrPath) const;
    bool DuplicateAsset(EditorContext& context, const std::string& path) const;
    bool Reimport(EditorContext& context, const std::string& uuid) const;
};

class EditorViewportOperator {
public:
    bool SetSceneViewportRect(EditorContext& context, const EditorRect& rect,
                              bool hovered) const;
    bool SetGameViewportRect(EditorContext& context, const EditorRect& rect) const;
};

class EditorOperators {
public:
    EditorSelectionOperator& Selection() { return m_Selection; }
    const EditorSelectionOperator& Selection() const { return m_Selection; }
    EditorCommandOperator& Commands() { return m_Commands; }
    const EditorCommandOperator& Commands() const { return m_Commands; }
    EditorDragDropOperator& DragDrop() { return m_DragDrop; }
    const EditorDragDropOperator& DragDrop() const { return m_DragDrop; }
    EditorTransactionOperator& Transactions() { return m_Transactions; }
    const EditorTransactionOperator& Transactions() const { return m_Transactions; }
    EditorAssetOperator& Assets() { return m_Assets; }
    const EditorAssetOperator& Assets() const { return m_Assets; }
    EditorViewportOperator& Viewport() { return m_Viewport; }
    const EditorViewportOperator& Viewport() const { return m_Viewport; }

private:
    EditorSelectionOperator m_Selection;
    EditorCommandOperator m_Commands;
    EditorDragDropOperator m_DragDrop;
    EditorTransactionOperator m_Transactions;
    EditorAssetOperator m_Assets;
    EditorViewportOperator m_Viewport;
};
