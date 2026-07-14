#include "Editor/EditorOperatorShared.h"

void EditorTransactionOperator::BeginSnapshot(EditorSceneTransaction& transaction, const char* label,
                                              const std::string& beforeJson, uint64_t selection) const {
    transaction.Begin(label, beforeJson, selection);
}

bool EditorTransactionOperator::CommitIfChanged(EditorContext& context, EditorSceneTransaction& transaction) const {
    return transaction.Commit(context);
}

bool EditorTransactionOperator::CommitSceneSnapshot(EditorContext& context, const char* label,
                                                    const std::string& beforeJson, const std::string& afterJson,
                                                    uint64_t beforeSelection, uint64_t afterSelection) const {
    if (beforeJson.empty() || afterJson.empty() || beforeJson == afterJson)
        return false;
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack || !context.CanEditScene())
        return false;
    SceneSerializer::LoadFromString(*scene, beforeJson);
    return stack->ExecuteCommand(EditorUndoUtil::MakeSceneSnapshotCommand(label ? label : "Scene Edit", beforeJson,
                                                                          afterJson, beforeSelection, afterSelection),
                                 context);
}

bool EditorTransactionOperator::CommitComponentProperty(EditorContext& context, Actor& actor,
                                                        const std::string& componentType,
                                                        const std::string& propertyName,
                                                        const nlohmann::json& beforeJson,
                                                        const nlohmann::json& afterJson) const {
    if (componentType.empty() || propertyName.empty() || beforeJson == afterJson)
        return false;
    EditorCommandStack* stack = context.GetCommandStack();
    if (!stack || !context.CanEditScene())
        return false;
    return stack->ExecuteCommand(
        EditorUndoUtil::MakeSetPropertyCommand(actor, componentType, propertyName, beforeJson, afterJson), context);
}

void EditorTransactionOperator::Cancel(EditorSceneTransaction& transaction) const {
    transaction.Cancel();
}
