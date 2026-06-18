#pragma once

#include "Scene/Transform.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

class EditorContext;

class IEditorCommand {
public:
    virtual ~IEditorCommand() = default;
    virtual bool Execute(EditorContext& context) = 0;
    virtual bool Undo(EditorContext& context) = 0;
    virtual const char* GetName() const = 0;
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
