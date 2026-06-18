#include "Editor/EditorCommand.h"

#include "Editor/EditorContext.h"
#include "Scene/Actor.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"

namespace {
class CompositeEditorCommand final : public IEditorCommand {
public:
    CompositeEditorCommand(std::string name,
                           std::vector<std::unique_ptr<IEditorCommand>> commands)
        : m_Name(std::move(name)), m_Commands(std::move(commands))
    {}

    bool Execute(EditorContext& context) override
    {
        size_t executedCount = 0;
        for (auto& command : m_Commands) {
            if (!command->Execute(context)) {
                while (executedCount > 0) m_Commands[--executedCount]->Undo(context);
                return false;
            }
            ++executedCount;
        }
        return true;
    }

    bool Undo(EditorContext& context) override
    {
        size_t undoneCount = 0;
        for (auto it = m_Commands.rbegin(); it != m_Commands.rend(); ++it) {
            if (!(*it)->Undo(context)) {
                auto restore = it.base();
                for (size_t index = 0; index < undoneCount; ++index) {
                    (*restore++)->Execute(context);
                }
                return false;
            }
            ++undoneCount;
        }
        return true;
    }

    const char* GetName() const override { return m_Name.c_str(); }

private:
    std::string m_Name;
    std::vector<std::unique_ptr<IEditorCommand>> m_Commands;
};
}

bool EditorCommandStack::ExecuteCommand(std::unique_ptr<IEditorCommand> command, EditorContext& context)
{
    if (!command || !command->Execute(context)) return false;
    if (m_InTransaction) m_TransactionCommands.push_back(std::move(command));
    else m_Undo.push_back(std::move(command));
    m_Redo.clear();
    context.MarkSceneDirty();
    return true;
}
bool EditorCommandStack::Undo(EditorContext& context) {
    if (m_InTransaction || m_Undo.empty()) return false;
    auto command = std::move(m_Undo.back()); m_Undo.pop_back();
    if (!command->Undo(context)) { m_Undo.push_back(std::move(command)); return false; }
    m_Redo.push_back(std::move(command)); context.MarkSceneDirty(); return true;
}
bool EditorCommandStack::Redo(EditorContext& context) {
    if (m_InTransaction || m_Redo.empty()) return false;
    auto command = std::move(m_Redo.back()); m_Redo.pop_back();
    if (!command->Execute(context)) { m_Redo.push_back(std::move(command)); return false; }
    m_Undo.push_back(std::move(command)); context.MarkSceneDirty(); return true;
}
void EditorCommandStack::Clear() {
    m_Undo.clear();
    m_Redo.clear();
    CancelTransaction();
}
bool EditorCommandStack::BeginTransaction(const std::string& name) {
    if (m_InTransaction) return false;
    m_TransactionName = name.empty() ? "Editor Transaction" : name;
    m_TransactionCommands.clear();
    m_InTransaction = true;
    return true;
}
bool EditorCommandStack::CommitTransaction() {
    if (!m_InTransaction) return false;
    m_InTransaction = false;
    if (m_TransactionCommands.empty()) {
        m_TransactionName.clear();
        return false;
    }
    if (m_TransactionCommands.size() == 1) {
        m_Undo.push_back(std::move(m_TransactionCommands.front()));
    } else {
        m_Undo.push_back(std::make_unique<CompositeEditorCommand>(
            m_TransactionName, std::move(m_TransactionCommands)));
    }
    m_TransactionCommands.clear();
    m_TransactionName.clear();
    return true;
}
void EditorCommandStack::CancelTransaction() {
    m_TransactionCommands.clear();
    m_TransactionName.clear();
    m_InTransaction = false;
}
const char* EditorCommandStack::GetUndoName() const { return CanUndo() ? m_Undo.back()->GetName() : ""; }
const char* EditorCommandStack::GetRedoName() const { return CanRedo() ? m_Redo.back()->GetName() : ""; }

LambdaEditorCommand::LambdaEditorCommand(std::string name, Function execute, Function undo)
    : m_Name(std::move(name)), m_Execute(std::move(execute)), m_Undo(std::move(undo)) {}
bool LambdaEditorCommand::Execute(EditorContext& context) { return m_Execute && m_Execute(context); }
bool LambdaEditorCommand::Undo(EditorContext& context) { return m_Undo && m_Undo(context); }

SceneSnapshotCommand::SceneSnapshotCommand(std::string name, std::string beforeJson,
    std::string afterJson, uint64_t beforeSelection, uint64_t afterSelection)
    : m_Name(std::move(name)), m_BeforeJson(std::move(beforeJson)),
      m_AfterJson(std::move(afterJson)), m_BeforeSelection(beforeSelection),
      m_AfterSelection(afterSelection) {}
bool SceneSnapshotCommand::Execute(EditorContext& context) { return Apply(context, m_AfterJson, m_AfterSelection); }
bool SceneSnapshotCommand::Undo(EditorContext& context) { return Apply(context, m_BeforeJson, m_BeforeSelection); }
bool SceneSnapshotCommand::Apply(EditorContext& context, const std::string& json, uint64_t selection) {
    Scene* scene = context.GetScene();
    if (!scene || !SceneSerializer::LoadFromString(*scene, json)) return false;
    if (selection && scene->FindByID(selection)) context.GetSelection().SelectActorID(selection);
    else context.GetSelection().Clear();
    return true;
}

SetActorTransformCommand::SetActorTransformCommand(uint64_t actorID, Transform before, Transform after)
    : m_ActorID(actorID), m_Before(before), m_After(after) {}
bool SetActorTransformCommand::Execute(EditorContext& context) { return Apply(context, m_After); }
bool SetActorTransformCommand::Undo(EditorContext& context) { return Apply(context, m_Before); }
bool SetActorTransformCommand::Apply(EditorContext& context, const Transform& transform) {
    Scene* scene = context.GetScene(); Actor* actor = scene ? scene->FindByID(m_ActorID) : nullptr;
    if (!actor) return false;
    actor->GetTransform() = transform; context.GetSelection().SelectActorID(m_ActorID); return true;
}
