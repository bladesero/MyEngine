#pragma once

#include "Scene/Scene.h"
#include "Scene/Transform.h"
#include "Assets/PrefabAsset.h"

#include <nlohmann/json.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class EditorContext;
class Scene;

class IEditorCommand {
public:
    virtual ~IEditorCommand() = default;
    virtual bool Execute(EditorContext& context) = 0;
    virtual bool Undo(EditorContext& context) = 0;
    virtual const char* GetName() const = 0;
};

// ==========================================================================
// IResourceCommand  鈥? resource-level undo / redo (assets, shaders, etc.)
// ==========================================================================
class IResourceCommand : public IEditorCommand {
public:
    virtual bool Execute(EditorContext& context) override = 0;
    virtual bool Undo(EditorContext& context) override = 0;
    virtual const char* GetName() const override = 0;

    // Path of the affected resource (for dependency invalidation).
    virtual const std::string& GetResourcePath() const {
        static const std::string empty;
        return empty;
    }
    virtual bool IsResourceCommand() const { return false; }
};

class EditorCommandStack {
public:
    bool ExecuteCommand(std::unique_ptr<IEditorCommand> command, EditorContext& context);
    bool Undo(EditorContext& context);
    bool Redo(EditorContext& context);
    void Clear();
    bool CanUndo() const { return !m_Undo.empty(); }
    bool CanRedo() const { return !m_Redo.empty(); }
    const char* GetUndoName() const;
    const char* GetRedoName() const;
    bool BeginTransaction(const std::string& name);
    bool CommitTransaction();
    void CancelTransaction();
    bool IsInTransaction() const { return m_InTransaction; }

private:
    std::vector<std::unique_ptr<IEditorCommand>> m_Undo;
    std::vector<std::unique_ptr<IEditorCommand>> m_Redo;
    std::vector<std::unique_ptr<IEditorCommand>> m_TransactionCommands;
    std::string m_TransactionName;
    bool m_InTransaction = false;
};

class LambdaEditorCommand final : public IEditorCommand {
public:
    using Function = std::function<bool(EditorContext&)>;
    LambdaEditorCommand(std::string name, Function execute, Function undo);
    bool Execute(EditorContext& context) override;
    bool Undo(EditorContext& context) override;
    const char* GetName() const override { return m_Name.c_str(); }
private:
    std::string m_Name;
    Function m_Execute;
    Function m_Undo;
};

// Full-scene snapshot (legacy; prefer fine-grained commands where possible)
class SceneSnapshotCommand final : public IEditorCommand {
public:
    SceneSnapshotCommand(std::string name, std::string beforeJson, std::string afterJson,
                         uint64_t beforeSelection = 0, uint64_t afterSelection = 0);
    bool Execute(EditorContext& context) override;
    bool Undo(EditorContext& context) override;
    const char* GetName() const override { return m_Name.c_str(); }
private:
    bool Apply(EditorContext& context, const std::string& json, uint64_t selection);
    std::string m_Name, m_BeforeJson, m_AfterJson;
    uint64_t m_BeforeSelection = 0, m_AfterSelection = 0;
};

// Fine-grained transform command
class SetActorTransformCommand final : public IEditorCommand {
public:
    SetActorTransformCommand(uint64_t actorID, Transform before, Transform after);
    bool Execute(EditorContext& context) override;
    bool Undo(EditorContext& context) override;
    const char* GetName() const override { return "Set Transform"; }
private:
    bool Apply(EditorContext& context, const Transform& transform);
    uint64_t m_ActorID = 0;
    Transform m_Before, m_After;
};

// ---------------------------------------------------------------------------
// Fine-grained scene commands (P0)
// ---------------------------------------------------------------------------

class CreateActorCommand final : public IEditorCommand {
public:
    CreateActorCommand(const ActorCreateDesc& desc, uint64_t newID);
    bool Execute(EditorContext& context) override;
    bool Undo(EditorContext& context) override;
    const char* GetName() const override { return "Create Actor"; }
private:
    ActorCreateDesc m_Desc;
    uint64_t m_ActorID;
    bool m_WasCreated = false;
};

class DestroyActorCommand final : public IEditorCommand {
public:
    DestroyActorCommand(uint64_t actorID, const std::string& subtreeJson,
                        uint64_t parentID);
    bool Execute(EditorContext& context) override;
    bool Undo(EditorContext& context) override;
    const char* GetName() const override { return "Destroy Actor"; }
private:
    bool ReconstructSubtree(Scene& scene);
    uint64_t m_ActorID;
    uint64_t m_ParentID;
    std::string m_SubtreeJson;
    bool m_WasDestroyed = false;
};

class SetParentCommand final : public IEditorCommand {
public:
    SetParentCommand(uint64_t childID, uint64_t beforeParentID, uint64_t afterParentID);
    bool Execute(EditorContext& context) override;
    bool Undo(EditorContext& context) override;
    const char* GetName() const override { return "Set Parent"; }
private:
    uint64_t m_ChildID;
    uint64_t m_BeforeParentID, m_AfterParentID;
};

class MoveActorCommand final : public IEditorCommand {
public:
    MoveActorCommand(uint64_t childID,
                     uint64_t beforeParentID, uint64_t beforeNextSiblingID,
                     uint64_t afterParentID, uint64_t afterNextSiblingID);
    bool Execute(EditorContext& context) override;
    bool Undo(EditorContext& context) override;
    const char* GetName() const override { return "Move Actor"; }
private:
    bool Apply(EditorContext& context, uint64_t parentID, uint64_t nextSiblingID);
    uint64_t m_ChildID;
    uint64_t m_BeforeParentID;
    uint64_t m_BeforeNextSiblingID;
    uint64_t m_AfterParentID;
    uint64_t m_AfterNextSiblingID;
};

class SetActorActiveCommand final : public IEditorCommand {
public:
    SetActorActiveCommand(uint64_t actorID, bool beforeActive, bool afterActive);
    bool Execute(EditorContext& context) override;
    bool Undo(EditorContext& context) override;
    const char* GetName() const override { return "Set Active"; }
private:
    uint64_t m_ActorID;
    bool m_BeforeActive, m_AfterActive;
};

class SetActorNameCommand final : public IEditorCommand {
public:
    SetActorNameCommand(uint64_t actorID, std::string beforeName, std::string afterName);
    bool Execute(EditorContext& context) override;
    bool Undo(EditorContext& context) override;
    const char* GetName() const override { return "Rename Actor"; }
private:
    uint64_t m_ActorID;
    std::string m_BeforeName, m_AfterName;
};

class AddComponentCommand final : public IEditorCommand {
public:
    AddComponentCommand(uint64_t actorID, std::string typeName,
                        const nlohmann::json& initialData);
    bool Execute(EditorContext& context) override;
    bool Undo(EditorContext& context) override;
    const char* GetName() const override { return "Add Component"; }
private:
    uint64_t m_ActorID;
    std::string m_TypeName;
    nlohmann::json m_InitialData;
    bool m_WasAdded = false;
};

class RemoveComponentCommand final : public IEditorCommand {
public:
    RemoveComponentCommand(uint64_t actorID, std::string typeName,
                           const nlohmann::json& serializedData);
    bool Execute(EditorContext& context) override;
    bool Undo(EditorContext& context) override;
    const char* GetName() const override { return "Remove Component"; }
private:
    uint64_t m_ActorID;
    std::string m_TypeName;
    nlohmann::json m_SerializedData;
    bool m_WasRemoved = false;
};

class SetComponentPropertyCommand final : public IEditorCommand {
public:
    SetComponentPropertyCommand(uint64_t actorID, std::string componentType,
                                std::string propertyName,
                                nlohmann::json beforeJson, nlohmann::json afterJson);
    bool Execute(EditorContext& context) override;
    bool Undo(EditorContext& context) override;
    const char* GetName() const override;
private:
    bool ApplyJson(EditorContext& context, const nlohmann::json& json);
    uint64_t m_ActorID;
    std::string m_ComponentType;
    std::string m_PropertyName;
    nlohmann::json m_BeforeJson, m_AfterJson;
};

class ModifyAssetCommand final : public IResourceCommand {
public:
    ModifyAssetCommand(std::string assetPath, std::string beforeContent,
                       std::string afterContent);
    bool Execute(EditorContext& context) override;
    bool Undo(EditorContext& context) override;
    const char* GetName() const override { return "Modify Asset"; }
    const std::string& GetResourcePath() const override { return m_AssetPath; }
    bool IsResourceCommand() const override { return true; }
private:
    std::string m_AssetPath;
    std::string m_BeforeContent;
    std::string m_AfterContent;
};

class CreateAssetCommand final : public IResourceCommand {
public:
    CreateAssetCommand(std::string assetPath, std::string content);
    bool Execute(EditorContext& context) override;
    bool Undo(EditorContext& context) override;
    const char* GetName() const override { return "Create Asset"; }
    const std::string& GetResourcePath() const override { return m_AssetPath; }
    bool IsResourceCommand() const override { return true; }
private:
    std::string m_AssetPath;
    std::string m_Content;
    bool m_Created = false;
};

class DeleteAssetCommand final : public IResourceCommand {
public:
    DeleteAssetCommand(std::string assetPath, std::string content);
    bool Execute(EditorContext& context) override;
    bool Undo(EditorContext& context) override;
    const char* GetName() const override { return "Delete Asset"; }
    const std::string& GetResourcePath() const override { return m_AssetPath; }
    bool IsResourceCommand() const override { return true; }
private:
    std::string m_AssetPath;
    std::string m_Content;
    std::string m_MetaContent;
    bool m_Deleted = false;
    bool m_HadMeta = false;
};

class RenameAssetCommand final : public IResourceCommand {
public:
    RenameAssetCommand(std::string oldPath, std::string newPath);
    bool Execute(EditorContext& context) override;
    bool Undo(EditorContext& context) override;
    const char* GetName() const override { return "Rename Asset"; }
    const std::string& GetResourcePath() const override { return m_NewPath; }
    bool IsResourceCommand() const override { return true; }
private:
    std::string m_OldPath;
    std::string m_NewPath;
    bool m_Renamed = false;
};
